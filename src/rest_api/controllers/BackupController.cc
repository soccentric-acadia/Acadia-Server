// rest_api/controllers/BackupController.cc — database backup (admin only).
//
// SRV-003: this endpoint previously streamed a full pg_dump of the database
// back to the caller with no authentication. It now (a) requires an
// authenticated admin user, (b) writes the dump to the server's backup
// directory instead of returning it, and (c) never interpolates the DB URL
// through a shell unquoted.

#include "rest_api/controllers/BackupController.h"

#include <cstdlib>
#include <ctime>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace acadia::rest
{

namespace {

// Single-quote a shell argument (for popen; ACADIA_DB_URL is operator
// supplied but must never terminate the quoting).
std::string shell_quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

} // namespace

void BackupController::backup(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    // SRV-003 (a): admin role required.
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    if (user_id.empty() || !auth_svc_.is_admin(user_id)) {
        Json::Value err;
        err["error"] = "Forbidden: admin role required";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k403Forbidden);
        cb(resp);
        return;
    }

    // SRV-003 (b): dump goes to disk, not to the caller.
    const char* backup_dir_env = std::getenv("ACADIA_BACKUP_DIR");
    std::string backup_dir = backup_dir_env && *backup_dir_env ? backup_dir_env : "/var/lib/acadia/backups";
    std::filesystem::create_directories(backup_dir); // best effort; errors surface at write time

    // Retrieve connection string from environment (set at server startup).
    const char* db_url = std::getenv("ACADIA_DB_URL");
    if (!db_url) {
        Json::Value err;
        err["error"] = "ACADIA_DB_URL not set";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k500InternalServerError);
        cb(resp);
        return;
    }

    // Build a timestamped filename.
    std::time_t now = std::time(nullptr);
    char ts_buf[32];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%dT%H%M%SZ", std::gmtime(&now));
    std::string filename = std::string("acadia-backup-") + ts_buf + ".dump";
    std::string out_path = backup_dir + "/" + filename;

    // pg_dump writes straight to the file (server-side); only the exit status
    // is captured. The URL is quoted to keep it a single shell argument.
    std::ostringstream cmd;
    cmd << "umask 077 && pg_dump --format=plain " << shell_quote(db_url) << " > " << shell_quote(out_path) << " 2>/dev/null";
    int rc = std::system(cmd.str().c_str());
    int exit_code = (rc == -1) ? -1 : WEXITSTATUS(rc);
    if (exit_code != 0) {
        Json::Value err;
        err["error"] = "pg_dump exited with error";
        err["exit_code"] = exit_code;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k500InternalServerError);
        cb(resp);
        return;
    }

    std::error_code ec;
    auto size = std::filesystem::file_size(out_path, ec);
    std::uintmax_t bytes = ec ? 0 : size;

    Json::Value body;
    body["status"] = "ok";
    body["file"] = out_path;
    body["bytes"] = static_cast<Json::Value::Int64>(bytes);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(drogon::k200OK);
    cb(resp);
}

} // namespace acadia::rest
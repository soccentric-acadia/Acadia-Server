// rest_api/controllers/BackupController.cc — database backup (admin only).
//
// SRV-003: requires an authenticated admin user and writes the dump to the
// server's backup directory instead of streaming it to the caller.
// SRV-004: the DB password is never placed on the process command line
// (visible in `ps`); pg_dump reads it from a 0600 PGPASSFILE that is deleted
// after the dump completes.
// SRV-005: the connection comes from the configured DbSettings, not the
// non-existent ACADIA_DB_URL environment variable.
// SRV-006: pg_dump runs on a worker thread — the Drogon IO loop is never
// blocked even for multi-GB dumps.

#include "rest_api/controllers/BackupController.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <spdlog/spdlog.h>

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace acadia::rest
{

namespace
{

// Single-quote a shell argument so it survives metacharacters.
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

// libpq DSN value escaping (SRV-083): quote when it contains '=', ' ', or a
// leading/trailing backslash; escape backslash and double-quote inside.
std::string dsn_quote(const std::string& v)
{
    if (!v.empty()) {
        bool need = false;
        for (char c : v) {
            if (c == ' ' || c == '=' || c == '\\' || c == '"' || c == '\t') {
                need = true;
                break;
            }
        }
        if (need) {
            std::string out = "\"";
            for (char c : v) {
                if (c == '\\' || c == '"')
                    out += '\\';
                out += c;
            }
            out += "\"";
            return out;
        }
    }
    return v;
}

// Build a libpq keyword/equals DSN from DbSettings WITHOUT the password
// (the password is passed via PGPASSFILE — SRV-004).
std::string dsn_without_password(const DbSettings& db)
{
    std::ostringstream o;
    o << "host=" << dsn_quote(db.host) << " port=" << db.port << " dbname=" << dsn_quote(db.name) << " user=" << dsn_quote(db.user);
    if (!db.sslmode.empty())
        o << " sslmode=" << dsn_quote(db.sslmode);
    o << " connect_timeout=" << db.connect_timeout;
    return o.str();
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

    // SRV-006: run the (potentially slow) pg_dump on a worker thread so the
    // Drogon IO thread is never blocked; Drogon invokes the callback safely
    // from any thread.
    std::thread([this, cb = std::move(cb)]() mutable { this->run_backup(std::move(cb)); }).detach();
}

void BackupController::run_backup(std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    auto finish_error = [cb](drogon::HttpStatusCode code, std::string message, std::string detail = "") {
        Json::Value err;
        err["error"] = message;
        if (!detail.empty())
            err["detail"] = detail;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(code);
        cb(resp);
    };

    try {
        std::filesystem::path dir{backup_dir_};
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        // Timestamped, server-side controlled file name (no caller input).
        std::time_t now = std::time(nullptr);
        char ts_buf[32];
        std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%dT%H%M%SZ", std::gmtime(&now));
        std::string out_path = (dir / ("acadia-backup-" + std::string(ts_buf) + ".dump")).string();

        std::string dsn = dsn_without_password(db_cfg_);

        // SRV-004: keep the password out of argv / `ps`. libpq reads
        // $PGPASSFILE (format: host:port:dbname:user:password) before
        // falling back to the DSN.
        std::string passfile;
        if (!db_cfg_.password.empty()) {
            passfile = (dir / (".pgpass-" + std::to_string(::getpid()))).string();
            {
                std::ofstream pf{passfile, std::ios::trunc};
                pf << db_cfg_.host << ":" << db_cfg_.port << ":" << db_cfg_.name << ":" << db_cfg_.user << ":" << db_cfg_.password << "\n";
            }
            if (chmod(passfile.c_str(), 0600) != 0) {
                unlink(passfile.c_str());
                passfile.clear();
                return finish_error(drogon::k500InternalServerError, "backup failed", "could not secure temporary credential file");
            }
        }

        // pg_dump with -f writes straight to the target file. The password is
        // passed via PGPASSFILE so it never appears on the process command
        // line (visible in `ps`) — SRV-004.
        std::string pgpass_env = passfile.empty() ? "" : ("PGPASSFILE=" + shell_quote(passfile) + " ");
        std::string full = pgpass_env + "pg_dump --format=plain --no-password " + shell_quote(dsn) + " -f " + shell_quote(out_path);

        int rc = std::system(full.c_str());
        int exit_code = (rc == -1) ? -1 : WEXITSTATUS(rc);

        // Always remove the credential file first.
        if (!passfile.empty()) {
            unlink(passfile.c_str());
        }

        if (exit_code != 0) {
            spdlog::error("backup: pg_dump exited with {} (dir={})", exit_code, backup_dir_);
            return finish_error(drogon::k500InternalServerError, "pg_dump exited with error", "exit_code=" + std::to_string(exit_code));
        }

        auto size = std::filesystem::file_size(out_path, ec);
        Json::Value body;
        body["status"] = "ok";
        body["file"] = out_path;
        body["bytes"] = static_cast<Json::Value::Int64>(ec ? 0 : size);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(drogon::k200OK);
        cb(resp);
    }
    catch (const std::exception& e) {
        spdlog::error("backup: unexpected error: {}", e.what());
        finish_error(drogon::k500InternalServerError, "backup failed");
    }
}

} // namespace acadia::rest

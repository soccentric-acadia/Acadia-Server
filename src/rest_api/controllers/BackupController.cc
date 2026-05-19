#include "rest_api/controllers/BackupController.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace acadia::rest {

void BackupController::backup(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const char* db_path_env = std::getenv("ACADIA_DB_PATH");
    const char* db_path = db_path_env ? db_path_env : "acadia.db";

    // Read the SQLite database file as a binary stream.
    std::ifstream db_file(db_path, std::ios::binary | std::ios::ate);
    if (!db_file.is_open()) {
        Json::Value err; err["error"] = "could not open database file";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k500InternalServerError);
        cb(resp);
        return;
    }

    std::streamsize size = db_file.tellg();
    db_file.seekg(0, std::ios::beg);
    std::string dump(static_cast<std::size_t>(size), '\0');
    if (!db_file.read(&dump[0], size)) {
        Json::Value err; err["error"] = "failed to read database file";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k500InternalServerError);
        cb(resp);
        return;
    }

    // Build a timestamped filename.
    std::time_t now = std::time(nullptr);
    char ts_buf[32];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%dT%H%M%SZ", std::gmtime(&now));
    std::string filename = std::string("acadia-backup-") + ts_buf + ".db";

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeString("application/octet-stream");
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    resp->setBody(std::move(dump));
    cb(resp);
}

}  // namespace acadia::rest

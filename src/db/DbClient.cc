#include "db/DbClient.h"

#include "config/Settings.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <stdexcept>

namespace acadia::db {

namespace {
drogon::orm::DbClientPtr g_db_client;
}

void init_db_client(const DbSettings& cfg) {
    spdlog::info("db: opening SQLite database at '{}'", cfg.path);

    g_db_client = drogon::orm::DbClient::newSqlite3Client(
        "filename=" + cfg.path, 1);

    if (!g_db_client) {
        throw std::runtime_error("db: failed to create SQLite DbClient");
    }
}

drogon::orm::DbClientPtr get_db_client() {
    if (!g_db_client) {
        throw std::runtime_error("db: DbClient not initialised — call init_db_client() first");
    }
    return g_db_client;
}

}  // namespace acadia::db

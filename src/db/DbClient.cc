#include "db/DbClient.h"

#include "config/Settings.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace acadia::db
{

namespace
{
drogon::orm::DbClientPtr g_db_client;
std::string g_dsn;
std::once_flag g_migrations_client_once;
drogon::orm::DbClientPtr g_migrations_client;
}

std::string build_dsn(const DbSettings& cfg)
{
    // Drogon expects: "host=... port=... dbname=... user=... password=..."
    std::string dsn;
    dsn += "host=" + cfg.host;
    dsn += " port=" + std::to_string(cfg.port);
    dsn += " dbname=" + cfg.name;
    dsn += " user=" + cfg.user;
    if (!cfg.password.empty()) {
        dsn += " password=" + cfg.password;
    }
    dsn += " connect_timeout=" + std::to_string(cfg.connect_timeout);
    return dsn;
}

void init_db_client(const DbSettings& cfg)
{
    std::string dsn = build_dsn(cfg);
    spdlog::info("db: connecting to {}@{}:{}/{} pool={}", cfg.user, cfg.host, cfg.port, cfg.name, cfg.pool_size);

    g_db_client = drogon::orm::DbClient::newPgClient(dsn, static_cast<size_t>(cfg.pool_size));

    if (!g_db_client) {
        throw std::runtime_error("db: failed to create Drogon DbClient");
    }
    // Keep the DSN for the dedicated single-connection migrations client.
    g_dsn = dsn;
}

drogon::orm::DbClientPtr get_db_client()
{
    if (!g_db_client) {
        throw std::runtime_error("db: DbClient not initialised — call init_db_client() first");
    }
    return g_db_client;
}

drogon::orm::DbClientPtr get_migrations_client()
{
    // SRV-067: migrations need ONE dedicated connection — the advisory lock is
    // session-scoped and BEGIN/COMMIT must not straddle pooled connections.
    // Built lazily from the same DSN with a pool size of exactly 1, so every
    // statement the MigrationRunner issues runs on the same PostgreSQL session.
    std::call_once(g_migrations_client_once, [] {
        if (g_dsn.empty()) {
            throw std::runtime_error("db: migrations client requested before init_db_client()");
        }
        g_migrations_client = drogon::orm::DbClient::newPgClient(g_dsn, 1);
        if (!g_migrations_client) {
            throw std::runtime_error("db: failed to create dedicated migrations DbClient");
        }
        spdlog::info("db: dedicated single-connection migrations client created");
    });
    return g_migrations_client;
}

} // namespace acadia::db
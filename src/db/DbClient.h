#pragma once
// DbClient — thin wrapper around Drogon's async DbClient.
// Provides connection-pool configuration and a thread-safe accessor.

#include <drogon/orm/DbClient.h>
#include <memory>
#include <string>

namespace acadia
{
struct DbSettings;
}

namespace acadia::db
{

// Initialise the global Drogon DB client pool.  Call once before any
// repository is constructed.  Must be called from Drogon's main thread or
// before drogon::app().run().
void init_db_client(const DbSettings& cfg);

// Returns the shared Drogon DbClient.  Throws if init_db_client() was not
// called.
drogon::orm::DbClientPtr get_db_client();

// Dedicated single-connection client for session-scoped work (SRV-067:
// migrations). A pool of exactly ONE connection guarantees BEGIN/COMMIT and
// session advisory locks all execute on the same PostgreSQL session, which a
// pooled client cannot guarantee.
drogon::orm::DbClientPtr get_migrations_client();

// Build a PostgreSQL DSN from settings.
std::string build_dsn(const DbSettings& cfg);

} // namespace acadia::db

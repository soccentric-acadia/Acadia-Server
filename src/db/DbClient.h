#pragma once
// DbClient — thin wrapper around Drogon's SQLite DbClient.

#include <drogon/orm/DbClient.h>
#include <string>

namespace acadia {
struct DbSettings;
}

namespace acadia::db {

// Initialise the global SQLite DB client.  Call once before any
// repository is constructed.
void init_db_client(const DbSettings& cfg);

// Returns the shared Drogon DbClient.  Throws if init_db_client() was not called.
drogon::orm::DbClientPtr get_db_client();

}  // namespace acadia::db

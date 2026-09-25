#pragma once
// rest_api/controllers/BackupController.h — database backup (admin only)
#include "config/Settings.h"
#include "services/AuthService.h"
#include <drogon/HttpController.h>
#include <functional>
#include <string>

namespace acadia::rest
{

class BackupController : public drogon::HttpController<BackupController, false>
{
  public:
    // SRV-005: the DSN is taken from the configured DbSettings (the
    // ACADIA_DB_URL env var does not exist anywhere) and the dump target
    // directory from Settings::backup.
    BackupController(AuthService& auth_svc, DbSettings db_cfg, std::string backup_dir)
        : auth_svc_(auth_svc), db_cfg_(std::move(db_cfg)), backup_dir_(std::move(backup_dir)) {}

    METHOD_LIST_BEGIN
    // SRV-003: admin-only; the dump is written to disk on the server, never
    // streamed back to the caller.
    ADD_METHOD_TO(BackupController::backup, "/api/v1/backup", "acadia::rest::AuthFilter", drogon::Post);
    METHOD_LIST_END

    // POST /api/v1/backup — triggers pg_dump asynchronously on a worker
    // thread, writes the dump to the server's backup directory, and returns
    // JSON metadata (not the dump itself).
    void backup(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);

  private:
    // Runs the pg_dump (blocking) and answers the request. Invoked on a
    // dedicated worker thread so the Drogon IO loop is never blocked.
    void run_backup(std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    AuthService& auth_svc_;
    DbSettings db_cfg_;  // SRV-005: connection settings, never an env string
    std::string backup_dir_;
};

} // namespace acadia::rest

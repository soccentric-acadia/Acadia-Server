#pragma once
// rest_api/controllers/BackupController.h — database backup (admin only)
#include "services/AuthService.h"
#include <drogon/HttpController.h>

namespace acadia::rest
{

class BackupController : public drogon::HttpController<BackupController, false>
{
  public:
    explicit BackupController(AuthService& auth_svc) : auth_svc_(auth_svc) {}

    METHOD_LIST_BEGIN
    // SRV-003: admin-only; the dump is written to disk on the server, never
    // streamed back to the caller.
    ADD_METHOD_TO(BackupController::backup, "/api/v1/backup", "AuthFilter", drogon::Post);
    METHOD_LIST_END

    // POST /api/v1/backup — triggers pg_dump, writes the dump to the server's
    // backup directory, and returns JSON metadata (not the dump itself).
    void backup(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);

  private:
    AuthService& auth_svc_;
};

} // namespace acadia::rest
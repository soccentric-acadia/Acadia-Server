#pragma once
// rest_api/controllers/LogsController.h — log listing and SSE live tail
#include "services/LogService.h"
#include <drogon/HttpController.h>

namespace acadia::rest {

class LogsController : public drogon::HttpController<LogsController> {
public:
    explicit LogsController(LogService& svc);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(LogsController::list,      "/api/v1/runs/{run_id}/logs", drogon::Get);
    ADD_METHOD_TO(LogsController::tail,      "/api/v1/runs/{run_id}/logs/tail", drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr&,
              std::function<void(const drogon::HttpResponsePtr&)>&&,
              std::string run_id);

    // SSE live tail — streams log entries as they arrive.
    void tail(const drogon::HttpRequestPtr&,
              std::function<void(const drogon::HttpResponsePtr&)>&&,
              std::string run_id);

private:
    LogService& svc_;
};

}  // namespace acadia::rest

#pragma once
// rest_api/controllers/RunsController.h
#include "services/RunService.h"
#include <drogon/HttpController.h>

namespace acadia::rest {

class RunsController : public drogon::HttpController<RunsController, false> {
public:
    explicit RunsController(RunService& svc);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RunsController::list,    "/api/v1/runs",                  drogon::Get);
    ADD_METHOD_TO(RunsController::create,  "/api/v1/runs",                  drogon::Post);
    ADD_METHOD_TO(RunsController::get,     "/api/v1/runs/{id}",             drogon::Get);
    ADD_METHOD_TO(RunsController::cancel,  "/api/v1/runs/{id}/cancel",      drogon::Post);
    ADD_METHOD_TO(RunsController::pause,   "/api/v1/runs/{id}/pause",       drogon::Post);
    ADD_METHOD_TO(RunsController::resume,  "/api/v1/runs/{id}/resume",      drogon::Post);
    ADD_METHOD_TO(RunsController::results, "/api/v1/runs/{id}/results",     drogon::Get);
    ADD_METHOD_TO(RunsController::events,  "/api/v1/runs/{id}/events",      drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr&,
              std::function<void(const drogon::HttpResponsePtr&)>&&);
    void create(const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&&);
    void get(const drogon::HttpRequestPtr&,
             std::function<void(const drogon::HttpResponsePtr&)>&&,
             std::string id);
    void cancel(const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&&,
                std::string id);
    void pause(const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&&,
               std::string id);
    void resume(const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&&,
                std::string id);
    // GET /api/v1/runs/{id}/results — paginated result listing
    void results(const drogon::HttpRequestPtr&,
                 std::function<void(const drogon::HttpResponsePtr&)>&&,
                 std::string id);
    // GET /api/v1/runs/{id}/events — SSE stream of run state/result events
    void events(const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&&,
                std::string id);

private:
    RunService& svc_;
};

}  // namespace acadia::rest

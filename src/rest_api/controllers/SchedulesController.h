#pragma once
// rest_api/controllers/SchedulesController.h — CRUD for run schedules
#include "services/ScheduleService.h"
#include <drogon/HttpController.h>

namespace acadia::rest
{

class SchedulesController : public drogon::HttpController<SchedulesController, false>
{
  public:
    explicit SchedulesController(ScheduleService& svc);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SchedulesController::list, "/api/v1/schedules", "AuthFilter", drogon::Get);
    ADD_METHOD_TO(SchedulesController::create, "/api/v1/schedules", "AuthFilter", drogon::Post);
    ADD_METHOD_TO(SchedulesController::get, "/api/v1/schedules/{id}", "AuthFilter", drogon::Get);
    ADD_METHOD_TO(SchedulesController::update, "/api/v1/schedules/{id}", "AuthFilter", drogon::Put);
    ADD_METHOD_TO(SchedulesController::remove, "/api/v1/schedules/{id}", "AuthFilter", drogon::Delete);
    ADD_METHOD_TO(SchedulesController::disable, "/api/v1/schedules/{id}/disable", "AuthFilter", drogon::Post);
    ADD_METHOD_TO(SchedulesController::enable, "/api/v1/schedules/{id}/enable", "AuthFilter", drogon::Post);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void create(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void get(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void update(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void remove(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void disable(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void enable(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);

  private:
    ScheduleService& svc_;
};

} // namespace acadia::rest

#pragma once
// rest_api/controllers/OtaController.h
#include "services/OtaService.h"
#include <drogon/HttpController.h>

namespace acadia::rest
{

class OtaController : public drogon::HttpController<OtaController, false>
{
  public:
    explicit OtaController(OtaService& svc);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OtaController::list, "/api/v1/ota", "AuthFilter", drogon::Get);
    ADD_METHOD_TO(OtaController::get, "/api/v1/ota/{id}", "AuthFilter", drogon::Get);
    ADD_METHOD_TO(OtaController::upload, "/api/v1/ota/upload", "AuthFilter", drogon::Post);
    ADD_METHOD_TO(OtaController::trigger, "/api/v1/ota/{id}/trigger", "AuthFilter", drogon::Post);
    ADD_METHOD_TO(OtaController::rollback, "/api/v1/ota/{id}/rollback", "AuthFilter", drogon::Post);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void get(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void upload(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void trigger(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void rollback(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);

  private:
    OtaService& svc_;
};

} // namespace acadia::rest

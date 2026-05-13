#pragma once
// rest_api/controllers/HealthController.h
#include <drogon/HttpController.h>

namespace acadia::rest {

class HealthController : public drogon::HttpController<HealthController, false> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::get, "/api/v1/health", drogon::Get);
    METHOD_LIST_END

    void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

}  // namespace acadia::rest

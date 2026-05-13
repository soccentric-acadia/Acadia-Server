#include "rest_api/controllers/HealthController.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace acadia::rest {

void HealthController::get(const drogon::HttpRequestPtr& /*req*/,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    Json::Value resp;
    resp["status"] = "ok";
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

}  // namespace acadia::rest

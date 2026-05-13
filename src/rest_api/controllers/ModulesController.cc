#include "rest_api/controllers/ModulesController.h"
#include "util/Errors.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace acadia::rest {

ModulesController::ModulesController(ModuleService& svc) : svc_(svc) {}

static Json::Value mod_to_json(const ModuleInfo& m) {
    Json::Value v;
    v["id"]          = m.id;
    v["name"]        = m.name;
    v["description"] = m.description;
    v["version"]     = m.version;
    return v;
}

void ModulesController::list(const drogon::HttpRequestPtr& /*req*/,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    auto mods = svc_.list_all();
    Json::Value arr{Json::arrayValue};
    for (const auto& m : mods) arr.append(mod_to_json(m));
    Json::Value resp;
    resp["modules"] = arr;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void ModulesController::get(const drogon::HttpRequestPtr& /*req*/,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string id) {
    auto m = svc_.get(id);
    if (!m) {
        Json::Value err; err["error"] = "Not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
        return;
    }
    cb(drogon::HttpResponse::newHttpJsonResponse(mod_to_json(*m)));
}

}  // namespace acadia::rest

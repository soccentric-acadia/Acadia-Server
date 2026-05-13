#pragma once
// rest_api/controllers/ModulesController.h
#include "services/ModuleService.h"
#include <drogon/HttpController.h>

namespace acadia::rest {

class ModulesController : public drogon::HttpController<ModulesController, false> {
public:
    explicit ModulesController(ModuleService& svc);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ModulesController::list, "/api/v1/modules",      drogon::Get);
    ADD_METHOD_TO(ModulesController::get,  "/api/v1/modules/{id}", drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr&,
              std::function<void(const drogon::HttpResponsePtr&)>&&);
    void get(const drogon::HttpRequestPtr&,
             std::function<void(const drogon::HttpResponsePtr&)>&&,
             std::string id);

private:
    ModuleService& svc_;
};

}  // namespace acadia::rest

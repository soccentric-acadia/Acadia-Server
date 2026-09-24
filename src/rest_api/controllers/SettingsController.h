#pragma once
// rest_api/controllers/SettingsController.h — manages runtime-mutable server
// settings (data retention, etc.).  Backed by the app_settings DB table.

#include "rest_api/AccessControl.h"
#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

namespace acadia::rest
{

class SettingsController : public drogon::HttpController<SettingsController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SettingsController::get_retention, "/api/v1/settings/retention", "AuthFilter", drogon::Get);
    ADD_METHOD_TO(SettingsController::put_retention, "/api/v1/settings/retention", "AuthFilter", drogon::Put);
    METHOD_LIST_END

    void get_retention(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    void put_retention(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

} // namespace acadia::rest

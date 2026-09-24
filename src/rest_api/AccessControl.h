#pragma once
// rest_api/AccessControl.h — SRV-007: route-level role enforcement (RBAC).
//
// AuthFilter stamps `user_id` and `role` (admin/operator/viewer) into the
// request attributes.  Controllers that change system state call
// require_role() so that a viewer role is read-only:
//
//   admin    — everything (backup, OTA upload/trigger/rollback, settings)
//   operator — run dispatch, OTA trigger/rollback, schedules
//   viewer   — read-only
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <functional>
#include <initializer_list>
#include <string>

namespace acadia::rest
{

// Returns true when the request's authenticated role is one of `roles`.
// An unauthenticated request (no role attribute) is always rejected.
[[nodiscard]] inline bool has_role(const drogon::HttpRequestPtr& req, std::initializer_list<const char*> roles)
{
    auto role = req->getAttributes()->get<std::string>("role");
    for (const auto* r : roles) {
        if (role == r)
            return true;
    }
    return false;
}

// Returns true (and answers the request with 403) when the request's role is
// not permitted to mutate.  Intended for controller entry points.
inline bool forbidden(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                      std::initializer_list<const char*> roles)
{
    if (has_role(req, roles))
        return false;
    Json::Value err;
    err["error"] = "Forbidden: insufficient role";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
    resp->setStatusCode(drogon::k403Forbidden);
    cb(std::move(resp));
    return true;
}

} // namespace acadia::rest

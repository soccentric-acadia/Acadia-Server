#include "rest_api/filters/AuthFilter.h"
#include "util/Errors.h"

#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include <json/json.h>

namespace acadia::rest
{

AuthFilter::AuthFilter(AuthService& auth_svc) : auth_svc_(auth_svc)
{
}

void AuthFilter::doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& failCb, drogon::FilterChainCallback&& nextCb)
{
    // 1. Check Bearer token header.
    std::string auth_header = req->getHeader("Authorization");
    if (!auth_header.empty() && auth_header.rfind("Bearer ", 0) == 0) {
        std::string token = auth_header.substr(7);
        try {
            auto user_id_opt = auth_svc_.authenticate_token(token);
            if (user_id_opt) {
                auto user = auth_svc_.get_user(*user_id_opt);
                req->getAttributes()->insert("user_id", *user_id_opt);
                req->getAttributes()->insert("role", user ? user->role : std::string{"viewer"});
                nextCb();
                return;
            }
        }
        catch (const AuthError&) {
        }
    }

    // 2. Check session cookie (SRV-016: persistent DB-backed session) or the
    // in-process Drogon session, then fall through to 401.
    if (auto sc = req->getCookie(AuthService::kSessionCookie); !sc.empty()) {
        try {
            if (auto user_id = auth_svc_.validate_session(sc)) {
                auto user = auth_svc_.get_user(*user_id);
                req->getAttributes()->insert("user_id", *user_id);
                req->getAttributes()->insert("role", user ? user->role : std::string{"viewer"});
                nextCb();
                return;
            }
        }
        catch (const AuthError&) {
        }
    }

    // 3. In-process Drogon session (same-request convenience path).
    auto session = req->getSession();
    if (session) {
        std::string user_id = session->get<std::string>("user_id");
        if (!user_id.empty()) {
            req->getAttributes()->insert("user_id", user_id);
            req->getAttributes()->insert("role", session->get<std::string>("role"));
            nextCb();
            return;
        }
    }

    // Not authenticated.
    Json::Value err_body;
    err_body["error"] = "Unauthorized";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(err_body);
    resp->setStatusCode(drogon::k401Unauthorized);
    failCb(resp);
}

} // namespace acadia::rest

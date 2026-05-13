#include "rest_api/filters/AuthFilter.h"
#include "util/Errors.h"

#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

namespace acadia::rest {

AuthFilter::AuthFilter(AuthService& auth_svc) : auth_svc_(auth_svc) {}

void AuthFilter::doFilter(const drogon::HttpRequestPtr& req,
                           drogon::FilterCallback&&      failCb,
                           drogon::FilterChainCallback&& nextCb) {
    // 1. Check Bearer token header.
    std::string auth_header = req->getHeader("Authorization");
    if (!auth_header.empty() && auth_header.rfind("Bearer ", 0) == 0) {
        std::string token = auth_header.substr(7);
        try {
            auto tok = auth_svc_.authenticate_token(token);
            req->getAttributes()->insert("user_id", tok.user_id);
            req->getAttributes()->insert("token_id", tok.id);
            nextCb();
            return;
        } catch (const AuthError&) {}
    }

    // 2. Check session cookie.
    auto session = req->getSession();
    if (session) {
        std::string user_id = session->get<std::string>("user_id");
        if (!user_id.empty()) {
            req->getAttributes()->insert("user_id", user_id);
            nextCb();
            return;
        }
    }

    // Not authenticated.
    auto resp = drogon::HttpResponse::newHttpJsonResponse(
        Json::Value{"{\"error\":\"Unauthorized\"}"});
    resp->setStatusCode(drogon::k401Unauthorized);
    failCb(resp);
}

}  // namespace acadia::rest

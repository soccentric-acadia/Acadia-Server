#include "rest_api/controllers/AuthController.h"
#include "util/Errors.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <spdlog/spdlog.h>

#include <chrono>

namespace acadia::rest
{

static Json::Value err_json(const std::string& msg)
{
    Json::Value v;
    v["error"] = msg;
    return v;
}

void AuthController::login(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    auto body = req->getJsonObject();
    if (!body) {
        cb(drogon::HttpResponse::newHttpJsonResponse(err_json("invalid json")));
        return;
    }
    auto username = (*body)["username"].asString();
    auto password = (*body)["password"].asString();
    try {
        auto result = svc_.login(username, password, req->getPeerAddr().toIp());
        if (!result) {
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err_json("invalid credentials"));
            resp->setStatusCode(drogon::k401Unauthorized);
            cb(resp);
            return;
        }
        auto session = req->getSession();
        session->insert("user_id", result->user.id);
        session->insert("role", result->user.role);
        // SRV-016/017: persistent, DB-backed session presented as an
        // HttpOnly + SameSite cookie that survives restarts and works across
        // instances; Secure is set when the deployment serves HTTPS
        // (ACADIA_COOKIE_SECURE=1, e.g. behind a TLS reverse proxy).
        drogon::Cookie cookie{AuthService::kSessionCookie, result->session_token};
        cookie.setPath("/");
        cookie.setMaxAge(AuthService::kSessionLifetimeSec);
        cookie.setSameSite(drogon::Cookie::SameSite::kStrict);
        cookie.setHttpOnly(true);
        cookie.setSecure(cookie_secure_);
        Json::Value body_out;
        body_out["user_id"] = result->user.id;
        body_out["username"] = result->user.username;
        body_out["role"] = result->user.role;
        auto json_resp = drogon::HttpResponse::newHttpJsonResponse(body_out);
        json_resp->addCookie(std::move(cookie));
        cb(json_resp);
    }
    catch (const AuthError& e) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err_json(e.what()));
        resp->setStatusCode(drogon::k401Unauthorized);
        cb(resp);
    }
}

void AuthController::logout(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    // SRV-016: drop the persistent session server-side and clear the cookie.
    if (auto sc = req->getCookie(AuthService::kSessionCookie); !sc.empty()) {
        svc_.delete_session(sc);
    }
    req->getSession()->clear();
    Json::Value body_out;
    body_out["ok"] = true;
    auto json_resp = drogon::HttpResponse::newHttpJsonResponse(body_out);
    drogon::Cookie expired{AuthService::kSessionCookie, ""};
    expired.setPath("/");
    expired.setMaxAge(-1); // expired
    expired.setHttpOnly(true);
    expired.setSecure(cookie_secure_);
    json_resp->addCookie(std::move(expired));
    cb(json_resp);
}

void AuthController::create_token(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto body = req->getJsonObject();
    std::string label = body ? (*body)["label"].asString() : "default";
    // SRV-013: optional ttl_hours + scopes in the request body.
    std::optional<std::chrono::system_clock::time_point> exp;
    std::string scopes;
    if (body) {
        if ((*body).isMember("ttl_hours") && (*body)["ttl_hours"].asInt() > 0) {
            exp = std::chrono::system_clock::now() + std::chrono::hours{(*body)["ttl_hours"].asInt()};
        }
        scopes = (*body)["scopes"].asString();
    }
    auto [token_rec, token_str] = svc_.create_api_token(user_id, label, exp, scopes);
    Json::Value resp;
    resp["token"] = token_str; // only shown once
    resp["id"] = token_rec.id;
    resp["label"] = token_rec.name;
    resp["expires_at"] = token_rec.expires_at;
    resp["scopes"] = token_rec.scopes;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void AuthController::list_tokens(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto tokens = svc_.list_tokens(user_id);
    Json::Value arr{Json::arrayValue};
    for (const auto& t : tokens) {
        Json::Value v;
        v["id"] = t.id;
        v["label"] = t.name;
        arr.append(v);
    }
    Json::Value resp;
    resp["tokens"] = arr;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}


void AuthController::revoke_token(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id)
{
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    svc_.revoke_token(id, user_id);
    Json::Value resp;
    resp["ok"] = true;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void AuthController::change_password(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto body = req->getJsonObject();
    if (!body) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err_json("invalid json"));
        resp->setStatusCode(drogon::k400BadRequest);
        cb(resp);
        return;
    }
    std::string old_password = (*body)["current_password"].asString();
    std::string new_password = (*body)["new_password"].asString();
    try {
        svc_.change_password(user_id, old_password, new_password);
        Json::Value resp;
        resp["ok"] = true;
        cb(drogon::HttpResponse::newHttpJsonResponse(resp));
    }
    catch (const AuthError& e) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err_json(e.what()));
        resp->setStatusCode(drogon::k401Unauthorized);
        cb(resp);
    }
}

} // namespace acadia::rest

#pragma once
// rest_api/controllers/AuthController.h — login, logout, token management
#include "services/AuthService.h"
#include <drogon/HttpController.h>

namespace acadia::rest
{

class AuthController : public drogon::HttpController<AuthController, false>
{
  public:
    // cookie_secure (SRV-017): Secure flag on the session cookie when the
    // deployment serves HTTPS.
    AuthController(AuthService& svc, bool cookie_secure = false) : svc_(svc), cookie_secure_(cookie_secure) {}

    METHOD_LIST_BEGIN
    // SRV-001: login is the only public auth route; every other auth
    // operation requires an authenticated session/token.
    ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::logout, "/api/v1/auth/logout", "acadia::rest::AuthFilter", drogon::Post);
    ADD_METHOD_TO(AuthController::create_token, "/api/v1/auth/tokens", "acadia::rest::AuthFilter", drogon::Post);
    ADD_METHOD_TO(AuthController::list_tokens, "/api/v1/auth/tokens", "acadia::rest::AuthFilter", drogon::Get);
    ADD_METHOD_TO(AuthController::revoke_token, "/api/v1/auth/tokens/{id}", "acadia::rest::AuthFilter", drogon::Delete);
    ADD_METHOD_TO(AuthController::change_password, "/api/v1/auth/password", "acadia::rest::AuthFilter", drogon::Patch);
    METHOD_LIST_END

    void login(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void logout(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void create_token(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void list_tokens(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void revoke_token(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void change_password(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);

  private:
    AuthService& svc_;
    bool cookie_secure_ = false;
};

} // namespace acadia::rest

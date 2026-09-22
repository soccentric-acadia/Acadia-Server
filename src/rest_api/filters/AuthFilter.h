#pragma once
// rest_api/filters/AuthFilter.h — Drogon filter that validates session cookie or Bearer token.

#include "services/AuthService.h"
#include <drogon/HttpFilter.h>

namespace acadia::rest
{

// SRV-002: AutoCreation=false — Drogon cannot auto-instantiate this filter by
// name (it requires an AuthService&); it is registered explicitly via
// app().registerFilter() in DrogonAppFactory.cc.
class AuthFilter : public drogon::HttpFilter<AuthFilter, false>
{
  public:
    explicit AuthFilter(AuthService& auth_svc);

    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& failCb, drogon::FilterChainCallback&& nextCb) override;

  private:
    AuthService& auth_svc_;
};

} // namespace acadia::rest

#include "rest_api/DrogonAppFactory.h"
#include "rest_api/controllers/AlertsController.h"
#include "rest_api/controllers/AuthController.h"
#include "rest_api/controllers/BackupController.h"
#include "rest_api/controllers/ConfigsController.h"
#include "rest_api/controllers/HealthController.h"
#include "rest_api/controllers/LogsController.h"
#include "rest_api/controllers/ModulesController.h"
#include "rest_api/controllers/NotificationsController.h"
#include "rest_api/controllers/OtaController.h"
#include "rest_api/controllers/RunsController.h"
#include "rest_api/controllers/SchedulesController.h"
#include "rest_api/controllers/SettingsController.h"
#include "rest_api/controllers/TargetsController.h"
#include "rest_api/controllers/TemplatesController.h"
#include "rest_api/filters/AuthFilter.h"
#include "services/CompareService.h"

#include <cstdlib>
#include <spdlog/spdlog.h>

namespace acadia::rest
{

namespace
{
bool env_bool(const char* var, bool def)
{
    const char* v = std::getenv(var);
    if (!v || *v == '\0')
        return def;
    return std::string{v} == "true" || std::string{v} == "1" || std::string{v} == "yes";
}
} // namespace

void configure_drogon_app(const Settings& settings, AuthService& auth_svc, RunService& run_svc, LogService& log_svc, ModuleService& mod_svc,
                          TargetService& target_svc, ConfigService& config_svc, TemplateService& template_svc, ScheduleService& schedule_svc,
                          OtaService& ota_svc, NotificationService& notif_svc, CompareService& cmp_svc)
{
    auto& app = drogon::app();

    const auto& http = settings.network.http_listen;
    auto colon = http.rfind(':');
    std::string host = (colon != std::string::npos) ? http.substr(0, colon) : "0.0.0.0";
    int port = (colon != std::string::npos) ? std::stoi(http.substr(colon + 1)) : 8080;
    app.addListener(host, port);

    app.enableSession(3600, drogon::Cookie::SameSite::kStrict);

    // SRV-002: Drogon cannot auto-instantiate AuthFilter by name (it needs an
    // AuthService&). Register the instance here so every route that lists
    // "acadia::rest::AuthFilter" resolves against this instance.
    //
    // SRV-153: this used to register correctly but every route referenced
    // the filter by the *unqualified* string "AuthFilter". Drogon's
    // DrObject<T> registers a class under its demangled, fully-qualified
    // type name — "acadia::rest::AuthFilter" — not the bare class name, so
    // every route's filter lookup silently failed ("AuthFilter not found",
    // logged by Drogon itself, easy to miss among startup logs) and NO
    // route in this file actually enforced authentication at runtime,
    // despite SRV-001/SRV-002 being recorded as fixed and despite this
    // exact registerFilter() call being correct. Verified live: a fresh
    // server, no credentials, `curl .../api/v1/runs` returned 200. Fixed by
    // qualifying every route's filter string to match the registered name.
    app.registerFilter(std::make_shared<AuthFilter>(auth_svc));

    // SRV-017: Secure session cookies when the deployment fronts HTTPS.
    const bool secure_cookies = env_bool("ACADIA_COOKIE_SECURE", false);

    int threads = static_cast<int>(settings.concurrency.http_threads);
    app.setThreadNum(threads > 0 ? threads : 4);

    app.registerController(std::make_shared<HealthController>());
    app.registerController(std::make_shared<AuthController>(auth_svc, secure_cookies));
    app.registerController(std::make_shared<RunsController>(run_svc, config_svc));
    app.registerController(std::make_shared<LogsController>(log_svc));
    app.registerController(std::make_shared<ModulesController>(mod_svc, run_svc, cmp_svc));
    app.registerController(std::make_shared<TargetsController>(target_svc));
    app.registerController(std::make_shared<ConfigsController>(config_svc));
    app.registerController(std::make_shared<OtaController>(ota_svc));
    app.registerController(std::make_shared<NotificationsController>(notif_svc));
    app.registerController(std::make_shared<SchedulesController>(schedule_svc));
    app.registerController(std::make_shared<TemplatesController>(template_svc));
    app.registerController(std::make_shared<AlertsController>());
    app.registerController(std::make_shared<BackupController>(auth_svc, settings.db, settings.backup.dir));
    app.registerController(std::make_shared<SettingsController>());

    if (!settings.cors_origins.empty()) {
        spdlog::info("CORS configured for: {}", settings.cors_origins);
    }

    // DSH-003: the browser-based Dashboard authenticates with the Server's
    // session cookie (SRV-001 auth). Cross-origin fetches need CORS headers
    // with Allow-Credentials plus OPTIONS preflight handling.
    const auto& allowed = settings.cors_origin_list;
    auto add_cors = [&allowed](const drogon::HttpRequestPtr& req,
                               const drogon::HttpResponsePtr& resp) {
        const std::string origin = req->getHeader("Origin");
        if (origin.empty()) return;
        bool ok = false;
        for (const auto& a : allowed) {
            if (a == "*" || a == origin) { ok = true; break; }
        }
        if (!ok) return;
        resp->addHeader("Access-Control-Allow-Origin", origin);
        resp->addHeader("Access-Control-Allow-Credentials", "true");
        resp->addHeader("Vary", "Origin");
    };

    app.registerPreRoutingAdvice(
        [&allowed, add_cors](const drogon::HttpRequestPtr& req,
                             drogon::AdviceCallback&& ackCb,
                             drogon::AdviceChainCallback&& nextCb) {
            if (req->method() == drogon::Options) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
                resp->addHeader("Access-Control-Max-Age", "600");
                add_cors(req, resp);
                ackCb(resp);
                return;
            }
            nextCb();
        });
    app.registerPostHandlingAdvice(
        [add_cors](const drogon::HttpRequestPtr& req,
                   const drogon::HttpResponsePtr& resp) { add_cors(req, resp); });

    spdlog::info("Drogon configured: {}:{} threads={}", host, port, threads);
}

} // namespace acadia::rest

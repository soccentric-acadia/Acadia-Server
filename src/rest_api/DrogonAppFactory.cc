#include "rest_api/DrogonAppFactory.h"
#include "rest_api/filters/AuthFilter.h"
#include "rest_api/controllers/HealthController.h"
#include "rest_api/controllers/AuthController.h"
#include "rest_api/controllers/RunsController.h"
#include "rest_api/controllers/LogsController.h"
#include "rest_api/controllers/ModulesController.h"
#include "rest_api/controllers/TargetsController.h"
#include "rest_api/controllers/ConfigsController.h"
#include "rest_api/controllers/OtaController.h"
#include "rest_api/controllers/NotificationsController.h"

#include <spdlog/spdlog.h>

namespace acadia::rest {

void configure_drogon_app(const Settings&      settings,
                           AuthService&         auth_svc,
                           RunService&          run_svc,
                           LogService&          log_svc,
                           ModuleService&       mod_svc,
                           TargetService&       target_svc,
                           ConfigService&       config_svc,
                           TemplateService&     /*template_svc*/,
                           ScheduleService&     /*schedule_svc*/,
                           OtaService&          ota_svc,
                           NotificationService& notif_svc) {
    auto& app = drogon::app();

    // Listeners
    const auto& http = settings.network.http_listen;
    // Parse "host:port"
    auto colon = http.rfind(':');
    std::string host = (colon != std::string::npos) ? http.substr(0, colon) : "0.0.0.0";
    int port         = (colon != std::string::npos) ? std::stoi(http.substr(colon + 1)) : 8080;
    app.addListener(host, port);

    // Session support
    app.enableSession(3600, drogon::Cookie::SameSite::kStrict);

    // Thread count
    int threads = static_cast<int>(settings.concurrency.http_threads);
    app.setThreadNum(threads > 0 ? threads : 4);

    // Register controllers
    app.registerController(std::make_shared<HealthController>());
    app.registerController(std::make_shared<AuthController>(auth_svc));
    app.registerController(std::make_shared<RunsController>(run_svc));
    app.registerController(std::make_shared<LogsController>(log_svc));
    app.registerController(std::make_shared<ModulesController>(mod_svc));
    app.registerController(std::make_shared<TargetsController>(target_svc));
    app.registerController(std::make_shared<ConfigsController>(config_svc));
    app.registerController(std::make_shared<OtaController>(ota_svc));
    app.registerController(std::make_shared<NotificationsController>(notif_svc));

    // CORS origin header (simple pass-through — real CORS filter can be added)
    if (!settings.cors_origins.empty()) {
        spdlog::info("CORS configured for: {}", settings.cors_origins);
    }

    spdlog::info("Drogon configured: {}:{} threads={}", host, port, threads);
}

}  // namespace acadia::rest

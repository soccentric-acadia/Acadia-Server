// src/main.cpp — Acadia Server entry point.
// Starts gRPC + Drogon (HTTP/REST) servers after running DB migrations.

#include "config/Settings.h"
#include "db/DbClient.h"
#include "db/MigrationRunner.h"
#include "logging/LoggingSetup.h"
#include "registry/ModuleRegistry.h"

#include "services/AnomalyService.h"
#include "services/AuthService.h"
#include "services/CompareService.h"
#include "services/ConfigService.h"
#include "services/LogService.h"
#include "services/ModuleService.h"
#include "services/NotificationService.h"
#include "services/OtaService.h"
#include "services/RunService.h"
#include "services/ScheduleService.h"
#include "services/TargetService.h"
#include "services/TemplateService.h"
#include "services/ThresholdService.h"

#include "grpc_server/ClientServiceImpl.h"
#include "grpc_server/GrpcServerRunner.h"
#include "grpc_server/HalServiceImpl.h"
#include "repositories/BoardRepository.h"

#include "rest_api/DrogonAppFactory.h"

#include "orchestration/OtaOrchestrator.h"
#include "orchestration/RetentionScheduler.h"
#include "orchestration/RunDispatcher.h"
#include "orchestration/RunManager.h"
#include "orchestration/Scheduler.h"
#include "orchestration/TargetHeartbeat.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <thread>

// ------------------------------------------------------------------
// Signal handling
// ------------------------------------------------------------------
namespace
{
std::atomic<bool> g_shutdown{false};

void handle_signal(int /*sig*/)
{
    g_shutdown = true;
    drogon::app().quit();
}
} // namespace

int main()
{
    // --- Load settings from environment ---
    acadia::Settings settings;
    try {
        settings = acadia::load_settings();
    }
    catch (const std::exception& e) {
        spdlog::critical("Failed to load settings: {}", e.what());
        return 1;
    }

    // --- Logging ---
    acadia::logging::setup_logging(settings.log_level);
    spdlog::info("Acadia Server starting — {}", acadia::settings_summary(settings));

    // --- Database ---
    try {
        acadia::db::init_db_client(settings.db);
    }
    catch (const std::exception& e) {
        spdlog::critical("DB init failed: {}", e.what());
        return 1;
    }

    // Run migrations.
    acadia::db::MigrationRunner migrations{ACADIA_MIGRATIONS_DIR};
    try {
        migrations.run();
    }
    catch (const std::exception& e) {
        spdlog::critical("Migration failed: {}", e.what());
        return 1;
    }

    // --- Module Registry ---
    acadia::ModuleRegistry registry;
    if (!settings.registry_path.empty() && std::filesystem::exists(settings.registry_path)) {
        registry.load(settings.registry_path);
        spdlog::info("Module registry loaded: {} modules", registry.size());
    } else {
        spdlog::warn("No module registry found at '{}'", settings.registry_path);
    }

    // --- Services ---
    acadia::AuthService auth_svc{settings.auth};

    // PAR-007: bootstrap the initial admin so operators (and the E2E
    // pipeline) can log into an authenticated deployment. Optional: only when
    // both variables are provided and no users exist yet.
    {
        const char* admin_user = std::getenv("ACADIA_ADMIN_USERNAME");
        const char* admin_pass = std::getenv("ACADIA_ADMIN_PASSWORD");
        if (admin_user && *admin_user && admin_pass && *admin_pass) {
            try {
                auth_svc.ensure_admin(admin_user, admin_pass);
                spdlog::info("bootstrap admin user '{}' ensured", admin_user);
            } catch (const std::exception& e) {
                spdlog::error("bootstrap admin failed: {}", e.what());
            }
        }
    }

    acadia::RunService run_svc;
    acadia::LogService log_svc;
    acadia::ModuleService mod_svc{registry};
    acadia::TargetService target_svc;
    acadia::ConfigService config_svc;
    acadia::TemplateService template_svc;
    acadia::ScheduleService schedule_svc;
    acadia::OtaService ota_svc{settings.ota};
    acadia::NotificationService notif_svc{settings.smtp};
    acadia::CompareService compare_svc;

    // --- Orchestration ---
    // Task 85/97: the client registry is shared by ClientService (control
    // dispatch) and OTA orchestration (real board dispatch), so create it here
    // and pass the same instance to both.
    auto client_registry = std::make_shared<acadia::grpc::ClientRegistry>();
    acadia::orchestration::RunManager run_mgr{run_svc, log_svc, notif_svc};
    acadia::orchestration::Scheduler scheduler{schedule_svc, run_svc};
    acadia::orchestration::TargetHeartbeat heartbeat{target_svc};
    acadia::orchestration::OtaOrchestrator ota_orch{ota_svc, client_registry};
    acadia::RetentionScheduler retention{run_svc};

    // SRV-032/SRV-033: the run dispatcher sends queued runs to connected
    // boards (ConfigureRun/StartRun) and routes cancel/pause/resume control
    // messages to the dispatching board.
    acadia::orchestration::RunDispatcher run_dispatcher{run_svc, *client_registry};
    run_svc.set_run_completed_cb([&run_dispatcher](const std::string& run_id, const std::string& status) {
        run_dispatcher.on_run_completed(run_id, status);
    });
    run_svc.set_control_handler([&run_dispatcher](const std::string& action, const std::string& run_id) {
        run_dispatcher.send_control(action, run_id);
    });

    // Reconcile any runs that were left in 'running'/'queued' state from a previous crash.
    {
        int stale = run_svc.reconcile_stale_runs();
        if (stale > 0) {
            spdlog::warn("Reconciled {} stale run(s) to 'failed' on startup", stale);
        }
    }

    scheduler.start();
    heartbeat.start();
    ota_orch.start();
    retention.start();
    run_dispatcher.start();

    // --- gRPC Server (in background thread) ---
    acadia::BoardRepository board_repo; // Task 91: durable board lifecycle
    acadia::grpc::HalServiceImpl hal_service{run_svc, log_svc, mod_svc, settings};
    acadia::grpc::ClientServiceImpl client_service{run_svc, log_svc, client_registry, &board_repo, settings.auth.stream_token};
    acadia::grpc::GrpcServerRunner grpc_runner{hal_service, client_service, settings.network};

    std::thread grpc_thread([&] {
        try {
            grpc_runner.start();
        }
        catch (const std::exception& e) {
            spdlog::critical("gRPC server error: {}", e.what());
            g_shutdown = true;
            drogon::app().quit();
        }
    });

    // --- Signals ---
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // --- REST API (Drogon — blocks until quit()) ---
    acadia::rest::configure_drogon_app(settings, auth_svc, run_svc, log_svc, mod_svc, target_svc, config_svc, template_svc, schedule_svc, ota_svc, notif_svc,
                                       compare_svc);

    spdlog::info("Starting Drogon HTTP server on {}", settings.network.http_listen);
    drogon::app().run(); // blocks

    // --- Shutdown ---
    spdlog::info("Shutting down...");
    scheduler.stop();
    heartbeat.stop();
    retention.stop();
    ota_orch.stop();
    run_dispatcher.stop();
    grpc_runner.shutdown();
    if (grpc_thread.joinable())
        grpc_thread.join();

    spdlog::info("Acadia Server stopped");
    return 0;
}

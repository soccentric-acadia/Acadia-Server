#include "orchestration/RunManager.h"
#include <spdlog/spdlog.h>

namespace acadia::orchestration {

RunManager::RunManager(RunService&          run_svc,
                        LogService&          log_svc,
                        NotificationService& notif_svc)
    : run_svc_(run_svc), log_svc_(log_svc), notif_svc_(notif_svc) {}

void RunManager::on_run_completed(const std::string& run_id,
                                   const std::string& final_status) {
    spdlog::info("RunManager: run_id={} completed status={}", run_id, final_status);
    run_svc_.complete_run(run_id, final_status);

    if (final_status == "failed") {
        notif_svc_.emit("run.failed",
                        R"({"run_id":")" + run_id + "\"}");
    }
}

void RunManager::on_result(const std::string& run_id, const Result& result) {
    run_svc_.ingest_result(result);
    spdlog::debug("RunManager: ingested result run_id={} module_id={}", run_id,
                  result.module_id);
}

}  // namespace acadia::orchestration

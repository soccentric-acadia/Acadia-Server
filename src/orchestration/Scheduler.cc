#include "orchestration/Scheduler.h"
#include "util/Time.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace acadia::orchestration {

Scheduler::Scheduler(ScheduleService& schedule_svc, RunService& run_svc)
    : schedule_svc_(schedule_svc), run_svc_(run_svc) {}

Scheduler::~Scheduler() { stop(); }

void Scheduler::start() {
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    spdlog::info("Scheduler started");
}

void Scheduler::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void Scheduler::loop() {
    while (running_) {
        try {
            auto due = schedule_svc_.get_due_schedules();
            for (const auto& s : due) {
                spdlog::info("Scheduler: firing schedule id={} template_id={}",
                             s.id, s.template_id);
                // Start a new run referencing the configured template.
                auto run = run_svc_.start_run(s.template_id, "single_shot",
                                               "scheduled:" + s.id, "scheduler");
                (void)run;

                // Advance next_run_at using the schedule's own cron expression
                // (or a one-time schedule is disabled after firing).
                if (s.cron_expression.empty()) {
                    // One-time schedule: disable so it never fires again.
                    schedule_svc_.set_enabled(s.id, false, "scheduler");
                } else {
                    std::string next =
                        schedule_svc_.compute_next_run(s.cron_expression);
                    schedule_svc_.record_fired(s.id, next);
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Scheduler loop error: {}", e.what());
        }
        std::this_thread::sleep_for(std::chrono::seconds{30});
    }
}

}  // namespace acadia::orchestration

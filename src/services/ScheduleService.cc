#include "services/ScheduleService.h"
#include "util/Cron.h"
#include "util/Errors.h"
#include "util/Time.h"

#include <chrono>
#include <sstream>

namespace acadia {

Schedule ScheduleService::create(const Schedule& s, const std::string& actor_user_id) {
    Schedule copy = s;
    copy.created_by = actor_user_id;
    if (!copy.cron_expression.empty() && copy.next_run_at.empty()) {
        copy.next_run_at = compute_next_run(copy.cron_expression);
    }
    auto created = repo_.create(copy);
    audit_repo_.log(actor_user_id, "schedule.created", "schedule", created.id, "{}");
    return created;
}

Schedule ScheduleService::get(const std::string& id) {
    auto opt = repo_.find_by_id(id);
    if (!opt) throw NotFoundError("schedule " + id);
    return *opt;
}

std::vector<Schedule> ScheduleService::list_enabled() {
    return repo_.list_enabled();
}

Schedule ScheduleService::update(const Schedule& s, const std::string& actor_user_id) {
    repo_.update(s);
    audit_repo_.log(actor_user_id, "schedule.updated", "schedule", s.id, "{}");
    return get(s.id);
}

void ScheduleService::set_enabled(const std::string& id, bool enabled,
                                   const std::string& actor_user_id) {
    repo_.set_enabled(id, enabled);
    audit_repo_.log(actor_user_id,
                    enabled ? "schedule.enabled" : "schedule.disabled",
                    "schedule", id, "{}");
}

void ScheduleService::remove(const std::string& id, const std::string& actor_user_id) {
    repo_.remove(id);
    audit_repo_.log(actor_user_id, "schedule.deleted", "schedule", id, "{}");
}

std::vector<Schedule> ScheduleService::get_due_schedules() {
    return repo_.list_due(util::utc_now_iso8601());
}

void ScheduleService::record_fired(const std::string& id,
                                    const std::string& next_run_at) {
    repo_.record_run(id, next_run_at);
}

// Real cron next-run computation (5-field cron, UTC) via util/Cron.
std::string ScheduleService::compute_next_run(const std::string& cron_expr) {
    auto next = util::cron_next_occurrence(cron_expr, util::utc_now_iso8601());
    if (next.empty()) {
        // Invalid/unmatched cron: fall back to +1 minute so the schedule
        // remains observable rather than silently never firing.
        using namespace std::chrono;
        next = util::to_iso8601(system_clock::now() + minutes{1});
    }
    return next;
}

}  // namespace acadia

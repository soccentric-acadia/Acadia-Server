#include "repositories/ScheduleRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

namespace {
Schedule row_to_sched(const drogon::orm::Row& r) {
    return Schedule{
        r["id"].as<std::string>(),
        r["template_id"].as<std::string>(),
        r["cron_expression"].isNull() ? "" : r["cron_expression"].as<std::string>(),
        r["run_at"].isNull() ? "" : r["run_at"].as<std::string>(),
        r["enabled"].as<bool>(),
        r["last_run_at"].isNull() ? "" : r["last_run_at"].as<std::string>(),
        r["next_run_at"].isNull() ? "" : r["next_run_at"].as<std::string>(),
        r["created_at"].as<std::string>(),
        r["created_by"].isNull() ? "" : r["created_by"].as<std::string>()
    };
}
}  // namespace

Schedule ScheduleRepository::create(const Schedule& s) {
    auto db = db::get_db_client();
    auto new_id = util::generate_uuid();
    auto rows = db->execSqlSync(
        "INSERT INTO schedules (id, template_id, cron_expression, run_at, enabled, next_run_at, created_by)"
        " VALUES (?, ?, NULLIF(?,''), NULLIF(?,''), ?, NULLIF(?,''), ?)"
        " RETURNING id, template_id, cron_expression, run_at, enabled,"
        "  last_run_at, next_run_at, created_at, created_by",
        new_id.c_str(),
        s.template_id.c_str(), s.cron_expression.c_str(), s.run_at.c_str(),
        s.enabled,
        s.next_run_at.c_str(),
        s.created_by.empty() ? nullptr : s.created_by.c_str());
    return row_to_sched(rows[0]);
}

std::optional<Schedule> ScheduleRepository::find_by_id(const std::string& id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, template_id, cron_expression, run_at, enabled,"
        "  last_run_at, next_run_at, created_at, created_by"
        " FROM schedules WHERE id=?",
        id.c_str());
    if (rows.empty()) return std::nullopt;
    return row_to_sched(rows[0]);
}

std::vector<Schedule> ScheduleRepository::list_enabled() {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, template_id, cron_expression, run_at, enabled,"
        "  last_run_at, next_run_at, created_at, created_by"
        " FROM schedules WHERE enabled=1 ORDER BY created_at DESC");
    std::vector<Schedule> out;
    for (const auto& r : rows) out.push_back(row_to_sched(r));
    return out;
}

std::vector<Schedule> ScheduleRepository::list_due(const std::string& now_ts) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, template_id, cron_expression, run_at, enabled,"
        "  last_run_at, next_run_at, created_at, created_by"
        " FROM schedules WHERE enabled=1 AND next_run_at <= ?",
        now_ts.c_str());
    std::vector<Schedule> out;
    for (const auto& r : rows) out.push_back(row_to_sched(r));
    return out;
}

void ScheduleRepository::update(const Schedule& s) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE schedules SET cron_expression=NULLIF(?,''), run_at=NULLIF(?,'')"
        ", enabled=?, next_run_at=NULLIF(?,'') WHERE id=?",
        s.cron_expression.c_str(), s.run_at.c_str(), s.enabled,
        s.next_run_at.c_str(), s.id.c_str());
}

void ScheduleRepository::set_enabled(const std::string& id, bool enabled) {
    auto db = db::get_db_client();
    db->execSqlSync("UPDATE schedules SET enabled=? WHERE id=?", enabled, id.c_str());
}

void ScheduleRepository::record_run(const std::string& id,
                                    const std::string& next_run_at) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE schedules SET last_run_at=datetime('now'), next_run_at=NULLIF(?,'') WHERE id=?",
        next_run_at.c_str(), id.c_str());
}

void ScheduleRepository::remove(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync("DELETE FROM schedules WHERE id=?", id.c_str());
}

}  // namespace acadia

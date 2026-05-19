#include "repositories/NotificationRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

namespace {
Notification row_to_notif(const drogon::orm::Row& r) {
    return Notification{
        r["id"].as<std::string>(),
        r["kind"].as<std::string>(),
        r["payload"].as<std::string>(),
        r["read_at"].isNull() ? "" : r["read_at"].as<std::string>(),
        r["created_at"].as<std::string>()
    };
}
}  // namespace

Notification NotificationRepository::create(const std::string& kind,
                                              const std::string& payload_json) {
    auto db = db::get_db_client();
    auto new_id = util::generate_uuid();
    auto rows = db->execSqlSync(
        "INSERT INTO notifications (id, kind, payload)"
        " VALUES (?, ?, ?)"
        " RETURNING id, kind, payload, read_at, created_at",
        new_id.c_str(),
        kind.c_str(), payload_json.empty() ? "{}" : payload_json.c_str());
    return row_to_notif(rows[0]);
}

std::vector<Notification> NotificationRepository::list(const NotificationFilter& f) {
    auto db = db::get_db_client();
    std::string sql =
        "SELECT id, kind, payload, read_at, created_at"
        " FROM notifications";
    if (f.unread_only) sql += " WHERE read_at IS NULL";
    sql += " ORDER BY created_at DESC LIMIT " + std::to_string(f.limit)
         + " OFFSET " + std::to_string(f.offset);
    auto rows = db->execSqlSync(sql);
    std::vector<Notification> out;
    for (const auto& r : rows) out.push_back(row_to_notif(r));
    return out;
}

void NotificationRepository::mark_read(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE notifications SET read_at=datetime('now') WHERE id=?", id.c_str());
}

void NotificationRepository::mark_all_read() {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE notifications SET read_at=datetime('now') WHERE read_at IS NULL");
}

int64_t NotificationRepository::unread_count() {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT COUNT(*) FROM notifications WHERE read_at IS NULL");
    return rows[0][0].as<int64_t>();
}

}  // namespace acadia

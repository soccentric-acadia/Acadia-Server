#include "repositories/TargetRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"
#include "util/Errors.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

namespace {
Board row_to_board(const drogon::orm::Row& r) {
    return Board{
        r["id"].as<std::string>(),
        r["name"].as<std::string>(),
        r["hostname"].isNull() ? "" : r["hostname"].as<std::string>(),
        r["model"].isNull() ? "" : r["model"].as<std::string>(),
        r["os_version"].isNull() ? "" : r["os_version"].as<std::string>(),
        r["hal_version"].isNull() ? "" : r["hal_version"].as<std::string>(),
        r["client_version"].isNull() ? "" : r["client_version"].as<std::string>(),
        r["tags"].as<std::string>(),
        r["hal_module_inventory"].as<std::string>(),
        r["first_seen"].as<std::string>(),
        r["last_seen"].as<std::string>(),
        r["status"].as<std::string>()
    };
}
}  // namespace

std::optional<Board> TargetRepository::get() {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, name, hostname, model, os_version, hal_version, client_version,"
        "  tags, hal_module_inventory, first_seen, last_seen, status"
        " FROM board LIMIT 1");
    if (rows.empty()) return std::nullopt;
    return row_to_board(rows[0]);
}

Board TargetRepository::create(const Board& b) {
    auto db = db::get_db_client();
    try {
        auto new_id = util::generate_uuid();
        auto rows = db->execSqlSync(
            "INSERT INTO board (id, name, hostname, model, os_version, hal_version,"
            "  client_version, tags, hal_module_inventory, status)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            " RETURNING id, name, hostname, model, os_version, hal_version, client_version,"
            "  tags, hal_module_inventory, first_seen, last_seen, status",
            new_id.c_str(),
            b.name.c_str(), b.hostname.c_str(), b.model.c_str(),
            b.os_version.c_str(), b.hal_version.c_str(), b.client_version.c_str(),
            b.tags_json.empty() ? "{}" : b.tags_json.c_str(),
            b.hal_module_inventory_json.empty() ? "[]" : b.hal_module_inventory_json.c_str(),
            b.status.empty() ? "offline" : b.status.c_str());
        return row_to_board(rows[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        std::string what{e.base().what()};
        if (what.find("unique") != std::string::npos) {
            throw ConflictError("board already registered");
        }
        throw;
    }
}

void TargetRepository::update(const Board& b) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE board SET name=?, hostname=?, tags=? WHERE id=?",
        b.name.c_str(), b.hostname.c_str(), b.tags_json.c_str(), b.id.c_str());
}

void TargetRepository::heartbeat(const std::string& id, const std::string& status) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE board SET last_seen=datetime('now'), status=? WHERE id=?",
        status.c_str(), id.c_str());
}

void TargetRepository::update_inventory(const std::string& id,
                                        const std::string& inventory_json) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE board SET hal_module_inventory=? WHERE id=?",
        inventory_json.c_str(), id.c_str());
}

void TargetRepository::remove() {
    auto db = db::get_db_client();
    db->execSqlSync("DELETE FROM board");
}

}  // namespace acadia

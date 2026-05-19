#include "repositories/AuditRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

void AuditRepository::log(const std::string& user_id,
                           const std::string& action,
                           const std::string& target_type,
                           const std::string& target_id,
                           const std::string& metadata_json) {
    auto db = db::get_db_client();
    auto new_id = util::generate_uuid();
    db->execSqlSync(
        "INSERT INTO audit_log (id, user_id, action, target_type, target_id, metadata)"
        " VALUES (?, ?, ?, ?, ?, ?)",
        new_id.c_str(),
        user_id.empty() ? nullptr : user_id.c_str(),
        action.c_str(), target_type.c_str(), target_id.c_str(),
        metadata_json.empty() ? "{}" : metadata_json.c_str());
}

std::vector<AuditEntry> AuditRepository::list(const AuditFilter& f) {
    auto db = db::get_db_client();
    std::ostringstream sql;
    std::vector<std::string> params;

    sql << "SELECT id, user_id, action, target_type, target_id,"
           "  metadata, created_at"
           " FROM audit_log WHERE 1=1";

    if (!f.user_id.empty())     { sql << " AND user_id=?";     params.push_back(f.user_id); }
    if (!f.action.empty())      { sql << " AND action=?";      params.push_back(f.action); }
    if (!f.target_type.empty()) { sql << " AND target_type=?"; params.push_back(f.target_type); }
    if (!f.from_ts.empty())     { sql << " AND created_at>=?"; params.push_back(f.from_ts); }
    if (!f.to_ts.empty())       { sql << " AND created_at<=?"; params.push_back(f.to_ts); }

    sql << " ORDER BY created_at DESC LIMIT " << f.limit << " OFFSET " << f.offset;
    auto rows = [&]() -> drogon::orm::Result {
        switch (params.size()) {
            case 0: return db->execSqlSync(sql.str());
            case 1: return db->execSqlSync(sql.str(), params[0].c_str());
            case 2: return db->execSqlSync(sql.str(), params[0].c_str(), params[1].c_str());
            case 3: return db->execSqlSync(sql.str(), params[0].c_str(), params[1].c_str(), params[2].c_str());
            case 4: return db->execSqlSync(sql.str(), params[0].c_str(), params[1].c_str(), params[2].c_str(), params[3].c_str());
            default: return db->execSqlSync(sql.str(), params[0].c_str(), params[1].c_str(), params[2].c_str(), params[3].c_str(), params[4].c_str());
        }
    }();

    std::vector<AuditEntry> out;
    for (const auto& r : rows) {
        out.push_back({
            r["id"].as<std::string>(),
            r["user_id"].isNull() ? "" : r["user_id"].as<std::string>(),
            r["action"].as<std::string>(),
            r["target_type"].as<std::string>(),
            r["target_id"].as<std::string>(),
            r["metadata"].as<std::string>(),
            r["created_at"].as<std::string>()
        });
    }
    return out;
}

}  // namespace acadia

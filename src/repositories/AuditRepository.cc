#include "repositories/AuditRepository.h"
#include "db/DbClient.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

void AuditRepository::log(const std::string& user_id,
                           const std::string& action,
                           const std::string& target_type,
                           const std::string& target_id,
                           const std::string& metadata_json) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "INSERT INTO audit_log (user_id, action, target_type, target_id, metadata)"
        " VALUES ($1::UUID, $2, $3, $4, $5::JSONB)",
        user_id.empty() ? nullptr : user_id.c_str(),
        action, target_type, target_id,
        metadata_json.empty() ? "{}" : metadata_json);
}

std::vector<AuditEntry> AuditRepository::list(const AuditFilter& f) {
    auto db = db::get_db_client();
    std::ostringstream sql;
    std::vector<std::string> params;
    int p = 1;

    sql << "SELECT id, user_id::TEXT, action, target_type, target_id,"
           "  metadata::TEXT, created_at::TEXT"
           " FROM audit_log WHERE 1=1";

    if (!f.user_id.empty())     { sql << " AND user_id=$" << p++ << "::UUID"; params.push_back(f.user_id); }
    if (!f.action.empty())      { sql << " AND action=$"  << p++;             params.push_back(f.action); }
    if (!f.target_type.empty()) { sql << " AND target_type=$" << p++;         params.push_back(f.target_type); }
    if (!f.from_ts.empty())     { sql << " AND created_at>=$" << p++;         params.push_back(f.from_ts); }
    if (!f.to_ts.empty())       { sql << " AND created_at<=$" << p++;         params.push_back(f.to_ts); }

    sql << " ORDER BY created_at DESC LIMIT " << f.limit << " OFFSET " << f.offset;
    auto rows = [&]() -> drogon::orm::Result {
        switch (params.size()) {
            case 2: return db->execSqlSync(sql.str(), params[0], params[1]);
            case 3: return db->execSqlSync(sql.str(), params[0], params[1], params[2]);
            case 4: return db->execSqlSync(sql.str(), params[0], params[1], params[2], params[3]);
            case 5: return db->execSqlSync(sql.str(), params[0], params[1], params[2], params[3], params[4]);
            default: return db->execSqlSync(sql.str(), params[0], params[1]);
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

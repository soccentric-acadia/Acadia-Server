#include "repositories/OtaRepository.h"
#include "db/DbClient.h"

#include <drogon/orm/DbClient.h>

namespace acadia
{

namespace
{
OtaUpgrade row_to_ota(const drogon::orm::Row& r)
{
    OtaUpgrade u;
    u.id = r["id"].as<std::string>();
    u.board_id = r["board_id"].as<std::string>();
    u.artifact_path = r["artifact_path"].as<std::string>();
    u.artifact_hash = r["artifact_hash"].as<std::string>();
    u.artifact_size = r["artifact_size"].as<int64_t>();
    u.from_version = r["from_version"].as<std::string>();
    u.to_version = r["to_version"].as<std::string>();
    u.status = r["status"].as<std::string>();
    u.progress = r["progress"].as<int>();
    u.started_at = r["started_at"].isNull() ? "" : r["started_at"].as<std::string>();
    u.ended_at = r["ended_at"].isNull() ? "" : r["ended_at"].as<std::string>();
    u.error_message = r["error_message"].isNull() ? "" : r["error_message"].as<std::string>();
    u.rolled_back_from = r["rolled_back_from"].isNull() ? "" : r["rolled_back_from"].as<std::string>();
    u.created_at = r["created_at"].as<std::string>();
    u.created_by = r["created_by"].isNull() ? "" : r["created_by"].as<std::string>();
    return u;
}
} // namespace

OtaUpgrade OtaRepository::create(const OtaUpgrade& u)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync("INSERT INTO ota_upgrades (artifact_path, artifact_hash, artifact_size,"
                                "  from_version, to_version, status, board_id, created_by)"
                                " VALUES ($1, $2, $3, $4, $5, $6, $7, $8::UUID)"
                                " RETURNING id, board_id, artifact_path,  artifact_hash,  artifact_size, "
                                "  from_version,  to_version,  status,  progress, "
                                "  started_at::TEXT,  ended_at::TEXT,  error_message, "
                                "  rolled_back_from::TEXT,  created_at::TEXT,  created_by::TEXT",
                                u.artifact_path.c_str(), u.artifact_hash.c_str(), u.artifact_size, u.from_version.c_str(),
                                u.to_version.c_str(), u.status.empty() ? "uploaded" : u.status.c_str(), u.board_id.c_str(),
                                u.created_by.empty() ? nullptr : u.created_by.c_str());
    return row_to_ota(rows[0]);
}

std::optional<OtaUpgrade> OtaRepository::find_by_id(const std::string& id)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync("SELECT id,  board_id,  artifact_path,  artifact_hash,  artifact_size, "
                                "  from_version,  to_version,  status,  progress, "
                                "  started_at::TEXT,  ended_at::TEXT,  error_message, "
                                "  rolled_back_from::TEXT,  created_at::TEXT,  created_by::TEXT"
                                " FROM ota_upgrades WHERE id=$1::UUID",
                                id.c_str());
    if (rows.empty())
        return std::nullopt;
    return row_to_ota(rows[0]);
}

std::vector<OtaUpgrade> OtaRepository::list()
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync("SELECT id,  board_id,  artifact_path,  artifact_hash,  artifact_size, "
                                "  from_version,  to_version,  status,  progress, "
                                "  started_at::TEXT,  ended_at::TEXT,  error_message, "
                                "  rolled_back_from::TEXT,  created_at::TEXT,  created_by::TEXT"
                                " FROM ota_upgrades ORDER BY created_at DESC");
    std::vector<OtaUpgrade> out;
    for (const auto& r : rows)
        out.push_back(row_to_ota(r));
    return out;
}

void OtaRepository::update_status(const std::string& id, const std::string& status, int progress, const std::string& error)
{
    auto db = db::get_db_client();
    if (progress >= 0) {
        db->execSqlSync("UPDATE ota_upgrades SET status=$1,  progress=$2, "
                        "  error_message=NULLIF($3,'')"
                        " WHERE id=$4::UUID",
                        status.c_str(), progress, error.c_str(), id.c_str());
    } else {
        db->execSqlSync("UPDATE ota_upgrades SET status=$1,  error_message=NULLIF($2,'')"
                        " WHERE id=$3::UUID",
                        status.c_str(), error.c_str(), id.c_str());
    }
}

void OtaRepository::set_ended(const std::string& id, const std::string& status, const std::string& error)
{
    auto db = db::get_db_client();
    db->execSqlSync("UPDATE ota_upgrades SET status=$1,  ended_at=NOW(), "
                    "  error_message=NULLIF($2,'') WHERE id=$3::UUID",
                    status.c_str(), error.c_str(), id.c_str());
}

} // namespace acadia

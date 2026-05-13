#include "repositories/ResultRepository.h"
#include "db/DbClient.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

namespace {
Result row_to_result(const drogon::orm::Row& r) {
    Result res;
    res.id               = r["id"].as<std::string>();
    res.run_id           = r["run_id"].as<std::string>();
    res.module_id        = r["module_id"].as<std::string>();
    res.module_version   = r["module_version"].as<std::string>();
    res.timestamp        = r["timestamp"].as<std::string>();
    res.mode             = r["mode"].as<std::string>();
    res.status           = r["status"].as<std::string>();
    res.error_message    = r["error_message"].isNull() ? "" : r["error_message"].as<std::string>();
    res.payload_type_url = r["payload_type_url"].as<std::string>();
    if (!r["payload_bytes"].isNull()) {
        auto bytes = r["payload_bytes"].as<std::string>();
        res.payload_bytes.assign(bytes.begin(), bytes.end());
    }
    return res;
}
}  // namespace

Result ResultRepository::insert(const Result& r) {
    auto db = db::get_db_client();
    std::string payload_hex;
    if (!r.payload_bytes.empty()) {
        // Drogon handles binary via bytea literal
        payload_hex = std::string(r.payload_bytes.begin(), r.payload_bytes.end());
    }
    auto rows = db->execSqlSync(
        "INSERT INTO results (run_id, module_id, module_version, mode, status,"
        "  error_message, payload_bytes, payload_type_url)"
        " VALUES ($1::UUID, $2, $3, $4, $5, NULLIF($6,''), $7::BYTEA, $8)"
        " RETURNING id, run_id, module_id, module_version, timestamp::TEXT,"
        "  mode, status, error_message, payload_bytes, payload_type_url",
        r.run_id, r.module_id, r.module_version, r.mode, r.status,
        r.error_message, payload_hex, r.payload_type_url);
    return row_to_result(rows[0]);
}

std::optional<Result> ResultRepository::find_by_id(const std::string& id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, run_id, module_id, module_version, timestamp::TEXT,"
        "  mode, status, error_message, payload_bytes, payload_type_url"
        " FROM results WHERE id = $1::UUID",
        id);
    if (rows.empty()) return std::nullopt;
    return row_to_result(rows[0]);
}

std::vector<Result> ResultRepository::list(const ResultFilter& f) {
    auto db = db::get_db_client();
    std::ostringstream sql;
    std::vector<std::string> params;
    int p = 1;

    sql << "SELECT id, run_id, module_id, module_version, timestamp::TEXT,"
           "  mode, status, error_message, payload_bytes, payload_type_url"
           " FROM results WHERE run_id = $" << p++;
    params.push_back(f.run_id);

    if (!f.module_id.empty()) { sql << " AND module_id=$" << p++; params.push_back(f.module_id); }
    if (!f.status.empty())    { sql << " AND status=$"    << p++; params.push_back(f.status); }
    if (!f.since.empty())     { sql << " AND timestamp>=$"<< p++; params.push_back(f.since); }
    if (!f.until.empty())     { sql << " AND timestamp<=$"<< p++; params.push_back(f.until); }

    sql << " ORDER BY timestamp ASC LIMIT $" << p++ << " OFFSET $" << p++;
    params.push_back(std::to_string(f.limit));
    params.push_back(std::to_string(f.offset));

    drogon::orm::Result rows;
    switch (params.size()) {
        case 3: rows = db->execSqlSync(sql.str(), params[0], params[1], params[2]); break;
        case 4: rows = db->execSqlSync(sql.str(), params[0], params[1], params[2], params[3]); break;
        case 5: rows = db->execSqlSync(sql.str(), params[0], params[1], params[2], params[3], params[4]); break;
        default: rows = db->execSqlSync(sql.str(), params[0], params[1], params[2]); break;
    }

    std::vector<Result> out;
    for (const auto& r : rows) out.push_back(row_to_result(r));
    return out;
}

int64_t ResultRepository::count_for_run(const std::string& run_id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT COUNT(*) FROM results WHERE run_id=$1::UUID", run_id);
    return rows[0][0].as<int64_t>();
}

}  // namespace acadia

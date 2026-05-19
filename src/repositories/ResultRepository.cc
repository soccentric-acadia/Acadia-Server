#include "repositories/ResultRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"

#include <drogon/orm/DbClient.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <sstream>

namespace acadia {

namespace {

// ── Row helpers ──────────────────────────────────────────────────────────────

Result row_to_result(const drogon::orm::Row& r) {
    Result res;
    res.id               = r["id"].as<std::string>();
    res.run_id           = r["run_id"].as<std::string>();
    res.module_id        = r["module_id"].as<std::string>();
    res.module_version   = r["module_version"].as<std::string>();
    res.timestamp        = r["timestamp"].as<std::string>();
    res.mode             = r["mode"].as<std::string>();
    res.status           = r["status"].as<int16_t>();
    res.severity         = r["severity"].as<int16_t>();
    res.test_id          = r["test_id"].as<std::string>();
    res.test_name        = r["test_name"].as<std::string>();
    res.sequence         = r["sequence"].as<int64_t>();
    res.duration_ns      = r["duration_ns"].as<int64_t>();
    res.error_message    = r["error_message"].isNull() ? ""
                           : r["error_message"].as<std::string>();
    res.reason_json      = r["reason"].as<std::string>();
    res.metrics_json     = r["metrics"].as<std::string>();
    res.monitoring_json  = r["monitoring"].as<std::string>();
    res.event_json       = r["event"].as<std::string>();
    if (!r["parent_id"].isNull())
        res.parent_id = r["parent_id"].as<std::string>();
    res.payload_type_url = r["payload_type_url"].as<std::string>();
    if (!r["payload_bytes"].isNull()) {
        auto bytes = r["payload_bytes"].as<std::string>();
        res.payload_bytes.assign(bytes.begin(), bytes.end());
    }
    return res;
}

static const char* SELECT_COLS =
    "id, run_id, module_id, module_version, timestamp,"
    " mode, status, severity, test_id, test_name, sequence, duration_ns,"
    " error_message, reason, metrics, monitoring, event,"
    " parent_id, payload_bytes, payload_type_url";

}  // namespace

// ── insert ───────────────────────────────────────────────────────────────────

Result ResultRepository::insert(const Result& r) {
    auto db = db::get_db_client();

    std::string payload_hex;
    if (!r.payload_bytes.empty())
        payload_hex = std::string(r.payload_bytes.begin(), r.payload_bytes.end());

    std::string parent_id_val = r.parent_id.value_or("");
    auto new_id = util::generate_uuid();

    auto rows = db->execSqlSync(
        "INSERT INTO results"
        " (id, run_id, module_id, module_version, mode,"
        "  status, severity, test_id, test_name, sequence, duration_ns,"
        "  error_message, reason, metrics, monitoring, event,"
        "  parent_id, payload_bytes, payload_type_url)"
        " VALUES"
        " (?, ?, ?, ?, ?,"
        "  ?, ?, ?, ?, ?, ?,"
        "  NULLIF(?,''), ?, ?, ?, ?,"
        "  NULLIF(?,''), NULLIF(?,''), ?)"
        " RETURNING " + std::string{SELECT_COLS},
        new_id.c_str(),
        r.run_id.c_str(),
        r.module_id.c_str(),
        r.module_version.c_str(),
        r.mode.c_str(),
        r.status,
        r.severity,
        r.test_id.c_str(),
        r.test_name.c_str(),
        r.sequence,
        r.duration_ns,
        r.error_message.c_str(),
        r.reason_json.empty()     ? "{}" : r.reason_json.c_str(),
        r.metrics_json.empty()    ? "{}" : r.metrics_json.c_str(),
        r.monitoring_json.empty() ? "{}" : r.monitoring_json.c_str(),
        r.event_json.empty()      ? "{}" : r.event_json.c_str(),
        parent_id_val.c_str(),
        payload_hex.c_str(),
        r.payload_type_url.c_str());

    Result saved = row_to_result(rows[0]);

    // Insert subtests recursively, referencing the just-inserted parent id.
    for (const auto& sub : r.subtests) {
        Result child = sub;
        child.parent_id = saved.id;
        // Propagate run-level fields if the subtest doesn't carry them.
        if (child.run_id.empty())         child.run_id         = saved.run_id;
        if (child.module_id.empty())      child.module_id      = saved.module_id;
        if (child.module_version.empty()) child.module_version = saved.module_version;
        if (child.mode.empty())           child.mode           = saved.mode;
        insert(child);   // recursive
    }

    // Keep the in-memory subtests populated so callers can inspect them.
    saved.subtests = r.subtests;

    upsert_run_summary(saved.run_id, saved.module_id, saved.test_id,
                       saved.status, saved.severity, saved.timestamp);

    return saved;
}

// ── find_by_id ───────────────────────────────────────────────────────────────

std::optional<Result> ResultRepository::find_by_id(const std::string& id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT " + std::string{SELECT_COLS} +
        " FROM results WHERE id = ?",
        id.c_str());
    if (rows.empty()) return std::nullopt;
    return row_to_result(rows[0]);
}

// ── list ─────────────────────────────────────────────────────────────────────

std::vector<Result> ResultRepository::list(const ResultFilter& f) {
    auto db = db::get_db_client();
    std::ostringstream sql;
    std::vector<std::string> params;

    sql << "SELECT " << SELECT_COLS
        << " FROM results WHERE run_id = ?";
    params.push_back(f.run_id);

    if (!f.module_id.empty()) {
        sql << " AND module_id = ?";
        params.push_back(f.module_id);
    }
    if (!f.test_id.empty()) {
        sql << " AND test_id = ?";
        params.push_back(f.test_id);
    }
    if (f.top_level_only) {
        sql << " AND parent_id IS NULL";
    }
    if (!f.statuses.empty()) {
        sql << " AND status IN (";
        for (std::size_t i = 0; i < f.statuses.size(); ++i) {
            if (i) sql << ",";
            sql << f.statuses[i];
        }
        sql << ")";
    }
    if (!f.severities.empty()) {
        sql << " AND severity IN (";
        for (std::size_t i = 0; i < f.severities.size(); ++i) {
            if (i) sql << ",";
            sql << f.severities[i];
        }
        sql << ")";
    }
    if (!f.since.empty()) {
        sql << " AND timestamp >= ?";
        params.push_back(f.since);
    }
    if (!f.until.empty()) {
        sql << " AND timestamp <= ?";
        params.push_back(f.until);
    }

    sql << " ORDER BY timestamp ASC LIMIT " << f.limit
        << " OFFSET " << f.offset;

    const std::string query = sql.str();

    auto call = [&]() -> drogon::orm::Result {
        switch (params.size()) {
        case 1: return db->execSqlSync(query, params[0].c_str());
        case 2: return db->execSqlSync(query, params[0].c_str(), params[1].c_str());
        case 3: return db->execSqlSync(query, params[0].c_str(), params[1].c_str(), params[2].c_str());
        case 4: return db->execSqlSync(query, params[0].c_str(), params[1].c_str(), params[2].c_str(), params[3].c_str());
        case 5: return db->execSqlSync(query, params[0].c_str(), params[1].c_str(), params[2].c_str(), params[3].c_str(), params[4].c_str());
        default: return db->execSqlSync(query, params[0].c_str(), params[1].c_str(), params[2].c_str(), params[3].c_str(), params[4].c_str(), params[5].c_str());
        }
    };
    auto db_rows = call();

    std::vector<Result> out;
    out.reserve(db_rows.size());
    for (const auto& row : db_rows) out.push_back(row_to_result(row));
    return out;
}

// ── count_for_run ─────────────────────────────────────────────────────────────

int64_t ResultRepository::count_for_run(const std::string& run_id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT COUNT(*) FROM results WHERE run_id = ? AND parent_id IS NULL",
        run_id.c_str());
    return rows[0][0].as<int64_t>();
}

// ── find_last_per_module ──────────────────────────────────────────────────────

std::vector<RunModuleSummary> ResultRepository::find_last_per_module(
        const std::string& run_id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT module_id, test_id, last_status, last_severity,"
        "       last_timestamp, result_count"
        " FROM run_summaries WHERE run_id = ?"
        " ORDER BY module_id, test_id",
        run_id.c_str());

    std::vector<RunModuleSummary> out;
    out.reserve(rows.size());
    for (const auto& r : rows) {
        RunModuleSummary s;
        s.module_id      = r["module_id"].as<std::string>();
        s.test_id        = r["test_id"].as<std::string>();
        s.last_status    = r["last_status"].as<int16_t>();
        s.last_severity  = r["last_severity"].as<int16_t>();
        s.last_timestamp = r["last_timestamp"].as<std::string>();
        s.result_count   = r["result_count"].as<int64_t>();
        out.push_back(s);
    }
    return out;
}

// ── find_module_trend ─────────────────────────────────────────────────────────

std::vector<TrendPoint> ResultRepository::find_module_trend(
        const std::string& module_id,
        const std::string& test_id,
        const std::string& field,
        const std::string& from_ts,
        const std::string& to_ts) {
    auto db = db::get_db_client();
    // Extract numeric value from metrics JSON: { field: { "type": "T", "value": V } }
    // ? params in order: field (SELECT), module_id, test_id, from_ts, to_ts,
    //                    field (json_type check), field (type check)
    auto rows = db->execSqlSync(
        "SELECT timestamp,"
        "       CAST(json_extract(metrics, '$.' || ? || '.value') AS REAL) AS val,"
        "       status, severity"
        " FROM results"
        " WHERE module_id = ?"
        "   AND test_id   = ?"
        "   AND parent_id IS NULL"
        "   AND timestamp >= ?"
        "   AND timestamp <= ?"
        "   AND json_type(metrics, '$.' || ?) IS NOT NULL"
        "   AND json_extract(metrics, '$.' || ? || '.type') IN ('double','int')"
        " ORDER BY timestamp ASC",
        field.c_str(),
        module_id.c_str(),
        test_id.c_str(),
        from_ts.c_str(),
        to_ts.c_str(),
        field.c_str(),
        field.c_str());

    std::vector<TrendPoint> out;
    out.reserve(rows.size());
    for (const auto& r : rows) {
        TrendPoint tp;
        tp.timestamp = r["timestamp"].as<std::string>();
        tp.value     = r["val"].as<double>();
        tp.status    = r["status"].as<int16_t>();
        tp.severity  = r["severity"].as<int16_t>();
        out.push_back(tp);
    }
    return out;
}

// ── find_distribution ────────────────────────────────────────────────────────

std::vector<double> ResultRepository::find_distribution(
        const std::string& module_id,
        const std::string& test_id,
        const std::string& field,
        const std::vector<std::string>& run_ids) {
    if (run_ids.empty()) return {};
    auto db = db::get_db_client();

    // Validate run_ids before embedding in SQL to prevent injection.
    for (const auto& rid : run_ids) {
        if (!util::is_valid_uuid(rid))
            throw std::invalid_argument("find_distribution: invalid run_id");
    }

    // Build IN clause with validated UUID literals.
    std::ostringstream in_clause;
    for (std::size_t i = 0; i < run_ids.size(); ++i) {
        if (i) in_clause << ",";
        in_clause << "'" << run_ids[i] << "'";
    }

    // ? params in order: field (SELECT), module_id, test_id,
    //                    field (json_type check), field (type check)
    std::string sql =
        "SELECT CAST(json_extract(metrics, '$.' || ? || '.value') AS REAL) AS val"
        " FROM results"
        " WHERE module_id = ?"
        "   AND test_id   = ?"
        "   AND parent_id IS NULL"
        "   AND run_id IN (" + in_clause.str() + ")"
        "   AND json_type(metrics, '$.' || ?) IS NOT NULL"
        "   AND json_extract(metrics, '$.' || ? || '.type') IN ('double','int')"
        " ORDER BY timestamp ASC";

    auto rows = db->execSqlSync(sql,
        field.c_str(), module_id.c_str(), test_id.c_str(),
        field.c_str(), field.c_str());

    std::vector<double> out;
    out.reserve(rows.size());
    for (const auto& r : rows)
        out.push_back(r["val"].as<double>());
    return out;
}

// ── find_for_compare ──────────────────────────────────────────────────────────

std::vector<CompareEntry> ResultRepository::find_for_compare(
        const std::string& run_a,
        const std::string& run_b) {
    auto db = db::get_db_client();
    // Emulate FULL OUTER JOIN with LEFT JOIN UNION for SQLite.
    // ? params in order: run_a, run_b, run_b, run_a
    auto rows = db->execSqlSync(
        "SELECT COALESCE(a.module_id, b.module_id) AS module_id,"
        "       COALESCE(a.test_id,   b.test_id)   AS test_id,"
        "       a.last_status   AS run_a_status,"
        "       b.last_status   AS run_b_status,"
        "       COALESCE(a.last_severity, 0) AS run_a_severity,"
        "       COALESCE(b.last_severity, 0) AS run_b_severity"
        " FROM (SELECT module_id, test_id, last_status, last_severity"
        "       FROM run_summaries WHERE run_id = ?) a"
        " LEFT JOIN (SELECT module_id, test_id, last_status, last_severity"
        "            FROM run_summaries WHERE run_id = ?) b"
        " ON b.module_id = a.module_id AND b.test_id = a.test_id"
        " UNION"
        " SELECT b2.module_id, b2.test_id,"
        "        NULL AS run_a_status, b2.last_status AS run_b_status,"
        "        0 AS run_a_severity, COALESCE(b2.last_severity, 0) AS run_b_severity"
        " FROM (SELECT module_id, test_id, last_status, last_severity"
        "       FROM run_summaries WHERE run_id = ?) b2"
        " WHERE NOT EXISTS ("
        "   SELECT 1 FROM run_summaries a2"
        "   WHERE a2.run_id = ? AND a2.module_id = b2.module_id AND a2.test_id = b2.test_id)"
        " ORDER BY module_id, test_id",
        run_a.c_str(), run_b.c_str(), run_b.c_str(), run_a.c_str());

    std::vector<CompareEntry> out;
    out.reserve(rows.size());
    for (const auto& r : rows) {
        CompareEntry e;
        e.module_id      = r["module_id"].as<std::string>();
        e.test_id        = r["test_id"].as<std::string>();
        // NULL means the test did not appear in that run.
        if (!r["run_a_status"].isNull())
            e.run_a_status = r["run_a_status"].as<int16_t>();
        if (!r["run_b_status"].isNull())
            e.run_b_status = r["run_b_status"].as<int16_t>();
        e.run_a_severity = r["run_a_severity"].as<int16_t>();
        e.run_b_severity = r["run_b_severity"].as<int16_t>();
        out.push_back(e);
    }
    return out;
}

// ── get_subtests ──────────────────────────────────────────────────────────────

std::vector<Result> ResultRepository::get_subtests(const std::string& parent_id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT " + std::string{SELECT_COLS} +
        " FROM results WHERE parent_id = ? ORDER BY sequence ASC",
        parent_id.c_str());

    std::vector<Result> out;
    out.reserve(rows.size());
    for (const auto& r : rows) out.push_back(row_to_result(r));
    return out;
}

// ── upsert_run_summary ────────────────────────────────────────────────────────

void ResultRepository::upsert_run_summary(const std::string& run_id,
                                            const std::string& module_id,
                                            const std::string& test_id,
                                            int16_t status,
                                            int16_t severity,
                                            const std::string& timestamp) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "INSERT INTO run_summaries"
        " (run_id, module_id, test_id, last_status, last_severity, last_timestamp, result_count)"
        " VALUES (?, ?, ?, ?, ?, ?, 1)"
        " ON CONFLICT (run_id, module_id, test_id) DO UPDATE SET"
        "   last_status    = excluded.last_status,"
        "   last_severity  = excluded.last_severity,"
        "   last_timestamp = excluded.last_timestamp,"
        "   result_count   = run_summaries.result_count + 1",
        run_id.c_str(),
        module_id.c_str(),
        test_id.c_str(),
        status,
        severity,
        timestamp.c_str());
}

}  // namespace acadia

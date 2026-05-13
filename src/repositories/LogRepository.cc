#include "repositories/LogRepository.h"
#include "db/DbClient.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

namespace {
LogEntry row_to_log(const drogon::orm::Row& r) {
    return LogEntry{
        r["id"].as<std::string>(),
        r["source"].as<std::string>(),
        r["severity"].as<std::string>(),
        r["timestamp"].as<std::string>(),
        r["message"].as<std::string>(),
        r["metadata"].as<std::string>(),
        r["run_id"].isNull() ? "" : r["run_id"].as<std::string>()
    };
}
}  // namespace

LogEntry LogRepository::insert(const LogEntry& e) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "INSERT INTO log_entries (source, severity, message, metadata, run_id)"
        " VALUES ($1, $2, $3, $4::JSONB, $5::UUID)"
        " RETURNING id, source, severity, timestamp::TEXT, message, metadata::TEXT,"
        "  run_id::TEXT",
        e.source, e.severity, e.message,
        e.metadata_json.empty() ? "{}" : e.metadata_json,
        e.run_id.empty() ? nullptr : e.run_id.c_str());
    return row_to_log(rows[0]);
}

std::vector<LogEntry> LogRepository::list(const LogFilter& f) {
    auto db = db::get_db_client();
    std::ostringstream sql;
    std::vector<std::string> params;
    int p = 1;
    sql << "SELECT id, source, severity, timestamp::TEXT, message, metadata::TEXT,"
           "  run_id::TEXT FROM log_entries WHERE 1=1";

    if (!f.source.empty())   { sql << " AND source=$"   << p++; params.push_back(f.source); }
    if (!f.severity.empty()) { sql << " AND severity=$" << p++; params.push_back(f.severity); }
    if (!f.from_ts.empty())  { sql << " AND timestamp>=$"<< p++; params.push_back(f.from_ts); }
    if (!f.to_ts.empty())    { sql << " AND timestamp<=$"<< p++; params.push_back(f.to_ts); }
    if (!f.run_id.empty())   { sql << " AND run_id=$"   << p++ << "::UUID"; params.push_back(f.run_id); }
    if (!f.query.empty())    {
        sql << " AND to_tsvector('english', message) @@ plainto_tsquery('english', $" << p++ << ")";
        params.push_back(f.query);
    }

    sql << " ORDER BY timestamp DESC LIMIT $" << p++ << " OFFSET $" << p++;
    params.push_back(std::to_string(f.limit));
    params.push_back(std::to_string(f.offset));

    drogon::orm::Result rows;
    switch (params.size()) {
        case 2: rows = db->execSqlSync(sql.str(), params[0], params[1]); break;
        case 3: rows = db->execSqlSync(sql.str(), params[0], params[1], params[2]); break;
        case 4: rows = db->execSqlSync(sql.str(), params[0], params[1], params[2], params[3]); break;
        case 5: rows = db->execSqlSync(sql.str(), params[0], params[1], params[2], params[3], params[4]); break;
        default: rows = db->execSqlSync(sql.str(), params[0], params[1]); break;
    }

    std::vector<LogEntry> out;
    for (const auto& r : rows) out.push_back(row_to_log(r));
    return out;
}

int64_t LogRepository::count(const LogFilter& f) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT COUNT(*) FROM log_entries WHERE 1=1");
    return rows[0][0].as<int64_t>();
}

LogBookmark LogRepository::create_bookmark(const std::string& user_id,
                                            const std::string& log_id,
                                            const std::string& note) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "INSERT INTO log_bookmarks (user_id, log_id, note)"
        " VALUES ($1::UUID, $2::UUID, $3)"
        " RETURNING id, user_id::TEXT, log_id::TEXT, note, created_at::TEXT",
        user_id, log_id, note);
    const auto& r = rows[0];
    return LogBookmark{
        r["id"].as<std::string>(),
        r["user_id"].as<std::string>(),
        r["log_id"].as<std::string>(),
        r["note"].as<std::string>(),
        r["created_at"].as<std::string>()
    };
}

std::vector<LogBookmark> LogRepository::list_bookmarks(const std::string& user_id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, user_id::TEXT, log_id::TEXT, note, created_at::TEXT"
        " FROM log_bookmarks WHERE user_id=$1::UUID ORDER BY created_at DESC",
        user_id);
    std::vector<LogBookmark> out;
    for (const auto& r : rows) {
        out.push_back({
            r["id"].as<std::string>(),
            r["user_id"].as<std::string>(),
            r["log_id"].as<std::string>(),
            r["note"].as<std::string>(),
            r["created_at"].as<std::string>()
        });
    }
    return out;
}

void LogRepository::remove_bookmark(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync("DELETE FROM log_bookmarks WHERE id=$1::UUID", id);
}

SavedLogView LogRepository::create_view(const std::string& user_id,
                                         const std::string& name,
                                         const std::string& filters_json) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "INSERT INTO saved_log_views (user_id, name, filters)"
        " VALUES ($1::UUID, $2, $3::JSONB)"
        " RETURNING id, user_id::TEXT, name, filters::TEXT, created_at::TEXT",
        user_id, name, filters_json.empty() ? "{}" : filters_json);
    const auto& r = rows[0];
    return SavedLogView{
        r["id"].as<std::string>(),
        r["user_id"].as<std::string>(),
        r["name"].as<std::string>(),
        r["filters"].as<std::string>(),
        r["created_at"].as<std::string>()
    };
}

std::vector<SavedLogView> LogRepository::list_views(const std::string& user_id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, user_id::TEXT, name, filters::TEXT, created_at::TEXT"
        " FROM saved_log_views WHERE user_id=$1::UUID ORDER BY created_at DESC",
        user_id);
    std::vector<SavedLogView> out;
    for (const auto& r : rows) {
        out.push_back({
            r["id"].as<std::string>(),
            r["user_id"].as<std::string>(),
            r["name"].as<std::string>(),
            r["filters"].as<std::string>(),
            r["created_at"].as<std::string>()
        });
    }
    return out;
}

}  // namespace acadia

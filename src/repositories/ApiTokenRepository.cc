#include "repositories/ApiTokenRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

ApiToken ApiTokenRepository::create(const std::string& user_id,
                                    const std::string& name,
                                    const std::string& token_hash) {
    auto db = db::get_db_client();
    auto new_id = util::generate_uuid();
    auto rows = db->execSqlSync(
        "INSERT INTO api_tokens (id, user_id, name, token_hash)"
        " VALUES (?, ?, ?, ?)"
        " RETURNING id, user_id, name, token_hash,"
        "   last_used_at, created_at, revoked_at",
        new_id.c_str(),
        user_id.c_str(), name.c_str(), token_hash.c_str());
    const auto& r = rows[0];
    return ApiToken{
        r["id"].as<std::string>(),
        r["user_id"].as<std::string>(),
        r["name"].as<std::string>(),
        r["token_hash"].as<std::string>(),
        r["last_used_at"].isNull() ? "" : r["last_used_at"].as<std::string>(),
        r["created_at"].as<std::string>(),
        r["revoked_at"].isNull() ? "" : r["revoked_at"].as<std::string>()
    };
}

std::optional<ApiToken> ApiTokenRepository::find_by_hash(const std::string& token_hash) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, user_id, name, token_hash,"
        "  last_used_at, created_at, revoked_at"
        " FROM api_tokens WHERE token_hash = ? AND revoked_at IS NULL",
        token_hash.c_str());
    if (rows.empty()) return std::nullopt;
    const auto& r = rows[0];
    return ApiToken{
        r["id"].as<std::string>(),
        r["user_id"].as<std::string>(),
        r["name"].as<std::string>(),
        r["token_hash"].as<std::string>(),
        r["last_used_at"].isNull() ? "" : r["last_used_at"].as<std::string>(),
        r["created_at"].as<std::string>(),
        r["revoked_at"].isNull() ? "" : r["revoked_at"].as<std::string>()
    };
}

std::vector<ApiToken> ApiTokenRepository::list_for_user(const std::string& user_id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, user_id, name, token_hash,"
        "  last_used_at, created_at, revoked_at"
        " FROM api_tokens WHERE user_id = ? AND revoked_at IS NULL"
        " ORDER BY created_at DESC",
        user_id.c_str());
    std::vector<ApiToken> out;
    for (const auto& r : rows) {
        out.push_back({
            r["id"].as<std::string>(),
            r["user_id"].as<std::string>(),
            r["name"].as<std::string>(),
            r["token_hash"].as<std::string>(),
            r["last_used_at"].isNull() ? "" : r["last_used_at"].as<std::string>(),
            r["created_at"].as<std::string>(),
            r["revoked_at"].isNull() ? "" : r["revoked_at"].as<std::string>()
        });
    }
    return out;
}

void ApiTokenRepository::revoke(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE api_tokens SET revoked_at = datetime('now') WHERE id = ?",
        id.c_str());
}

void ApiTokenRepository::touch(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE api_tokens SET last_used_at = datetime('now') WHERE id = ?",
        id.c_str());
}

}  // namespace acadia

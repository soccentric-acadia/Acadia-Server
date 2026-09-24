#include "repositories/ApiTokenRepository.h"
#include "db/DbClient.h"

#include <drogon/orm/DbClient.h>

#include <ctime>
#include <sstream>

namespace acadia
{

namespace
{

std::optional<std::string> format_ts(std::optional<std::chrono::system_clock::time_point> tp)
{
    if (!tp)
        return std::nullopt;
    std::time_t t = std::chrono::system_clock::to_time_t(*tp);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return std::string{buf};
}

ApiToken row_to_token(const drogon::orm::Row& r)
{
    return ApiToken{r["id"].as<std::string>(),
                    r["user_id"].as<std::string>(),
                    r["name"].as<std::string>(),
                    r["token_hash"].as<std::string>(),
                    r["last_used_at"].isNull() ? "" : r["last_used_at"].as<std::string>(),
                    r["created_at"].as<std::string>(),
                    r["revoked_at"].isNull() ? "" : r["revoked_at"].as<std::string>(),
                    r["expires_at"].isNull() ? "" : r["expires_at"].as<std::string>(),
                    r["scopes"].as<std::string>()};
}

const std::string kCols = "SELECT id, user_id, name, token_hash,"
                         " last_used_at::TEXT, created_at::TEXT, revoked_at::TEXT, expires_at::TEXT, scopes";
} // namespace

ApiToken ApiTokenRepository::create(const std::string& user_id, const std::string& name, const std::string& token_hash,
                                    std::optional<std::chrono::system_clock::time_point> expires_at, const std::string& scopes)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(kCols + " FROM api_tokens"
                                " WHERE id = ("
                                "   INSERT INTO api_tokens (user_id, name, token_hash, expires_at, scopes)"
                                "   VALUES ($1::UUID, $2, $3, $4, $5) RETURNING id)",
                                user_id.c_str(), name.c_str(), token_hash.c_str(), format_ts(expires_at)->c_str(), scopes.c_str());
    return row_to_token(rows[0]);
}

std::optional<ApiToken> ApiTokenRepository::find_by_hash(const std::string& token_hash)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(std::string{kCols} +
                                " FROM api_tokens WHERE token_hash = $1 AND revoked_at IS NULL"
                                " AND (expires_at IS NULL OR expires_at > NOW())",
                                token_hash.c_str());
    if (rows.empty())
        return std::nullopt;
    return row_to_token(rows[0]);
}

std::optional<ApiToken> ApiTokenRepository::find_by_id(const std::string& id)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(std::string{kCols} + " FROM api_tokens WHERE id = $1::UUID", id.c_str());
    if (rows.empty())
        return std::nullopt;
    return row_to_token(rows[0]);
}

std::vector<ApiToken> ApiTokenRepository::list_for_user(const std::string& user_id)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(std::string{kCols} + " FROM api_tokens WHERE user_id = $1::UUID AND revoked_at IS NULL ORDER BY created_at DESC",
                                user_id.c_str());
    std::vector<ApiToken> out;
    for (const auto& r : rows)
        out.push_back(row_to_token(r));
    return out;
}

void ApiTokenRepository::revoke(const std::string& id)
{
    auto db = db::get_db_client();
    db->execSqlSync("UPDATE api_tokens SET revoked_at = NOW() WHERE id = $1::UUID", id.c_str());
}

int ApiTokenRepository::revoke_for_user(const std::string& user_id)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync("UPDATE api_tokens SET revoked_at = NOW() WHERE user_id = $1::UUID AND revoked_at IS NULL RETURNING id", user_id.c_str());
    return static_cast<int>(rows.size());
}

void ApiTokenRepository::touch(const std::string& id)
{
    // SRV-014: debounce — only touch when the last touch is older than 60 s,
    // so authenticated hot paths do not write to Postgres on every request.
    auto db = db::get_db_client();
    db->execSqlSync("UPDATE api_tokens SET last_used_at = NOW()"
                    " WHERE id = $1::UUID AND (last_used_at IS NULL OR last_used_at < NOW() - INTERVAL '60 seconds')",
                    id.c_str());
}

} // namespace acadia

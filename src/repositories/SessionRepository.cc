#include "repositories/SessionRepository.h"
#include "db/DbClient.h"

#include <drogon/orm/DbClient.h>

#include <ctime>

namespace acadia
{

namespace
{
std::time_t to_time_t(std::chrono::system_clock::time_point tp)
{
    return std::chrono::system_clock::to_time_t(tp);
}
} // namespace

void SessionRepository::create(const std::string& token, const std::string& user_id, std::chrono::system_clock::time_point expires_at)
{
    auto db = db::get_db_client();
    db->execSqlSync("INSERT INTO sessions (token, user_id, expires_at) VALUES ($1, $2::UUID, $3::TIMESTAMPTZ)", token.c_str(), user_id.c_str(),
                    static_cast<std::intmax_t>(to_time_t(expires_at)));
}

std::optional<std::string> SessionRepository::validate(const std::string& token)
{
    auto db = db::get_db_client();
    auto rows = db->execSqlSync("SELECT user_id FROM sessions WHERE token = $1 AND expires_at > NOW()", token.c_str());
    if (rows.empty())
        return std::nullopt;
    const std::string user_id = rows[0]["user_id"].as<std::string>();
    // Debounced activity refresh (only writes when > 60 s stale).
    db->execSqlSync("UPDATE sessions SET last_seen_at = NOW()"
                    " WHERE token = $1 AND last_seen_at < NOW() - INTERVAL '60 seconds'",
                    token.c_str());
    return user_id;
}

void SessionRepository::delete_token(const std::string& token)
{
    auto db = db::get_db_client();
    db->execSqlSync("DELETE FROM sessions WHERE token = $1", token.c_str());
}

void SessionRepository::delete_for_user(const std::string& user_id)
{
    auto db = db::get_db_client();
    db->execSqlSync("DELETE FROM sessions WHERE user_id = $1::UUID", user_id.c_str());
}

} // namespace acadia

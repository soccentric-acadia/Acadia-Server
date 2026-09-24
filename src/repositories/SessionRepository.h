#pragma once
// repositories/SessionRepository.h — persistent session tokens (SRV-016).
#include <chrono>
#include <optional>
#include <string>

namespace acadia
{

class SessionRepository
{
  public:
    SessionRepository() = default;

    // Create a session row. token must be a fresh random 256-bit hex string.
    void create(const std::string& token, const std::string& user_id, std::chrono::system_clock::time_point expires_at);

    // Look up a live session and refresh last_seen (debounced in SQL, SRV-014
    // pattern). Returns user_id, or nullopt when the token is unknown/expired.
    std::optional<std::string> validate(const std::string& token);

    // Delete one session.
    void delete_token(const std::string& token);

    // Delete all of a user's sessions (e.g. after password change, SRV-011).
    void delete_for_user(const std::string& user_id);
};

} // namespace acadia

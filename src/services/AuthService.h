#pragma once
// services/AuthService.h — authentication and token management.
#include "config/Settings.h"
#include "repositories/ApiTokenRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/SessionRepository.h"
#include "repositories/UserRepository.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace acadia
{

struct LoginResult
{
    User user;
    std::string session_token; // opaque random token stored in signed cookie
};

class AuthService
{
  public:
    // SRV-016: cookie name + session lifetime (s) shared with the filter.
    static constexpr const char* kSessionCookie = "acadia_session";
    static constexpr int kSessionLifetimeSec = 3600;

    explicit AuthService(const AuthSettings& cfg);

    // Login with username/password. Returns user + session token on success,
    // nullopt on bad credentials / rate limit / lockout (SRV-008).
    std::optional<LoginResult> login(const std::string& username, const std::string& password, const std::string& remote_ip);

    // Validate a persistent session token (DB-backed, SRV-015/016).
    // Returns user_id on success.
    std::optional<std::string> validate_session(const std::string& token);

    // Delete a session token (logout).
    void delete_session(const std::string& token);

    // Create a new API token with optional expiry + scopes (SRV-013).
    // Returns {record, plaintext_token}.
    std::pair<ApiToken, std::string> create_api_token(const std::string& user_id, const std::string& name,
                                                       std::optional<std::chrono::system_clock::time_point> expires_at = std::nullopt, const std::string& scopes = "");

    // Authenticate via API token. Returns user_id on success.
    std::optional<std::string> authenticate_token(const std::string& plaintext_token);

    // Change password.  Throws AuthError if old_password is wrong.
    void change_password(const std::string& user_id, const std::string& old_password, const std::string& new_password);

    // Create the initial admin user (only if no users exist yet).
    User ensure_admin(const std::string& username, const std::string& password);

    // SRV-003: true when the user exists and has role='admin'.
    bool is_admin(const std::string& user_id);

    // SRV-007: the user's role (admin/operator/viewer). 'viewer' when unknown.
    std::string role_of(const std::string& user_id);

    // List tokens for user.
    std::vector<ApiToken> list_tokens(const std::string& user_id);

    // Revoke a token.
    void revoke_token(const std::string& token_id, const std::string& actor_user_id);

    // Get user by id.
    std::optional<User> get_user(const std::string& user_id);

  private:
    struct IpWindow
    {
        std::chrono::steady_clock::time_point window_start;
        int count = 0;
    };
    struct UserFailures
    {
        int consecutive = 0;
        std::chrono::steady_clock::time_point locked_until{};
    };

    AuthSettings cfg_;
    UserRepository user_repo_;
    ApiTokenRepository token_repo_;
    AuditRepository audit_repo_;
    SessionRepository session_repo_;
    std::string dummy_hash_; // SRV-009: equalizes unknown-user timing

    // SRV-008: login throttling state (per-IP window + user lockout).
    std::mutex rate_mu_;
    std::unordered_map<std::string, IpWindow> ip_windows_;
    std::unordered_map<std::string, UserFailures> user_failures_;
};

} // namespace acadia

#include "services/AuthService.h"
#include "repositories/SessionRepository.h"
#include "util/Errors.h"
#include "util/Hash.h"
#include "util/Uuid.h"

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <chrono>
#include <mutex>
#include <stdexcept>

namespace acadia
{

// ── SRV-008: login throttling ────────────────────────────────────────────────
//  Per-IP request window (cfg_.login_rate_limit per 60 s) plus per-user
//  consecutive-failure lockout (5 failures ⇒ 5 min freeze).
namespace
{
constexpr int kLockoutFailures = 5;
constexpr std::chrono::seconds kLockoutDuration{300};

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
} // namespace

AuthService::AuthService(const AuthSettings& cfg) : cfg_(cfg)
{
    // A fixed, precomputed argon2id hash used to equalise timing on the
    // unknown-user path (SRV-009) so attackers cannot enumerate usernames
    // from response latency.
    util::Argon2Params p{cfg_.argon2_mem_kib, cfg_.argon2_iter, cfg_.argon2_parallel};
    dummy_hash_ = util::argon2id_hash("acadia-dummy-verify-constant", p);
}

namespace
{
// SRV-010: audit payloads are built with jsoncpp so IPs/identifiers with
// special characters can never break (or inject into) the JSONB document.
std::string json_audit(const std::string& key, const std::string& value)
{
    Json::Value v;
    v[key] = value;
    return v.toStyledString();
}
} // namespace

std::optional<LoginResult> AuthService::login(const std::string& username, const std::string& password, const std::string& remote_ip)
{
    // ── SRV-008 throttling ──
    {
        std::lock_guard lk(rate_mu_);
        const auto now = std::chrono::steady_clock::now();

        if (!remote_ip.empty()) {
            auto& w = ip_windows_[remote_ip];
            if (now - w.window_start > std::chrono::seconds{60}) {
                w.window_start = now;
                w.count = 0;
            }
            if (++w.count > static_cast<int>(cfg_.login_rate_limit)) {
                audit_repo_.log("", "auth.login.rate_limited", "ip", remote_ip, json_audit("attempts", std::to_string(w.count)));
                spdlog::warn("auth: login rate limit hit for ip={}", remote_ip);
                return std::nullopt;
            }
        }

        auto& uf = user_failures_[username];
        if (now < uf.locked_until) {
            audit_repo_.log("", "auth.login.locked", "user", username, json_audit("ip", remote_ip));
            spdlog::warn("auth: login while locked out username={}", username);
            return std::nullopt;
        }
    }

    auto fail_count = [&](const char* reason) {
        std::lock_guard lk(rate_mu_);
        auto it = user_failures_.find(username);
        if (it != user_failures_.end()) {
            it->second.consecutive++;
            if (it->second.consecutive >= kLockoutFailures) {
                it->second.locked_until = std::chrono::steady_clock::now() + kLockoutDuration;
                spdlog::warn("auth: {} — locking out username={} for {} s", reason, username, static_cast<int>(kLockoutDuration.count()));
            }
        }
    };

    auto user_opt = user_repo_.find_by_username(username);
    if (!user_opt) {
        // SRV-009: burn the same amount of work as a real verify so the
        // unknown-user path is indistinguishable by timing.
        util::argon2id_verify(password, dummy_hash_);
        spdlog::info("auth: login failed (unknown user) username={} ip={}", username, remote_ip);
        fail_count("unknown user");
        return std::nullopt;
    }

    if (!util::argon2id_verify(password, user_opt->password_hash)) {
        spdlog::info("auth: login failed (bad password) username={} ip={}", username, remote_ip);
        audit_repo_.log("", "auth.login.failed", "user", user_opt->id, json_audit("ip", remote_ip));
        fail_count("bad password");
        return std::nullopt;
    }

    {
        std::lock_guard lk(rate_mu_);
        user_failures_.erase(username); // success clears the failure counter
    }

    // Opaque session token: stored in the persistent sessions table (SRV-016)
    // and presented to the browser as the `acadia_session` cookie.
    std::string token = util::random_hex(32);
    session_repo_.create(token, user_opt->id, std::chrono::system_clock::now() +
                                                    std::chrono::milliseconds{static_cast<int64_t>(kSessionLifetimeSec) * 1000});
    audit_repo_.log(user_opt->id, "auth.login", "user", user_opt->id, json_audit("ip", remote_ip));
    spdlog::info("auth: login ok username={}", username);
    return LoginResult{*user_opt, token};
}

// SRV-015 + SRV-016: validate the persistent (DB-backed) session token.
std::optional<std::string> AuthService::validate_session(const std::string& token)
{
    if (token.empty())
        return std::nullopt;
    return session_repo_.validate(token);
}

void AuthService::delete_session(const std::string& token)
{
    if (!token.empty())
        session_repo_.delete_token(token);
}

std::pair<ApiToken, std::string> AuthService::create_api_token(const std::string& user_id, const std::string& name,
                                                                std::optional<std::chrono::system_clock::time_point> expires_at, const std::string& scopes)
{
    std::string plaintext = util::random_token(32);
    std::string hash = util::sha256_hex(plaintext);
    auto token = token_repo_.create(user_id, name, hash, expires_at, scopes);
    audit_repo_.log(user_id, "token.created", "api_token", token.id, json_audit("name", name));
    return {token, plaintext};
}

std::optional<std::string> AuthService::authenticate_token(const std::string& plaintext_token)
{
    if (plaintext_token.empty())
        return std::nullopt;
    std::string hash = util::sha256_hex(plaintext_token);
    auto token_opt = token_repo_.find_by_hash(hash); // expiry filtered in SQL (SRV-013)
    if (!token_opt)
        return std::nullopt;
    token_repo_.touch(token_opt->id); // debounced (SRV-014)
    return token_opt->user_id;
}

static bool password_policy_ok(const std::string& password)
{
    if (password.size() < 12)
        return false;
    bool lower = false, upper = false, digit = false;
    for (char c : password) {
        if (std::islower(static_cast<unsigned char>(c)))
            lower = true;
        else if (std::isupper(static_cast<unsigned char>(c)))
            upper = true;
        else if (std::isdigit(static_cast<unsigned char>(c)))
            digit = true;
    }
    return lower && upper && digit;
}

void AuthService::change_password(const std::string& user_id, const std::string& old_password, const std::string& new_password)
{
    auto user_opt = user_repo_.find_by_id(user_id);
    if (!user_opt)
        throw AuthError("user not found");

    if (!util::argon2id_verify(old_password, user_opt->password_hash)) {
        throw AuthError("incorrect current password");
    }

    // SRV-011: password policy (len 12, mixed case + digit) enforced server-side.
    if (!password_policy_ok(new_password)) {
        throw AuthError("password too weak: need at least 12 chars with upper, lower and a digit");
    }

    util::Argon2Params p{cfg_.argon2_mem_kib, cfg_.argon2_iter, cfg_.argon2_parallel};
    std::string new_hash = util::argon2id_hash(new_password, p);
    user_repo_.update_password(user_id, new_hash);

    // SRV-011: invalidate every existing credential of this user — all API
    // tokens and all sessions — a password change supersedes old material.
    token_repo_.revoke_for_user(user_id);
    session_repo_.delete_for_user(user_id);

    audit_repo_.log(user_id, "auth.password_changed", "user", user_id, json_audit("tokens_revoked", ""));
}

User AuthService::ensure_admin(const std::string& username, const std::string& password)
{
    auto existing = user_repo_.find_by_username(username);
    if (existing)
        return *existing;

    util::Argon2Params p{cfg_.argon2_mem_kib, cfg_.argon2_iter, cfg_.argon2_parallel};
    std::string hash = util::argon2id_hash(password, p);
    auto user = user_repo_.create(username, hash, /*role=*/"admin");
    spdlog::info("auth: created admin user '{}'", username);
    return user;
}

bool AuthService::is_admin(const std::string& user_id)
{
    auto user_opt = user_repo_.find_by_id(user_id);
    return user_opt.has_value() && user_opt->role == "admin";
}

std::string AuthService::role_of(const std::string& user_id)
{
    auto user_opt = user_repo_.find_by_id(user_id);
    return user_opt ? user_opt->role : "viewer";
}

std::vector<ApiToken> AuthService::list_tokens(const std::string& user_id)
{
    return token_repo_.list_for_user(user_id);
}

void AuthService::revoke_token(const std::string& token_id, const std::string& actor_user_id)
{
    // Task 96: owner-constrained revocation — a user may only revoke their OWN
    // token; admins may revoke any token (SRV-007 RBAC).
    auto tok = token_repo_.find_by_id(token_id);
    if (!tok) {
        throw AuthError("token not found");
    }
    if (tok->user_id != actor_user_id && !is_admin(actor_user_id)) {
        throw AuthError("cannot revoke a token you do not own");
    }
    token_repo_.revoke(token_id);
    audit_repo_.log(actor_user_id, "token.revoked", "api_token", token_id, json_audit("name", tok->name));
}

std::optional<User> AuthService::get_user(const std::string& user_id)
{
    return user_repo_.find_by_id(user_id);
}

} // namespace acadia

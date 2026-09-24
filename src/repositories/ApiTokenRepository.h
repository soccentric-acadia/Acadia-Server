#pragma once
// repositories/ApiTokenRepository.h
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace acadia
{

struct ApiToken
{
    std::string id;
    std::string user_id;
    std::string name;
    std::string token_hash;
    std::string last_used_at;
    std::string created_at;
    std::string revoked_at;
    std::string expires_at; // SRV-013: empty = unlimited
    std::string scopes;     // SRV-013: comma-separated; empty = legacy full access
};

class ApiTokenRepository
{
  public:
    // Create a new token record (token_hash is SHA-256 of the plaintext).
    // SRV-013: optional expiry and scopes ("" = unlimited / full access).
    ApiToken create(const std::string& user_id, const std::string& name, const std::string& token_hash, std::optional<std::chrono::system_clock::time_point> expires_at = std::nullopt,
                    const std::string& scopes = "");

    // Find by token_hash (used during authentication). Expired tokens are
    // filtered in SQL (SRV-013).
    std::optional<ApiToken> find_by_hash(const std::string& token_hash);
    // Task 96: look up a token by id to enforce owner-constrained revocation.
    std::optional<ApiToken> find_by_id(const std::string& id);

    // List all non-revoked (expired included, for bookkeeping) tokens for a user.
    std::vector<ApiToken> list_for_user(const std::string& user_id);

    // Revoke a token by id.
    void revoke(const std::string& id);

    // SRV-011: revoke every token of a user (e.g. on password change).
    // Returns the number of rows revoked.
    int revoke_for_user(const std::string& user_id);

    // Update last_used_at to now.
    void touch(const std::string& id);
};

} // namespace acadia

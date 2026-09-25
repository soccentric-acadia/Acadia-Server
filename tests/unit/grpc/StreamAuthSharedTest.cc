// tests/unit/grpc/StreamAuthSharedTest.cc — VB-8: the shared token-auth
// comparison used by both ClientService's stream auth (X2-003) and
// HalService's new call auth (SRV-025), which had none before this task.
#include "grpc_server/StreamAuth.h"

#include <gtest/gtest.h>

using acadia::grpc::verify_token_auth;

static std::multimap<::grpc::string_ref, ::grpc::string_ref> meta_with(const std::string& key, const std::string& value)
{
    std::multimap<::grpc::string_ref, ::grpc::string_ref> md;
    md.emplace(key, value);
    return md;
}

TEST(VerifyTokenAuthTest, EmptyExpectedTokenAlwaysOk)
{
    std::multimap<::grpc::string_ref, ::grpc::string_ref> empty_md;
    EXPECT_TRUE(verify_token_auth(empty_md, "", "x-acadia-hal-token").ok());
}

TEST(VerifyTokenAuthTest, MissingMetadataRejected)
{
    std::multimap<::grpc::string_ref, ::grpc::string_ref> empty_md;
    auto status = verify_token_auth(empty_md, "secret", "x-acadia-hal-token");
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::UNAUTHENTICATED);
}

TEST(VerifyTokenAuthTest, WrongTokenRejected)
{
    static const std::string key = "x-acadia-hal-token", wrong = "wrong";
    auto md = meta_with(key, wrong);
    auto status = verify_token_auth(md, "secret", key);
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::UNAUTHENTICATED);
}

TEST(VerifyTokenAuthTest, CorrectTokenAccepted)
{
    static const std::string key = "x-acadia-hal-token", secret = "secret";
    auto md = meta_with(key, secret);
    EXPECT_TRUE(verify_token_auth(md, secret, key).ok());
}

// tests/unit/grpc/StreamAuthTest.cc — X2-003: ClientService stream token auth.
#include "grpc_server/ClientServiceImpl.h"

#include <gtest/gtest.h>

#include <string>

using acadia::grpc::verify_stream_auth;

// grpc::string_ref is non-owning; the referenced strings must outlive the map.
static const std::string kKey = "x-acadia-stream-token";
static const std::string kSecret = "secret-token";
static const std::string kWrong = "wrong";
static const std::string kEmpty = "";

static std::multimap<::grpc::string_ref, ::grpc::string_ref> meta_with(
    const std::string& key, const std::string& value) {
    std::multimap<::grpc::string_ref, ::grpc::string_ref> md;
    md.emplace(key, value);
    return md;
}

TEST(StreamAuthTest, OpenStreamWhenNoTokenConfigured) {
    // Dev/bench mode: no expected token → always OK.
    EXPECT_TRUE(verify_stream_auth({}, "").ok());
    EXPECT_TRUE(
        verify_stream_auth(meta_with("x-acadia-stream-token", "anything"), "").ok());
}

TEST(StreamAuth, MissingMetadataRejectedWhenTokenRequired) {
    auto status = verify_stream_auth({}, kSecret);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
}

TEST(StreamAuth, WrongTokenRejected) {
    auto md = meta_with(kKey, kWrong);
    auto status = verify_stream_auth(md, kSecret);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
}

TEST(StreamAuth, CorrectTokenAccepted) {
    auto md = meta_with(kKey, kSecret);
    EXPECT_TRUE(verify_stream_auth(md, kSecret).ok());
}

TEST(StreamAuth, EmptyTokenValueStillRejected) {
    auto md = meta_with(kKey, kEmpty);
    auto status = verify_stream_auth(md, kSecret);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
}
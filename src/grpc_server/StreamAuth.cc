#include "grpc_server/StreamAuth.h"

namespace acadia::grpc
{

::grpc::Status verify_token_auth(const std::multimap<::grpc::string_ref, ::grpc::string_ref>& metadata, const std::string& expected_token,
                                 const std::string& metadata_key)
{
    if (expected_token.empty())
        return ::grpc::Status::OK;
    auto it = metadata.find(metadata_key);
    if (it == metadata.end())
        return {::grpc::StatusCode::UNAUTHENTICATED, "missing " + metadata_key + " metadata"};
    if (it->second != expected_token)
        return {::grpc::StatusCode::UNAUTHENTICATED, "invalid token"};
    return ::grpc::Status::OK;
}

} // namespace acadia::grpc

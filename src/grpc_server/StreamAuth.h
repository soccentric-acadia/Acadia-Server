#pragma once
// grpc_server/StreamAuth.h — shared metadata-token comparison for gRPC
// service auth (X2-003 for ClientService; VB-8/SRV-025 for HalService,
// which had no auth at all before this was extracted and reused).
//
// Pure function over the request metadata so it's unit-testable without a
// live ServerContext. Does NOT log the "running open" warning itself —
// each call site owns its own one-time (per-process) warning via its own
// `static std::once_flag`, so ClientService and HalService each warn once,
// distinctly, rather than sharing one flag that would suppress the second
// service's warning.
//
// `expected_token` empty means "open" (dev/bench/CI only) and this always
// returns OK; `expected_token` set requires `metadata_key` present in
// `metadata` and equal to it, else UNAUTHENTICATED.

#include <grpcpp/grpcpp.h>

#include <map>
#include <string>

namespace acadia::grpc
{

::grpc::Status verify_token_auth(const std::multimap<::grpc::string_ref, ::grpc::string_ref>& metadata, const std::string& expected_token,
                                 const std::string& metadata_key);

} // namespace acadia::grpc

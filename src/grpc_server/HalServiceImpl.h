#pragma once
// grpc_server/HalServiceImpl.h — implements the HalService gRPC service.
// Uses the gRPC sync API for unary RPCs and the async API for streaming RPCs.

// Forward-declare generated types to avoid include-order issues.
// The actual proto headers are included in HalServiceImpl.cc.

#include "registry/ModuleRegistry.h"
#include "services/RunService.h"
#include "services/LogService.h"
#include "services/ModuleService.h"
#include "config/Settings.h"

// Include generated gRPC header — path resolved after protoc generation.
#include "hal_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <string>

namespace acadia::grpc {

class HalServiceImpl final
    : public ::HalService::Service {
public:
    HalServiceImpl(RunService&        run_svc,
                   LogService&        log_svc,
                   ModuleService&     mod_svc,
                   const Settings&    settings);

    // Unary RPC — run a single-shot test for one module.
    ::grpc::Status RunSingleShot(
        ::grpc::ServerContext*         ctx,
        const ::SingleShotRequest*     req,
        ::ResultEnvelope*              resp) override;

    // Server-streaming RPCs.
    ::grpc::Status StartLongTerm(
        ::grpc::ServerContext*              ctx,
        const ::LongTermRequest*            req,
        ::grpc::ServerWriter<::ResultEnvelope>* writer) override;

    ::grpc::Status StartMonitoring(
        ::grpc::ServerContext*              ctx,
        const ::MonitoringRequest*          req,
        ::grpc::ServerWriter<::ResultEnvelope>* writer) override;

    ::grpc::Status StartEventBased(
        ::grpc::ServerContext*              ctx,
        const ::EventBasedRequest*          req,
        ::grpc::ServerWriter<::ResultEnvelope>* writer) override;

    // Stop an active run/stream.
    ::grpc::Status Stop(
        ::grpc::ServerContext*  ctx,
        const ::StopRequest*    req,
        ::StopResponse*         resp) override;

    // List available modules.
    ::grpc::Status ListModules(
        ::grpc::ServerContext*          ctx,
        const ::ListModulesRequest*     req,
        ::ListModulesResponse*          resp) override;

private:
    // Validate module_id against registry.  Returns non-OK status if invalid.
    ::grpc::Status validate_module(const std::string& module_id);

    // Persist a ResultEnvelope and broadcast to REST subscribers.
    void persist_result(const std::string& run_id, const ::ResultEnvelope& env);

    // Streaming helper: read envelopes from the writer-side and ingest them.
    ::grpc::Status run_streaming(
        ::grpc::ServerContext*              ctx,
        const std::string&                  run_id,
        const std::string&                  module_id,
        ::grpc::ServerWriter<::ResultEnvelope>* writer);

    RunService&     run_svc_;
    LogService&     log_svc_;
    ModuleService&  mod_svc_;
    const Settings& settings_;

    // Active runs: run_id → cancellation flag
    std::mutex cancel_mutex_;
    std::unordered_map<std::string, std::atomic<bool>*> cancel_flags_;
};

}  // namespace acadia::grpc

#include "grpc_server/HalServiceImpl.h"
#include "grpc_server/StreamBackpressure.h"
#include "util/Uuid.h"
#include "util/Time.h"

#include <spdlog/spdlog.h>

#include <thread>
#include <vector>

namespace acadia::grpc {

HalServiceImpl::HalServiceImpl(RunService&     run_svc,
                                 LogService&     log_svc,
                                 ModuleService&  mod_svc,
                                 const Settings& settings)
    : run_svc_(run_svc),
      log_svc_(log_svc),
      mod_svc_(mod_svc),
      settings_(settings) {}

::grpc::Status HalServiceImpl::validate_module(const std::string& module_id) {
    if (module_id.empty()) {
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, "module_id is required"};
    }
    if (!mod_svc_.validate(module_id)) {
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT,
                              "unknown module_id: " + module_id};
    }
    return ::grpc::Status::OK;
}

void HalServiceImpl::persist_result(const std::string& run_id,
                                     const hal::common::ResultEnvelope& env) {
    Result r;
    r.run_id           = run_id;
    r.module_id        = env.module_id();
    r.module_version   = env.module_version();
    r.timestamp        = util::utc_now_iso8601();
    r.mode             = std::to_string(static_cast<int>(env.mode()));
    r.status           = env.status() == hal::common::PASS ? "pass" : "fail";
    r.error_message    = env.error_message();
    r.payload_type_url = env.payload().type_url();

    // Serialize the Any payload bytes
    std::string serialized = env.payload().value();
    r.payload_bytes.assign(serialized.begin(), serialized.end());

    run_svc_.ingest_result(r);
}

::grpc::Status HalServiceImpl::RunSingleShot(
    ::grpc::ServerContext*          /*ctx*/,
    const hal::SingleShotRequest*   req,
    hal::common::ResultEnvelope*    resp) {

    auto status = validate_module(req->module_id());
    if (!status.ok()) return status;

    spdlog::info("grpc: RunSingleShot module_id={}", req->module_id());

    // Create a run to record this single-shot request.
    auto run = run_svc_.start_run("{}", "single_shot", "single_shot", "");
    run_svc_.mark_running(run.id);

    // The actual execution happens on the client (HAL); we just record the result.
    // The client will call this RPC and return a ResultEnvelope directly.
    // We echo it back and persist it.
    resp->set_module_id(req->module_id());
    resp->set_status(hal::common::PASS);

    persist_result(run.id, *resp);
    run_svc_.complete_run(run.id, "completed");

    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::StartLongTerm(
    ::grpc::ServerContext*                              ctx,
    const hal::LongTermRequest*                        req,
    ::grpc::ServerWriter<hal::common::ResultEnvelope>* writer) {

    auto status = validate_module(req->module_id());
    if (!status.ok()) return status;

    auto run = run_svc_.start_run("{}", "long_term", "long_term", "");
    run_svc_.mark_running(run.id);

    spdlog::info("grpc: StartLongTerm run_id={} module_id={}", run.id, req->module_id());

    hal::common::ResultEnvelope ack;
    ack.set_module_id(req->module_id());
    ack.set_status(hal::common::PASS);
    writer->Write(ack);

    // Block until client cancels or server shutdown.
    while (!ctx->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    run_svc_.complete_run(run.id, "completed");
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::StartMonitoring(
    ::grpc::ServerContext*                              ctx,
    const hal::MonitoringRequest*                      req,
    ::grpc::ServerWriter<hal::common::ResultEnvelope>* writer) {

    auto status = validate_module(req->module_id());
    if (!status.ok()) return status;

    auto run = run_svc_.start_run("{}", "monitoring", "monitoring", "");
    run_svc_.mark_running(run.id);

    spdlog::info("grpc: StartMonitoring run_id={} module_id={}", run.id, req->module_id());

    hal::common::ResultEnvelope ack;
    ack.set_module_id(req->module_id());
    ack.set_status(hal::common::PASS);
    writer->Write(ack);

    while (!ctx->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    run_svc_.complete_run(run.id, "completed");
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::StartEventBased(
    ::grpc::ServerContext*                              ctx,
    const hal::EventBasedRequest*                      req,
    ::grpc::ServerWriter<hal::common::ResultEnvelope>* writer) {

    auto status = validate_module(req->module_id());
    if (!status.ok()) return status;

    auto run = run_svc_.start_run("{}", "event_based", "event_based", "");
    run_svc_.mark_running(run.id);

    spdlog::info("grpc: StartEventBased run_id={} module_id={}", run.id, req->module_id());

    hal::common::ResultEnvelope ack;
    ack.set_module_id(req->module_id());
    ack.set_status(hal::common::PASS);
    writer->Write(ack);

    while (!ctx->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    run_svc_.complete_run(run.id, "completed");
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::Stop(
    ::grpc::ServerContext*   /*ctx*/,
    const hal::StopRequest*  req,
    hal::StopResponse*       resp) {

    spdlog::info("grpc: Stop session_id={}", req->session_id());
    run_svc_.cancel_run(req->session_id(), "");
    resp->set_accepted(true);
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::ListModules(
    ::grpc::ServerContext*           /*ctx*/,
    const hal::ListModulesRequest*   /*req*/,
    hal::ListModulesResponse*        resp) {

    for (const auto& m : mod_svc_.list_all()) {
        auto* entry = resp->add_modules();
        entry->set_id(m.id);
        entry->set_config_type(m.config_type_url);
        entry->set_result_type(m.result_type_url);
        entry->set_version(0);
        entry->set_status("active");
    }
    return ::grpc::Status::OK;
}

}  // namespace acadia::grpc

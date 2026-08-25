#include "grpc_server/HalServiceImpl.h"
#include "grpc_server/StreamBackpressure.h"
#include "util/Uuid.h"
#include "util/Time.h"
#include "util/ResultConvert.h"

#include "common.pb.h"

#include <google/protobuf/any.pb.h>
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
    // Unpack the Any payload as a TestResult proto.
    const std::string expected_url = "type.googleapis.com/hal.common.TestResult";
    if (env.payload().type_url() != expected_url) {
        spdlog::warn("grpc: persist_result: unexpected type_url='{}' run_id={}",
                     env.payload().type_url(), run_id);
        // Fall back: build a minimal Result from the envelope fields.
        Result r;
        r.run_id           = run_id;
        r.module_id        = env.module_id();
        r.module_version   = env.module_version();
        r.timestamp        = util::utc_now_iso8601();
        r.mode             = std::to_string(static_cast<int>(env.mode()));
        r.status           = static_cast<int16_t>(env.status());
        r.error_message    = env.error_message();
        r.payload_type_url = env.payload().type_url();
        std::string serialized = env.payload().value();
        r.payload_bytes.assign(serialized.begin(), serialized.end());
        run_svc_.ingest_result(r);
        return;
    }

    hal::common::TestResult tr;
    if (!env.payload().UnpackTo(&tr)) {
        spdlog::error("grpc: persist_result: failed to unpack TestResult run_id={}", run_id);
        return;
    }

    // Reject UNSPECIFIED status at the boundary.
    if (tr.output().status() == hal::common::TEST_STATUS_UNSPECIFIED) {
        spdlog::warn("grpc: persist_result: UNSPECIFIED status rejected run_id={} module_id={}",
                     run_id, env.module_id());
        return;
    }

    const std::string module_version = env.module_version().empty()
        ? std::to_string(static_cast<int>(env.mode()))
        : env.module_version();
    const std::string mode = std::to_string(static_cast<int>(env.mode()));

    run_svc_.ingest_test_result(tr, run_id, module_version, mode);
}

::grpc::Status HalServiceImpl::RunSingleShot(
    ::grpc::ServerContext*          /*ctx*/,
    const hal::SingleShotRequest*   req,
    hal::common::ResultEnvelope*    resp) {

    auto status = validate_module(req->module_id());
    if (!status.ok()) return status;

    spdlog::info("grpc: RunSingleShot module_id={}", req->module_id());

    // Task 83: carry the real request into the run snapshot (module_config +
    // common config), not a synthetic empty "{}". The client dispatches the
    // actual execution; the run is created with the request it represents.
    std::string cfg_json = "{}";
    if (req->has_module_config()) {
        std::string any_bytes;
        req->module_config().SerializeToString(&any_bytes);
        cfg_json = "{\"module_config\":\"" + any_bytes + "\","
                   "\"common\":\"" + req->common().SerializeAsString() + "\"}";
    }
    auto run = run_svc_.start_run(cfg_json, "single_shot", "single_shot", "");
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

// Task 84: shared streaming loop — subscribe to the run's results, stream them
// for duration/forever, honor cancellation/deadline.
void HalServiceImpl::run_streaming(
    ::grpc::ServerContext*                              ctx,
    const std::string&                                  module_id,
    const std::string&                                  run_mode,
    hal::common::ExecutionMode                          exec_mode,
    const hal::common::CommonConfig&                    common,
    ::grpc::ServerWriter<hal::common::ResultEnvelope>* writer) {

    const bool    run_forever = common.run_forever() || common.duration_ms() <= 0;
    const int64_t duration_ms = common.duration_ms();
    const auto     dl = ctx->deadline();
    const bool has_deadline = dl != std::chrono::system_clock::time_point::max();

    auto run = run_svc_.start_run("{}", run_mode, run_mode, "");
    run_svc_.mark_running(run.id);
    spdlog::info("grpc: {} run_id={} module_id={} forever={}",
                 run_mode, run.id, module_id, run_forever);

    auto started = std::chrono::steady_clock::now();
    std::mutex w_mu;
    auto sub = run_svc_.subscribe_results(run.id, [&](const Result& r) {
        hal::common::ResultEnvelope env;
        env.set_module_id(module_id);
        env.set_mode(exec_mode);
        env.set_status(static_cast<hal::common::TestStatus>(r.status));
        env.set_error_message(r.error_message);
        std::lock_guard<std::mutex> lk(w_mu);
        writer->Write(env);
    });

    while (!ctx->IsCancelled()) {
        if (!run_forever) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= duration_ms) break;
        }
        if (has_deadline && std::chrono::system_clock::now() >= dl) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    run_svc_.unsubscribe_results(sub);
    run_svc_.complete_run(run.id, "completed");
}

::grpc::Status HalServiceImpl::StartLongTerm(
    ::grpc::ServerContext*                              ctx,
    const hal::LongTermRequest*                        req,
    ::grpc::ServerWriter<hal::common::ResultEnvelope>* writer) {
    if (auto st = validate_module(req->module_id()); !st.ok()) return st;
    run_streaming(ctx, req->module_id(), "long_term", hal::common::LONG_TERM,
                  req->common(), writer);
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::StartMonitoring(
    ::grpc::ServerContext*                              ctx,
    const hal::MonitoringRequest*                      req,
    ::grpc::ServerWriter<hal::common::ResultEnvelope>* writer) {
    if (auto st = validate_module(req->module_id()); !st.ok()) return st;
    run_streaming(ctx, req->module_id(), "monitoring", hal::common::MONITORING,
                  req->common(), writer);
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::StartEventBased(
    ::grpc::ServerContext*                              ctx,
    const hal::EventBasedRequest*                      req,
    ::grpc::ServerWriter<hal::common::ResultEnvelope>* writer) {
    if (auto st = validate_module(req->module_id()); !st.ok()) return st;
    run_streaming(ctx, req->module_id(), "event_based", hal::common::EVENT_BASED,
                  req->common(), writer);
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::Pause(
    ::grpc::ServerContext*   /*ctx*/,
    const hal::PauseRequest*  req,
    hal::PauseResponse*       resp) {

    spdlog::info("grpc: Pause session_id={}", req->session_id());
    run_svc_.pause_run(req->session_id(), "");
    resp->set_accepted(true);
    return ::grpc::Status::OK;
}

::grpc::Status HalServiceImpl::Resume(
    ::grpc::ServerContext*   /*ctx*/,
    const hal::ResumeRequest*  req,
    hal::ResumeResponse*       resp) {

    spdlog::info("grpc: Resume session_id={}", req->session_id());
    run_svc_.resume_run(req->session_id(), "");
    resp->set_accepted(true);
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

// orchestration/RunDispatcher.cc — SRV-032/SRV-033 implementation.
// See RunDispatcher.h for the design.

#include "orchestration/RunDispatcher.h"

#include "services/RunService.h"

#include <google/protobuf/any.pb.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>

#include <json/json.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <sstream>

namespace acadia::orchestration
{

namespace
{

// Map the run table's textual mode to hal.common.ExecutionMode.
bool execution_mode_from_string(const std::string& mode, hal::common::ExecutionMode* out)
{
    if (mode == "single_shot" || mode == "1") *out = hal::common::SINGLE_SHOT;
    else if (mode == "long_term" || mode == "long_run" || mode == "2") *out = hal::common::LONG_TERM;
    else if (mode == "monitoring" || mode == "3") *out = hal::common::MONITORING;
    else if (mode == "event_based" || mode == "event" || mode == "4") *out = hal::common::EVENT_BASED;
    else return false;
    return true;
}

// Snapshot module entry shape: {"module_id": "...", "test_names": [...]}.
std::string module_id_of(const Json::Value& entry)
{
    if (entry.isObject()) {
        if (entry.isMember("module_id") && entry["module_id"].isString())
            return entry["module_id"].asString();
        if (entry.isMember("id") && entry["id"].isString())
            return entry["id"].asString();
    }
    if (entry.isString()) return entry.asString();
    return "";
}

std::vector<std::string> test_names_of(const Json::Value& entry)
{
    std::vector<std::string> names;
    if (entry.isObject() && entry.isMember("test_names") && entry["test_names"].isArray()) {
        for (const auto& n : entry["test_names"]) {
            if (n.isString()) names.push_back(n.asString());
        }
    }
    return names;
}

} // namespace

RunDispatcher::RunDispatcher(acadia::RunService& run_svc, acadia::grpc::ClientRegistry& registry)
    : run_svc_(run_svc), registry_(registry)
{
}

RunDispatcher::~RunDispatcher()
{
    stop();
}

void RunDispatcher::start()
{
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    spdlog::info("RunDispatcher started");
}

void RunDispatcher::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void RunDispatcher::loop()
{
    while (running_) {
        try {
            RunFilter f;
            f.status = "queued";
            f.limit = 10;
            for (const auto& run : run_svc_.list_runs(f)) {
                dispatch_run(run);
            }
        }
        catch (const std::exception& e) {
            spdlog::error("RunDispatcher loop error: {}", e.what());
        }
        std::this_thread::sleep_for(std::chrono::seconds{2});
    }
}

void RunDispatcher::dispatch_run(const acadia::Run& run)
{
    // Parse the test-config snapshot:
    // {"name","description","mode","common_config":{...},"modules":[{"module_id","test_names":[...]}]}
    Json::Value snapshot;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream in{run.test_config_snapshot_json};
    if (!Json::parseFromStream(b, in, &snapshot, &errs) || !snapshot.isObject()) {
        spdlog::error("RunDispatcher: run {} has an unparseable config snapshot — failing run", run.id);
        run_svc_.complete_run(run.id, "failed", "config snapshot is not valid JSON");
        return;
    }

    hal::common::ExecutionMode emode;
    if (!execution_mode_from_string(run.mode.empty() ? snapshot["mode"].asString() : run.mode, &emode)) {
        spdlog::error("RunDispatcher: run {} has unknown mode '{}' — failing run", run.id, run.mode);
        run_svc_.complete_run(run.id, "failed", "unknown execution mode '" + run.mode + "'");
        return;
    }

    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_modules_.erase(run.id);
        run_mode_[run.id] = static_cast<int>(emode);
        if (snapshot.isMember("modules") && snapshot["modules"].isArray()) {
            for (const auto& m : snapshot["modules"]) {
                std::string id = module_id_of(m);
                if (!id.empty()) pending_modules_[run.id].push_back({id, test_names_of(m)});
            }
        }
        if (pending_modules_[run.id].empty()) {
            pending_modules_.erase(run.id);
            run_mode_.erase(run.id);
            run_svc_.complete_run(run.id, "failed", "config snapshot contains no runnable modules");
            return;
        }
    }

    // Target board: snapshot-specified board if connected, else any connected
    // board. With no client connected the run stays queued and is retried on
    // the next poll — a queued run must never be lost because a board was
    // momentarily offline.
    const std::string wanted = snapshot.isMember("target_board") && snapshot["target_board"].isString()
                                   ? snapshot["target_board"].asString()
                                   : "";
    std::string board;
    auto connected = registry_.connected();
    if (!wanted.empty()) {
        for (const auto& c : connected)
            if (c == wanted) { board = wanted; break; }
        if (board.empty()) {
            spdlog::warn("RunDispatcher: run {} targets board {} which is not connected — leaving queued", run.id, wanted);
            std::lock_guard<std::mutex> lk(pending_mu_);
            pending_modules_.erase(run.id);
            run_mode_.erase(run.id);
            return;
        }
    } else if (!connected.empty()) {
        board = connected.front();
    } else {
        return; // nothing connected yet — retried on the next poll
    }

    std::string first_module;
    std::vector<std::string> first_tests;
    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        run_board_[run.id] = board;
        const auto& front = pending_modules_[run.id].front();
        first_module = front.module_id;
        first_tests = front.test_names;
        pending_modules_[run.id].erase(pending_modules_[run.id].begin());
    }
    dispatch_module(run.id, board, emode, first_module, first_tests);
}

void RunDispatcher::dispatch_module(const std::string& run_id, const std::string& board,
                                    hal::common::ExecutionMode emode, const std::string& module_id,
                                    const std::vector<std::string>& test_names)
{
    acadia::stream::ServerMessage msg;
    auto* cr = msg.mutable_configure_run();
    cr->set_run_id(run_id);
    cr->set_module_id(module_id);
    cr->set_mode(emode);

    // Build hal.modules.<id>.Config Any carrying the snapshot's test
    // selection. Module protos are compiled into the server so the generated
    // pool can build exactly the message the client decodes (CLI-160).
    const std::string type_name = "hal.modules." + module_id + ".Config";
    const auto* desc = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
    if (desc) {
        google::protobuf::DynamicMessageFactory factory;
        const google::protobuf::Message* proto = factory.GetPrototype(desc);
        std::unique_ptr<google::protobuf::Message> cfg(proto->New());
        const auto* refl = cfg->GetReflection();
        if (const auto* f = desc->FindFieldByName("test_names");
            f && f->is_repeated() && f->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
            for (const auto& t : test_names) refl->AddString(cfg.get(), f, t);
        }
        cr->mutable_module_config()->PackFrom(*cfg);
    } else {
        spdlog::warn("RunDispatcher: no compiled proto for hal.modules.{}.Config — sending module_id only", module_id);
    }

    if (!registry_.SendControl(board, msg)) {
        spdlog::warn("RunDispatcher: ConfigureRun for run {} board {} not delivered (client gone)", run_id, board);
        return; // the run stays running; reconnect/reconcile recovers it
    }

    acadia::stream::ServerMessage start;
    start.mutable_start_run()->set_run_id(run_id);
    if (registry_.SendControl(board, start)) {
        run_svc_.mark_running(run_id);
        spdlog::info("RunDispatcher: dispatched run {} module {} to board {}", run_id, module_id, board);
    }
}

void RunDispatcher::on_run_completed(const std::string& run_id, const std::string& status)
{
    std::string board;
    std::string next_module;
    std::vector<std::string> next_tests;
    int emode_int = 0;
    bool have_next = false;
    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        auto it = pending_modules_.find(run_id);
        if (it == pending_modules_.end()) return;
        if (status != "completed") {
            // A failed/aborted run stops the remaining modules entirely.
            pending_modules_.erase(it);
            run_board_.erase(run_id);
            run_mode_.erase(run_id);
            return;
        }
        if (it->second.empty()) {
            pending_modules_.erase(it);
            run_board_.erase(run_id);
            run_mode_.erase(run_id);
            return;
        }
        auto m = run_mode_.find(run_id);
        if (m != run_mode_.end()) emode_int = m->second;
        auto b = run_board_.find(run_id);
        if (b != run_board_.end()) board = b->second;
        // Peek the next module and pop it.
        next_module = it->second.front().module_id;
        next_tests = it->second.front().test_names;
        it->second.erase(it->second.begin());
        have_next = !next_module.empty() && !board.empty();
    }
    if (!have_next) return;

    // Continue with the next module of the multi-module run. The client's
    // previous run with this run_id has finished, so the run_id key is free.
    dispatch_module(run_id, board, static_cast<hal::common::ExecutionMode>(emode_int),
                    next_module, next_tests);
}

void RunDispatcher::send_control(const std::string& action, const std::string& run_id)
{
    std::string board;
    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        auto it = run_board_.find(run_id);
        if (it != run_board_.end()) board = it->second;
    }
    if (board.empty()) {
        spdlog::warn("RunDispatcher: {} for run {} — no known board (run never dispatched); DB status still updated", action, run_id);
        return;
    }
    acadia::stream::ServerMessage msg;
    if (action == "stop") msg.mutable_stop_run()->set_run_id(run_id);
    else if (action == "pause") msg.mutable_pause_run()->set_run_id(run_id);
    else if (action == "resume") msg.mutable_resume_run()->set_run_id(run_id);
    else {
        spdlog::error("RunDispatcher: unknown control action '{}'", action);
        return;
    }
    if (!registry_.SendControl(board, msg)) {
        spdlog::warn("RunDispatcher: {} for run {} board {} not delivered", action, run_id, board);
    }
}

} // namespace acadia::orchestration
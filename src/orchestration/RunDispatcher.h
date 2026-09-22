#pragma once
// orchestration/RunDispatcher.h — SRV-032: REST-created and scheduled runs
// never executed because nothing sent ConfigureRun/StartRun to a board.
//
// The dispatcher polls the run table for 'queued' runs, resolves a target
// board from the connected-client registry, builds a ConfigureRun (module +
// execution mode + per-module Config Any carrying the snapshot's test
// selection) and sends it, followed by StartRun, through the ClientRegistry
// control stream. Runs whose snapshot contains several modules are dispatched
// sequentially: the next module is sent only when the current one completes
// (on_run_completed), because the client keys live runs by run_id (a second
// concurrent ConfigureRun for the same run_id is rejected as a duplicate —
// CLI-082).
//
// SRV-033: cancel/pause/resume also dispatch StopRun/PauseRun/ResumeRun to
// the board the run was dispatched to (board per run_id is tracked here).

#include "grpc_server/ClientRegistry.h"

#include "common.pb.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace acadia
{
struct Run;
class RunService;
}

namespace acadia::orchestration
{

class RunDispatcher
{
  public:
    RunDispatcher(acadia::RunService& run_svc, acadia::grpc::ClientRegistry& registry);
    ~RunDispatcher();

    RunDispatcher(const RunDispatcher&) = delete;
    RunDispatcher& operator=(const RunDispatcher&) = delete;

    void start();
    void stop();

    // Called by RunService when a run reaches a terminal state: advances to
    // the next module of a multi-module run (or clears the pending state).
    void on_run_completed(const std::string& run_id, const std::string& status);

    // SRV-033: route a control action ('stop'|'pause'|'resume') for a run to
    // the board it was dispatched to. Safe to call for never-dispatched runs
    // (a warning is logged; the DB status change still applies).
    void send_control(const std::string& action, const std::string& run_id);

  private:
    struct PendingModule
    {
        std::string module_id;
        std::vector<std::string> test_names;
    };

    void loop();
    void dispatch_run(const acadia::Run& run);
    void dispatch_module(const std::string& run_id, const std::string& board,
                         hal::common::ExecutionMode emode, const std::string& module_id,
                         const std::vector<std::string>& test_names);

    acadia::RunService& run_svc_;
    acadia::grpc::ClientRegistry& registry_;

    std::atomic<bool> running_{false};
    std::thread thread_;

    // Pending dispatch state (guarded by pending_mu_):
    std::mutex pending_mu_;
    // run_id → modules still to run (front = current/next).
    std::unordered_map<std::string, std::vector<PendingModule>> pending_modules_;
    // run_id → board_id the run was dispatched to (for control routing).
    std::unordered_map<std::string, std::string> run_board_;
    // run_id → execution mode of the in-flight run (for the next module).
    std::unordered_map<std::string, int> run_mode_;
};

} // namespace acadia::orchestration
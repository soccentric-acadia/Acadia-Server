#pragma once
// services/RunService.h — run lifecycle management.
#include "repositories/RunRepository.h"
#include "repositories/ResultRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/NotificationRepository.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace acadia {

// Subscriber callback for live result streaming to WebSocket/SSE clients.
using ResultSubscriber = std::function<void(const Result&)>;

class RunService {
public:
    // Create and start (enqueue) a new run.
    Run start_run(const std::string& config_snapshot_json,
                  const std::string& mode,
                  const std::string& name,
                  const std::string& actor_user_id);

    Run get_run(const std::string& id);
    std::vector<Run> list_runs(const RunFilter& f);

    // Transition the run to 'running'.
    void mark_running(const std::string& run_id);

    // Persist a result received from the gRPC stream.
    Result ingest_result(const Result& r);

    // Finalize the run.
    void complete_run(const std::string& run_id, const std::string& status,
                      const std::string& error = "");

    void cancel_run(const std::string& run_id, const std::string& actor_user_id);
    void pause_run(const std::string& run_id, const std::string& actor_user_id);
    void resume_run(const std::string& run_id, const std::string& actor_user_id);

    // Subscribe to live results for a run (REST/WebSocket live tail).
    // Returns a subscription id that can be used to unsubscribe.
    uint64_t subscribe_results(const std::string& run_id, ResultSubscriber cb);
    void     unsubscribe_results(uint64_t sub_id);

    // Notify all subscribers of a new result.
    void broadcast_result(const std::string& run_id, const Result& r);

    // Query results.
    std::vector<Result> get_results(const ResultFilter& f);
    std::optional<Result> get_result(const std::string& id);

    // Rerun a finished run with the same config snapshot.
    Run rerun(const std::string& run_id, const std::string& actor_user_id);

    // Set baseline.
    void set_baseline(const std::string& run_id, const std::string& actor_user_id);

    void delete_run(const std::string& run_id, const std::string& actor_user_id);

private:
    RunRepository        run_repo_;
    ResultRepository     result_repo_;
    AuditRepository      audit_repo_;
    NotificationRepository notif_repo_;

    std::mutex                                         subs_mutex_;
    std::unordered_map<uint64_t, ResultSubscriber>     subs_;
    std::unordered_map<std::string, std::vector<uint64_t>> run_subs_;
    std::atomic<uint64_t>                              next_sub_id_{1};
};

}  // namespace acadia

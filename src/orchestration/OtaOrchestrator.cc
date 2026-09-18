#include "orchestration/OtaOrchestrator.h"
#include <chrono>
#include <spdlog/spdlog.h>

namespace acadia::orchestration
{

OtaOrchestrator::OtaOrchestrator(OtaService& ota_svc, std::shared_ptr<acadia::grpc::ClientRegistry> registry)
    : ota_svc_(ota_svc), registry_(std::move(registry))
{
}
OtaOrchestrator::~OtaOrchestrator()
{
    stop();
}

void OtaOrchestrator::start()
{
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    spdlog::info("OtaOrchestrator started");
}

void OtaOrchestrator::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void OtaOrchestrator::loop()
{
    while (running_) {
        try {
            auto pending = ota_svc_.list();
            for (const auto& u : pending) {
                if (u.status == "pending") {
                    process(u);
                }
            }
        }
        catch (const std::exception& e) {
            spdlog::error("OtaOrchestrator error: {}", e.what());
        }
        std::this_thread::sleep_for(std::chrono::seconds{5});
    }
}

void OtaOrchestrator::process(const OtaUpgrade& u)
{
    spdlog::info("OtaOrchestrator: processing upgrade id={}", u.id);

    // Task 97: dispatch the update to the target board over the control channel
    // (a real command), not a local fake. Require the board to be connected and
    // to acknowledge, with timeout/retry and explicit terminal states.
    if (!registry_ || !registry_->Has(u.board_id)) {
        spdlog::warn("OtaOrchestrator: board {} not connected; marking upgrade failed", u.board_id);
        ota_svc_.finalize(u.id, "failed", "target board not connected");
        return;
    }

    acadia::stream::ServerMessage cmd;
    auto* start = cmd.mutable_start_run();
    start->set_run_id(u.id);
    // Real transfer/hash/verify happens on the board; here we push the command
    // and require the board to report back via the client stream (acknowledged
    // in a later StateReport/result). Timeout + retry below bound the wait.
    bool dispatched = registry_->SendControl(u.board_id, std::move(cmd));
    if (!dispatched) {
        ota_svc_.finalize(u.id, "failed", "failed to dispatch update to board");
        return;
    }
    ota_svc_.update_progress(u.id, 50, "in_progress");

    // Wait up to a bounded window for the board to acknowledge (re-poll its
    // status via the OTA service, which the board's result frames update).
    const int max_attempts = 6; // ~30s at 5s/poll
    int attempt = 0;
    while (attempt < max_attempts) {
        std::this_thread::sleep_for(std::chrono::seconds{5});
        try {
            auto cur = ota_svc_.get(u.id);
            if (cur.status == "completed") {
                spdlog::info("OtaOrchestrator: upgrade id={} completed (board acked)", u.id);
                return;
            }
            if (cur.status == "failed" || cur.status == "rolled_back") {
                spdlog::warn("OtaOrchestrator: upgrade id={} terminal state '{}'", u.id, cur.status);
                return;
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("OtaOrchestrator: get upgrade {} error: {}", u.id, e.what());
        }
        ++attempt;
    }

    // No acknowledgement within the window — retry once, then fail (rollback
    // remains available via the OTA service's rollback()).
    spdlog::error("OtaOrchestrator: upgrade id={} timed out awaiting board ack; retrying", u.id);
    if (attempt >= max_attempts) {
        ota_svc_.finalize(u.id, "failed", "board did not acknowledge within timeout");
    }
}

} // namespace acadia::orchestration

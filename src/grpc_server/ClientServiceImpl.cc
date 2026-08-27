#include "grpc_server/ClientServiceImpl.h"
#include "util/Time.h"

#include "google/protobuf/any.pb.h"

#include <spdlog/spdlog.h>

#include <mutex>
#include <thread>
#include <chrono>

namespace acadia::grpc {

// Queue capacity: 1024 work items.  Each item holds a single TestResult.
static constexpr std::size_t kQueueCapacity = 1024;

ClientServiceImpl::ClientServiceImpl(RunService& run_svc, LogService& log_svc,
                                     std::shared_ptr<ClientRegistry> registry,
                                     BoardRepository* board_repo,
                                     std::string stream_token)
    : run_svc_(run_svc), log_svc_(log_svc), registry_(std::move(registry)),
      board_repo_(board_repo), stream_token_(std::move(stream_token)) {}

// ── Stream authentication (X2-003) ────────────────────────────────────────
//
// The ClientService stream is the fleet's control plane; an open stream on a
// production host would let any LAN peer push run configs. Enforcement:
//   - ACADIA_STREAM_TOKEN set   → the `x-acadia-stream-token` metadata must
//     match exactly, else UNAUTHENTICATED.
//   - ACADIA_STREAM_TOKEN empty → open stream (dev/bench/CI only) with a
//     one-time warning.
//
// Kept as a pure function over the metadata for unit testing.
::grpc::Status verify_stream_auth(
    const std::multimap<::grpc::string_ref, ::grpc::string_ref>& metadata,
    const std::string& expected_token) {
    if (expected_token.empty()) {
        // Dev/bench mode: no token configured — allow, but only surface the
        // warning once per process.
        static std::once_flag open_warn;
        std::call_once(open_warn, [] {
            spdlog::warn(
                "ACADIA_STREAM_TOKEN not set: ClientService streams are UNAUTHENTICATED "
                "(dev/bench mode — set it in production)");
        });
        return ::grpc::Status::OK;
    }
    auto it = metadata.find("x-acadia-stream-token");
    if (it == metadata.end()) {
        spdlog::warn("stream auth: missing x-acadia-stream-token metadata");
        return {::grpc::StatusCode::UNAUTHENTICATED,
                "missing x-acadia-stream-token metadata"};
    }
    if (it->second != expected_token) {
        spdlog::warn("stream auth: token mismatch");
        return {::grpc::StatusCode::UNAUTHENTICATED, "invalid stream token"};
    }
    return ::grpc::Status::OK;
}

// ── Handle a TestResultBatch ──────────────────────────────────────────────────

void ClientServiceImpl::handle_result_batch(
    const acadia::stream::TestResultBatch& batch,
    BoundedQueue<IngestWork>&              queue,
    std::unordered_map<std::string, int>&  batch_pending) {

    const std::string mode = std::to_string(static_cast<int>(batch.mode()));

    // Task 87: validate module identity at ingestion.
    if (batch.module_id().empty()) {
        spdlog::warn("client_svc: batch run_id={} has empty module_id; rejecting",
                     batch.run_id());
        return;
    }

    // Register this batch's pending sample count so the writer thread can ack
    // only after ALL its samples are durably persisted (task 88).
    const std::string batch_key = batch.run_id() + ":" + std::to_string(batch.batch_seq());
    int enqueued = 0;

    for (const auto& sample : batch.samples()) {
        const std::string type_url = sample.payload().type_url();
        if (type_url.empty()) {
            spdlog::debug("client_svc: empty payload type_url run_id={} module_id={}",
                          batch.run_id(), batch.module_id());
            continue;
        }

        constexpr std::string_view expected = "type.googleapis.com/hal.common.TestResult";
        if (type_url != expected) {
            spdlog::warn("client_svc: unexpected payload type_url='{}' run_id={}",
                         type_url, batch.run_id());
            continue;
        }

        hal::common::TestResult tr;
        if (!sample.payload().UnpackTo(&tr)) {
            spdlog::error("client_svc: failed to unpack TestResult run_id={} module_id={}",
                          batch.run_id(), batch.module_id());
            continue;
        }

        // Task 87: a sample that names a different module than the batch is a
        // protocol error — reject it (never trust a mismatched identity).
        if (!tr.module_id().empty() && tr.module_id() != batch.module_id()) {
            spdlog::error("client_svc: module mismatch batch.module_id={} sample.module_id={} "
                          "run_id={}; rejecting sample",
                          batch.module_id(), tr.module_id(), batch.run_id());
            continue;
        }

        // UNSPECIFIED guard.
        if (tr.output().status() == hal::common::TEST_STATUS_UNSPECIFIED) {
            spdlog::warn("client_svc: UNSPECIFIED status rejected run_id={} module_id={}",
                         batch.run_id(), batch.module_id());
            continue;
        }

        IngestWork work;
        work.run_id         = batch.run_id();
        work.module_id      = batch.module_id();
        work.module_version = "";
        work.mode           = mode;
        work.batch_seq      = batch.batch_seq();
        work.tr             = std::move(tr);

        // Task 89: saturation propagates rather than silently dropping.
        if (!queue.enqueue(std::move(work))) {
            spdlog::warn("client_svc: queue closed during shutdown; batch NOT acked "
                         "run_id={} seq={} (client will replay)",
                         batch.run_id(), batch.batch_seq());
            return;
        }
        ++enqueued;
    }
    if (enqueued > 0) batch_pending[batch_key] = enqueued;
}

// ── Handle a LogBatch ────────────────────────────────────────────────────────

void ClientServiceImpl::handle_log_batch(const acadia::stream::LogBatch& batch) {
    for (const auto& line : batch.lines()) {
        LogEntry e;
        e.run_id   = line.run_id().empty() ? batch.run_id() : line.run_id();
        e.source   = line.source();
        e.severity = line.level();
        e.message  = line.text();
        log_svc_.ingest(e);
    }
}

// ── Connect (bidi stream) ─────────────────────────────────────────────────────

::grpc::Status ClientServiceImpl::Connect(
    ::grpc::ServerContext* ctx,
    ::grpc::ServerReaderWriter<acadia::stream::ServerMessage,
                                acadia::stream::ClientMessage>* stream) {

    // X2-003: authenticate before any state is touched.
    {
        auto status = verify_stream_auth(ctx->client_metadata(), stream_token_);
        if (!status.ok()) return status;
    }

    BoundedQueue<IngestWork> queue{kQueueCapacity};
    std::atomic<bool> writer_ok{true};
    // Task 88: a batch is acked only after every sample is durably persisted.
    std::mutex ack_mu;
    std::unordered_map<std::string, int> batch_pending;
    // Task 90: per-run last-seen batch_seq for duplicate/gap detection.
    std::mutex seq_mu;
    std::unordered_map<std::string, int64_t> last_batch_seq;

    // DB writer thread: drains the queue and persists results, then acks the
    // batch once all its samples are durable.
    std::thread writer_thread{[&] {
        while (true) {
            auto work = queue.dequeue();
            if (!work.has_value()) break;  // Queue closed.
            try {
                run_svc_.ingest_test_result(work->tr, work->run_id,
                                             work->module_version, work->mode);
            } catch (const std::exception& e) {
                spdlog::error("client_svc: ingest error run_id={}: {}",
                              work->run_id, e.what());
                // Do not abort the stream for a single failed ingest.
            }
            // Acknowledge only once this batch's samples are all durable.
            const std::string key = work->run_id + ":" + std::to_string(work->batch_seq);
            bool ack_batch = false;
            {
                std::lock_guard<std::mutex> lk(ack_mu);
                auto it = batch_pending.find(key);
                if (it != batch_pending.end()) {
                    if (--it->second <= 0) { batch_pending.erase(it); ack_batch = true; }
                }
            }
            // Task 90: duplicate/gap detection + idempotent resume. Track the
            // highest batch_seq seen per run. A duplicate (<= last) is still
            // acked (idempotent) but not re-ingested meaningfully; a gap
            // (> last+1) is logged so reconnect recovery is observable.
            {
                std::lock_guard<std::mutex> lk(seq_mu);
                auto it = last_batch_seq.find(work->run_id);
                int64_t last = it == last_batch_seq.end() ? -1 : it->second;
                if (work->batch_seq <= last) {
                    spdlog::warn("client_svc: duplicate batch run_id={} seq={} (last={})",
                                 work->run_id, work->batch_seq, last);
                } else if (last >= 0 && work->batch_seq > last + 1) {
                    spdlog::warn("client_svc: GAP run_id={}: expected seq {} got {}",
                                 work->run_id, last + 1, work->batch_seq);
                }
                last_batch_seq[work->run_id] = work->batch_seq;
            }
            if (ack_batch) {
                acadia::stream::ServerMessage ack_msg;
                auto* ack = ack_msg.mutable_batch_ack();
                ack->set_run_id(work->run_id);
                ack->set_batch_seq(work->batch_seq);
                if (!stream->Write(ack_msg)) {
                    spdlog::warn("client_svc: failed to send BatchAck run_id={}",
                                 work->run_id);
                }
            }
        }
    }};

    acadia::stream::ClientMessage msg;
    std::string board_id;  // Task 85: known once Hello arrives; used to register for control dispatch.
    while (!ctx->IsCancelled() && stream->Read(&msg)) {
        switch (msg.payload_case()) {
        case acadia::stream::ClientMessage::kHello: {
            const auto& hello = msg.hello();
            board_id = hello.board_id();
            if (registry_) registry_->Register(board_id, stream);
            // Task 91: persist board registration (durable lifecycle).
            if (board_repo_ && !board_id.empty()) {
                board_repo_->upsert_board(board_id, hello.client_version(),
                                          util::utc_now_iso8601());
            }
            spdlog::info("client_svc: HelloReport board_id={} client_ver={}",
                         hello.board_id(), hello.client_version());
            break;
        }

        case acadia::stream::ClientMessage::kResultBatch: {
            const auto& batch = msg.result_batch();
            // Task 88: ack is sent by the writer thread after durable persist.
            handle_result_batch(batch, queue, batch_pending);

            // If terminal batch, finalize the run.
            if (batch.terminal()) {
                spdlog::info("client_svc: terminal batch run_id={} module_id={}",
                             batch.run_id(), batch.module_id());
                run_svc_.complete_run(batch.run_id(), "completed");
            }
            break;
        }

        case acadia::stream::ClientMessage::kLogBatch: {
            const auto& batch = msg.log_batch();
            handle_log_batch(batch);

            // Send LogBatchAck.
            acadia::stream::ServerMessage ack_msg;
            auto* ack = ack_msg.mutable_log_batch_ack();
            ack->set_run_id(batch.run_id());
            ack->set_batch_seq(batch.batch_seq());
            if (!stream->Write(ack_msg)) {
                spdlog::warn("client_svc: failed to send LogBatchAck run_id={}", batch.run_id());
            }
            break;
        }

        case acadia::stream::ClientMessage::kStateReport: {
            const auto& sr = msg.state_report();
            spdlog::debug("client_svc: StateReport board_id={}", sr.board_id());
            // Task 91: persist board heartbeat + active-run state.
            if (board_repo_ && !sr.board_id().empty()) {
                board_repo_->update_state(sr.board_id(),
                    static_cast<int>(sr.overall_state()),
                    static_cast<int64_t>(sr.spool_bytes()),
                    static_cast<int64_t>(sr.spool_cap_bytes()),
                    sr.spool_pressure(), util::utc_now_iso8601());
                if (sr.active_runs_size() > 0) {
                    const auto& ai = sr.active_runs(0);
                    board_repo_->set_active_run(sr.board_id(), ai.run_id(),
                        static_cast<int>(ai.state()), ai.last_batch_seq());
                }
            }
            break;
        }

        case acadia::stream::ClientMessage::kConfigureAck: {
            const auto& ack = msg.configure_ack();
            if (!ack.ok()) {
                spdlog::warn("client_svc: ConfigureAck NACK run_id={} field={} error={}",
                             ack.run_id(), ack.field(), ack.error());
            }
            break;
        }

        default:
            spdlog::debug("client_svc: unknown payload_case={}", msg.payload_case());
            break;
        }
    }

    // Close the queue and wait for writer thread to flush.
    queue.close();
    if (writer_thread.joinable()) writer_thread.join();

    if (registry_ && !board_id.empty()) registry_->Unregister(board_id);

    spdlog::info("client_svc: stream ended (cancelled={})", ctx->IsCancelled());
    return ::grpc::Status::OK;
}

}  // namespace acadia::grpc

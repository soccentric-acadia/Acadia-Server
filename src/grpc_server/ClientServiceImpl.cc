#include "grpc_server/ClientServiceImpl.h"
#include "util/Time.h"

#include "google/protobuf/any.pb.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

namespace acadia::grpc
{

// Queue capacity: 1024 work items.  Each item holds a single TestResult.
static constexpr std::size_t kQueueCapacity = 1024;

ClientServiceImpl::ClientServiceImpl(RunService& run_svc, LogService& log_svc, std::shared_ptr<ClientRegistry> registry, BoardRepository* board_repo,
                                     std::string stream_token)
    : run_svc_(run_svc), log_svc_(log_svc), registry_(std::move(registry)), board_repo_(board_repo), stream_token_(std::move(stream_token))
{
}

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
::grpc::Status verify_stream_auth(const std::multimap<::grpc::string_ref, ::grpc::string_ref>& metadata, const std::string& expected_token)
{
    if (expected_token.empty()) {
        // Dev/bench mode: no token configured — allow, but only surface the
        // warning once per process.
        static std::once_flag open_warn;
        std::call_once(open_warn, [] {
            spdlog::warn("ACADIA_STREAM_TOKEN not set: ClientService streams are UNAUTHENTICATED "
                         "(dev/bench mode — set it in production)");
        });
        return ::grpc::Status::OK;
    }
    auto it = metadata.find("x-acadia-stream-token");
    if (it == metadata.end()) {
        spdlog::warn("stream auth: missing x-acadia-stream-token metadata");
        return {::grpc::StatusCode::UNAUTHENTICATED, "missing x-acadia-stream-token metadata"};
    }
    if (it->second != expected_token) {
        spdlog::warn("stream auth: token mismatch");
        return {::grpc::StatusCode::UNAUTHENTICATED, "invalid stream token"};
    }
    return ::grpc::Status::OK;
}

// ── Ack helper (SRV-049) ──────────────────────────────────────────────────────

bool ClientServiceImpl::send_ack(const std::string& board_id, Stream* stream, const acadia::stream::ServerMessage& msg)
{
    if (registry_ && !board_id.empty())
        return registry_->SendControl(board_id, msg);
    // No board_id yet (shouldn't happen for results/logs, which follow
    // Hello) or no registry configured: nothing else can be writing to this
    // stream concurrently, so an unserialized Write is safe.
    return stream->Write(msg);
}

// ── Handle a TestResultBatch ──────────────────────────────────────────────────

void ClientServiceImpl::handle_result_batch(const acadia::stream::TestResultBatch& batch, BoundedQueue<IngestWork>& queue,
                                            std::unordered_map<std::string, int>& batch_pending, std::mutex& ack_mu,
                                            const std::string& board_id, Stream* stream)
{

    const std::string mode = std::to_string(static_cast<int>(batch.mode()));
    const std::string batch_key = batch.run_id() + ":" + std::to_string(batch.batch_seq());
    int enqueued = 0;

    // Task 87: validate module identity at ingestion. (Falls through to the
    // zero-enqueued immediate-ack path below instead of returning early —
    // SRV-046: a rejected batch still needs an ack, or the client's spool
    // never drains it.)
    if (batch.module_id().empty()) {
        spdlog::warn("client_svc: batch run_id={} has empty module_id; rejecting", batch.run_id());
    } else {
        for (const auto& sample : batch.samples()) {
            const std::string type_url = sample.payload().type_url();
            if (type_url.empty()) {
                spdlog::debug("client_svc: empty payload type_url run_id={} module_id={}", batch.run_id(), batch.module_id());
                continue;
            }

            constexpr std::string_view expected = "type.googleapis.com/hal.common.TestResult";
            if (type_url != expected) {
                spdlog::warn("client_svc: unexpected payload type_url='{}' run_id={}", type_url, batch.run_id());
                continue;
            }

            hal::common::TestResult tr;
            if (!sample.payload().UnpackTo(&tr)) {
                spdlog::error("client_svc: failed to unpack TestResult run_id={} module_id={}", batch.run_id(), batch.module_id());
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
                spdlog::warn("client_svc: UNSPECIFIED status rejected run_id={} module_id={}", batch.run_id(), batch.module_id());
                continue;
            }

            IngestWork work;
            work.run_id = batch.run_id();
            work.module_id = batch.module_id();
            work.module_version = "";
            work.mode = mode;
            work.batch_seq = batch.batch_seq();
            work.tr = std::move(tr);

            // SRV-045: register this sample's pending-ack slot *before*
            // enqueueing it, under the same mutex the writer thread
            // decrements under — otherwise the writer can dequeue and finish
            // this sample before the slot exists, and its ack is lost.
            {
                std::lock_guard<std::mutex> lk(ack_mu);
                ++batch_pending[batch_key];
            }
            // Task 89: saturation propagates rather than silently dropping.
            if (!queue.enqueue(std::move(work))) {
                {
                    std::lock_guard<std::mutex> lk(ack_mu);
                    auto it = batch_pending.find(batch_key);
                    if (it != batch_pending.end() && --it->second <= 0)
                        batch_pending.erase(it);
                }
                spdlog::warn("client_svc: queue closed during shutdown; batch NOT acked "
                             "run_id={} seq={} (client will replay)",
                             batch.run_id(), batch.batch_seq());
                return;
            }
            ++enqueued;
        }
    }

    if (enqueued == 0) {
        // SRV-046: nothing to wait on (rejected batch, keep-alive, synthetic
        // terminal batch with no samples, or every sample filtered) — ack
        // now instead of leaving the client waiting for an ack that the
        // writer thread will never have a reason to send.
        acadia::stream::ServerMessage ack_msg;
        auto* ack = ack_msg.mutable_batch_ack();
        ack->set_run_id(batch.run_id());
        ack->set_batch_seq(batch.batch_seq());
        if (!send_ack(board_id, stream, ack_msg)) {
            spdlog::warn("client_svc: failed to send empty-batch BatchAck run_id={}", batch.run_id());
        }
    }
}

// ── Terminal state mapping (SRV-050) ──────────────────────────────────────────

std::string terminal_run_state_to_status(acadia::stream::RunState state)
{
    switch (state) {
    case acadia::stream::COMPLETED:
        return "completed";
    case acadia::stream::CANCELLED:
        return "cancelled";
    case acadia::stream::FAILED:
        return "failed";
    default:
        // A terminal batch that doesn't carry a genuinely terminal RunState is
        // a protocol error, not a success — never default to "completed".
        spdlog::error("client_svc: terminal batch carried non-terminal state={}; treating as failed", static_cast<int>(state));
        return "failed";
    }
}

// ── Handle a LogBatch ────────────────────────────────────────────────────────

void ClientServiceImpl::handle_log_batch(const acadia::stream::LogBatch& batch)
{
    for (const auto& line : batch.lines()) {
        LogEntry e;
        e.run_id = line.run_id().empty() ? batch.run_id() : line.run_id();
        e.source = line.source();
        e.severity = line.level();
        e.message = line.text();
        log_svc_.ingest(e);
    }
}

// ── Connect (bidi stream) ─────────────────────────────────────────────────────

::grpc::Status ClientServiceImpl::Connect(::grpc::ServerContext* ctx,
                                          ::grpc::ServerReaderWriter<acadia::stream::ServerMessage, acadia::stream::ClientMessage>* stream)
{

    // X2-003: authenticate before any state is touched.
    {
        auto status = verify_stream_auth(ctx->client_metadata(), stream_token_);
        if (!status.ok())
            return status;
    }

    BoundedQueue<IngestWork> queue{kQueueCapacity};
    std::atomic<bool> writer_ok{true};
    // Task 88: a batch is acked only after every sample is durably persisted.
    std::mutex ack_mu;
    std::unordered_map<std::string, int> batch_pending;
    // Task 90 + SRV-044: batch-level dedupe state. last_batch_seq tracks the
    // highest batch_seq ingested per run (gap detection); seen_batches holds
    // the "run_id:seq" keys already ingested so a replayed batch is acked
    // without re-ingesting ANY of its samples. Checked ONCE per network
    // batch, on the reader thread, before any of its samples are enqueued —
    // not once per sample on the writer thread: every sample of one network
    // batch shares the same key, so a per-sample check on the writer thread
    // means only the first sample of any multi-sample batch is ever
    // ingested (the rest see the key already in `seen_batches`, inserted by
    // that first sample, and are silently treated as a replay). This was a
    // real bug, found while fixing SRV-045/046/047/049/050 — not previously
    // tracked under any SRV-0xx id.
    std::mutex seq_mu;
    std::unordered_map<std::string, int64_t> last_batch_seq;
    std::unordered_set<std::string> seen_batches;
    // SRV-050: the terminal batch's reported state, applied only after the
    // writer thread has drained (below), not the moment it arrives.
    std::optional<acadia::stream::RunState> terminal_state;
    std::string terminal_run_id;
    // Task 85: known once Hello arrives (reader thread), read by the writer
    // thread on every BatchAck (send_ack needs it to serialize with
    // SendControl — SRV-049) — a plain std::string would be a data race
    // between those two threads, so it's guarded by its own mutex rather
    // than folded into ack_mu/seq_mu, which guard unrelated state.
    std::mutex board_id_mu;
    std::string board_id;
    auto current_board_id = [&] {
        std::lock_guard<std::mutex> lk(board_id_mu);
        return board_id;
    };

    // DB writer thread: drains the queue and persists results, then acks the
    // batch once all its samples are durable.
    std::thread writer_thread{[&] {
        while (true) {
            auto work = queue.dequeue();
            if (!work.has_value())
                break; // Queue closed.

            const std::string key = work->run_id + ":" + std::to_string(work->batch_seq);

            try {
                run_svc_.ingest_test_result(work->tr, work->run_id, work->module_version, work->mode);
            }
            catch (const std::exception& e) {
                spdlog::error("client_svc: ingest error run_id={} seq={}: {} — ack withheld, client will retry",
                              work->run_id, work->batch_seq, e.what());
                // SRV-047: do NOT decrement/ack on a failed ingest. The
                // pending slot for this batch is left registered, so the
                // batch is never acked and the client's spool retries it —
                // matching the "withhold ack" fix the item asks for, at the
                // cost of leaking that one map entry for the life of this
                // connection if the batch is never fully retried before
                // disconnect (accepted: failures are expected to be rare,
                // and the alternative — acking a batch we failed to
                // persist — is silent data loss).
                continue;
            }
            // Acknowledge only once this batch's samples are all durable.
            bool ack_batch = false;
            {
                std::lock_guard<std::mutex> lk(ack_mu);
                auto it = batch_pending.find(key);
                if (it != batch_pending.end()) {
                    if (--it->second <= 0) {
                        batch_pending.erase(it);
                        ack_batch = true;
                    }
                }
            }
            if (ack_batch) {
                acadia::stream::ServerMessage ack_msg;
                auto* ack = ack_msg.mutable_batch_ack();
                ack->set_run_id(work->run_id);
                ack->set_batch_seq(work->batch_seq);
                if (!send_ack(current_board_id(), stream, ack_msg)) {
                    spdlog::warn("client_svc: failed to send BatchAck run_id={}", work->run_id);
                }
            }
        }
    }};

    acadia::stream::ClientMessage msg;
    while (!ctx->IsCancelled() && stream->Read(&msg)) {
        switch (msg.payload_case()) {
        case acadia::stream::ClientMessage::kHello: {
            const auto& hello = msg.hello();
            {
                std::lock_guard<std::mutex> lk(board_id_mu);
                board_id = hello.board_id();
            }
            if (registry_)
                registry_->Register(board_id, stream);
            // Task 91: persist board registration (durable lifecycle).
            if (board_repo_ && !board_id.empty()) {
                board_repo_->upsert_board(board_id, hello.client_version(), util::utc_now_iso8601());
            }
            spdlog::info("client_svc: HelloReport board_id={} client_ver={}", hello.board_id(), hello.client_version());
            break;
        }

        case acadia::stream::ClientMessage::kResultBatch: {
            const auto& batch = msg.result_batch();
            const std::string batch_key = batch.run_id() + ":" + std::to_string(batch.batch_seq());

            bool duplicate_batch;
            {
                std::lock_guard<std::mutex> lk(seq_mu);
                duplicate_batch = seen_batches.count(batch_key) > 0;
                if (!duplicate_batch) {
                    seen_batches.insert(batch_key);
                    auto it = last_batch_seq.find(batch.run_id());
                    int64_t last = it == last_batch_seq.end() ? -1 : it->second;
                    if (last >= 0 && batch.batch_seq() > last + 1) {
                        spdlog::warn("client_svc: GAP run_id={}: expected seq {} got {}", batch.run_id(), last + 1, batch.batch_seq());
                    }
                    last_batch_seq[batch.run_id()] = std::max(last, batch.batch_seq());
                }
            }

            if (duplicate_batch) {
                spdlog::warn("client_svc: duplicate batch run_id={} seq={} — acked without re-ingest", batch.run_id(), batch.batch_seq());
                acadia::stream::ServerMessage ack_msg;
                auto* ack = ack_msg.mutable_batch_ack();
                ack->set_run_id(batch.run_id());
                ack->set_batch_seq(batch.batch_seq());
                if (!send_ack(board_id, stream, ack_msg)) {
                    spdlog::warn("client_svc: failed to send duplicate BatchAck run_id={}", batch.run_id());
                }
            } else {
                // Task 88: ack (for enqueued samples) is sent by the writer
                // thread after durable persist; handle_result_batch itself
                // acks immediately only the zero-enqueued case (SRV-046).
                handle_result_batch(batch, queue, batch_pending, ack_mu, board_id, stream);
            }

            // SRV-050: record the terminal state; apply it after the writer
            // thread drains (see below), not now — samples from this and
            // earlier batches may still be queued/in flight, and the
            // reported state (COMPLETED/FAILED/CANCELLED) must be honored,
            // not overwritten with an unconditional "completed".
            if (batch.terminal()) {
                spdlog::info("client_svc: terminal batch run_id={} module_id={} state={}", batch.run_id(), batch.module_id(),
                            static_cast<int>(batch.state()));
                terminal_state = batch.state();
                terminal_run_id = batch.run_id();
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
            if (!send_ack(board_id, stream, ack_msg)) {
                spdlog::warn("client_svc: failed to send LogBatchAck run_id={}", batch.run_id());
            }
            break;
        }

        case acadia::stream::ClientMessage::kStateReport: {
            const auto& sr = msg.state_report();
            spdlog::debug("client_svc: StateReport board_id={}", sr.board_id());
            // Task 91: persist board heartbeat + active-run state.
            if (board_repo_ && !sr.board_id().empty()) {
                // StateReport (client_service.proto) has no spool_pressure field --
                // derive the fill ratio from spool_bytes/spool_cap_bytes instead
                // of reading a field that has never existed on the wire.
                const double pressure = sr.spool_cap_bytes() > 0
                    ? static_cast<double>(sr.spool_bytes()) / static_cast<double>(sr.spool_cap_bytes())
                    : 0.0;
                board_repo_->update_state(sr.board_id(), static_cast<int>(sr.overall_state()), static_cast<int64_t>(sr.spool_bytes()),
                                          static_cast<int64_t>(sr.spool_cap_bytes()), pressure, util::utc_now_iso8601());
                if (sr.active_runs_size() > 0) {
                    const auto& ai = sr.active_runs(0);
                    board_repo_->set_active_run(sr.board_id(), ai.run_id(), static_cast<int>(ai.state()), ai.last_batch_seq());
                }
            }
            break;
        }

        case acadia::stream::ClientMessage::kConfigureAck: {
            const auto& ack = msg.configure_ack();
            if (!ack.ok()) {
                spdlog::warn("client_svc: ConfigureAck NACK run_id={} field={} error={}", ack.run_id(), ack.field(), ack.error());
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
    if (writer_thread.joinable())
        writer_thread.join();

    // SRV-050: finalize only after every queued sample (including this
    // batch's own, and any earlier batch's) has been drained by the writer
    // thread above, and with the state the client actually reported.
    if (terminal_state.has_value()) {
        run_svc_.complete_run(terminal_run_id, terminal_run_state_to_status(*terminal_state));
    }

    if (registry_ && !board_id.empty())
        registry_->Unregister(board_id);

    spdlog::info("client_svc: stream ended (cancelled={})", ctx->IsCancelled());
    return ::grpc::Status::OK;
}

} // namespace acadia::grpc

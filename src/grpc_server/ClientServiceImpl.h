#pragma once
// grpc_server/ClientServiceImpl.h — implements the acadia::stream::ClientService bidi stream.
//
// The Client (on-target device) sends ClientMessage frames over a single persistent stream.
// This implementation:
//   - Processes TestResultBatch frames → extracts ResultSamples → unpacks each Any payload
//     as hal::common::TestResult and calls RunService::ingest_test_result.
//   - Processes LogBatch frames → forwards to LogService.
//   - Processes HelloReport → registers the board.
//   - Sends BatchAck / LogBatchAck responses.
//   - Applies BoundedQueue backpressure: the DB write happens on a dedicated writer thread;
//     the reader thread enqueues work items and blocks only when the queue is full.

#include "grpc_server/ClientRegistry.h"
#include "grpc_server/StreamBackpressure.h"
#include "repositories/BoardRepository.h"
#include "services/LogService.h"
#include "services/RunService.h"

#include "client_service.grpc.pb.h"
#include "client_service.pb.h"
#include "common.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace acadia::grpc
{

class ClientServiceImpl final : public acadia::stream::ClientService::Service
{
  public:
    ClientServiceImpl(RunService& run_svc, LogService& log_svc, std::shared_ptr<ClientRegistry> registry, BoardRepository* board_repo,
                      std::string stream_token = {});

    ::grpc::Status Connect(::grpc::ServerContext* ctx,
                           ::grpc::ServerReaderWriter<acadia::stream::ServerMessage, acadia::stream::ClientMessage>* stream) override;

  private:
    struct IngestWork
    {
        std::string run_id;
        std::string module_id;
        std::string module_version;
        std::string mode;
        int64_t batch_seq = 0; // Task 88: ack after this batch's samples persist
        hal::common::TestResult tr;
    };

    RunService& run_svc_;
    LogService& log_svc_;
    std::shared_ptr<ClientRegistry> registry_; // Task 85: control dispatch
    BoardRepository* board_repo_ = nullptr;
    std::string stream_token_; // X2-003: required stream metadata token
    // Task 91: board lifecycle persistence

    using Stream = ::grpc::ServerReaderWriter<acadia::stream::ServerMessage, acadia::stream::ClientMessage>;

    // Process a single (non-duplicate) TestResultBatch: validates and enqueues
    // each sample for DB write, registering each sample's pending-ack slot
    // under `ack_mu` *before* it is enqueued (SRV-045 — the writer thread must
    // never be able to decrement a slot that doesn't exist yet). If the batch
    // ends up with zero enqueued samples (invalid module_id, or every sample
    // filtered — keep-alives, synthetic terminal batches, client error
    // batches), acks immediately instead of leaving the client waiting for an
    // ack that will never come (SRV-046).
    void handle_result_batch(const acadia::stream::TestResultBatch& batch, BoundedQueue<IngestWork>& queue,
                             std::unordered_map<std::string, int>& batch_pending, std::mutex& ack_mu,
                             const std::string& board_id, Stream* stream);

    // Process a single LogBatch.
    void handle_log_batch(const acadia::stream::LogBatch& batch);

    // SRV-049: every response on this stream (BatchAck, LogBatchAck) goes
    // through here instead of calling stream->Write() directly, so it
    // serializes with SendControl through ClientRegistry's single mutex (see
    // that class for why) whenever a board_id is known. Before Hello (no
    // board_id yet) or with no registry configured, falls back to a direct,
    // unserialized Write — there is no concurrent SendControl target to race
    // with in either case.
    bool send_ack(const std::string& board_id, Stream* stream, const acadia::stream::ServerMessage& msg);
};

// Pure mapping from the wire RunState (client_service.proto) enum, as carried
// by a terminal TestResultBatch, to the RunService status string. Exposed at
// namespace scope for unit tests. An unexpected/non-terminal state maps to
// "failed" rather than silently claiming "completed" (SRV-050).
std::string terminal_run_state_to_status(acadia::stream::RunState state);

// X2-003: pure-function stream auth, exposed at namespace scope for unit
// tests. Defined in ClientServiceImpl.cc.
::grpc::Status verify_stream_auth(const std::multimap<::grpc::string_ref, ::grpc::string_ref>& metadata, const std::string& expected_token);

} // namespace acadia::grpc

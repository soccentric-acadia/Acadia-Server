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

#include "services/RunService.h"
#include "services/LogService.h"
#include "grpc_server/StreamBackpressure.h"
#include "grpc_server/ClientRegistry.h"
#include "repositories/BoardRepository.h"

#include "client_service.grpc.pb.h"
#include "client_service.pb.h"
#include "common.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace acadia::grpc {

class ClientServiceImpl final
    : public acadia::stream::ClientService::Service {
public:
    ClientServiceImpl(RunService& run_svc, LogService& log_svc,
                      std::shared_ptr<ClientRegistry> registry,
                      BoardRepository* board_repo,
                      std::string stream_token = {});

    ::grpc::Status Connect(
        ::grpc::ServerContext*                                                    ctx,
        ::grpc::ServerReaderWriter<acadia::stream::ServerMessage,
                                   acadia::stream::ClientMessage>* stream) override;

private:
    struct IngestWork {
        std::string              run_id;
        std::string              module_id;
        std::string              module_version;
        std::string              mode;
        int64_t                  batch_seq = 0;   // Task 88: ack after this batch's samples persist
        hal::common::TestResult  tr;
    };

    RunService& run_svc_;
    LogService& log_svc_;
    std::shared_ptr<ClientRegistry> registry_;  // Task 85: control dispatch
    BoardRepository* board_repo_ = nullptr;
    std::string                 stream_token_;   // X2-003: required stream metadata token
    // Task 91: board lifecycle persistence

    // Process a single TestResultBatch, enqueue each sample for DB write.
    // Tracks per-batch pending counts so the writer acks only after durable
    // persist (tasks 87/88/89).
    void handle_result_batch(
        const acadia::stream::TestResultBatch& batch,
        BoundedQueue<IngestWork>&              queue,
        std::unordered_map<std::string, int>&  batch_pending);

    // Process a single LogBatch.
    void handle_log_batch(const acadia::stream::LogBatch& batch);
};

// X2-003: pure-function stream auth, exposed at namespace scope for unit
// tests. Defined in ClientServiceImpl.cc.
::grpc::Status verify_stream_auth(
    const std::multimap<::grpc::string_ref, ::grpc::string_ref>& metadata,
    const std::string& expected_token);

}  // namespace acadia::grpc

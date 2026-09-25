#pragma once
// grpc_server/ClientRegistry.h — tracks active client (board) connections and
// dispatches Server→Client control messages (task 85). The ClientService Connect
// handler registers its stream here; Run/Hal orchestration uses SendControl to
// route configure/start/pause/resume/stop/restart to the right board.

#include "client_service.grpc.pb.h"
#include "client_service.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace acadia::grpc
{

class ClientRegistry
{
  public:
    using Stream = ::grpc::ServerReaderWriter<acadia::stream::ServerMessage, acadia::stream::ClientMessage>;

    void Register(const std::string& board_id, Stream* stream);
    void Unregister(const std::string& board_id);
    bool Has(const std::string& board_id) const;

    // Board ids with a live control stream (RunDispatcher target selection).
    std::vector<std::string> connected() const;

    // Send a control message to a board. Returns true if dispatched (a client
    // for that board was connected and the write succeeded).
    //
    // SRV-049: a sync gRPC ServerReaderWriter forbids concurrent Write() calls
    // on the same stream. `mu_` is held for the whole call (including the
    // blocking Write), not just the map lookup, specifically so this is the
    // single serialization point for that stream: the Connect handler's own
    // acks (BatchAck, LogBatchAck) are routed through this same method
    // instead of calling stream->Write() directly, so all three writer paths
    // (Connect's reader thread, its writer thread, and orchestration threads
    // calling SendControl) can never race. This does mean a slow/blocked
    // Write to one board holds `mu_` and stalls lookups/dispatch for other
    // boards for its duration — an accepted tradeoff for correctness over
    // the alternative of a second, independently-lifetimed mutex per stream.
    bool SendControl(const std::string& board_id, acadia::stream::ServerMessage msg);

  private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, Stream*> streams_;
};

} // namespace acadia::grpc

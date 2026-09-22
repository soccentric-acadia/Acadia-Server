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
    bool SendControl(const std::string& board_id, acadia::stream::ServerMessage msg);

  private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, Stream*> streams_;
};

} // namespace acadia::grpc

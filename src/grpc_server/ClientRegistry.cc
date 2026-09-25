#include "grpc_server/ClientRegistry.h"

#include <spdlog/spdlog.h>

namespace acadia::grpc
{

void ClientRegistry::Register(const std::string& board_id, Stream* stream)
{
    std::lock_guard<std::mutex> lk(mu_);
    streams_[board_id] = stream;
    spdlog::info("client_registry: registered board_id={}", board_id);
}

void ClientRegistry::Unregister(const std::string& board_id)
{
    std::lock_guard<std::mutex> lk(mu_);
    streams_.erase(board_id);
}

bool ClientRegistry::Has(const std::string& board_id) const
{
    std::lock_guard<std::mutex> lk(mu_);
    return streams_.find(board_id) != streams_.end();
}

std::vector<std::string> ClientRegistry::connected() const
{
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    out.reserve(streams_.size());
    for (const auto& [id, stream] : streams_) (void)stream, out.push_back(id);
    return out;
}

bool ClientRegistry::SendControl(const std::string& board_id, acadia::stream::ServerMessage msg)
{
    // mu_ is held across the Write call itself (see the header comment on
    // this method) — this is the single serialization point for the stream.
    std::lock_guard<std::mutex> lk(mu_);
    auto it = streams_.find(board_id);
    if (it == streams_.end()) {
        spdlog::warn("client_registry: no connected client for board_id={}", board_id);
        return false;
    }
    return it->second->Write(msg);
}

} // namespace acadia::grpc

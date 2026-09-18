#pragma once
// orchestration/OtaOrchestrator.h — polls for pending OTA upgrades and drives them.
#include "grpc_server/ClientRegistry.h"
#include "services/OtaService.h"
#include <atomic>
#include <string>
#include <thread>

namespace acadia::orchestration
{

class OtaOrchestrator
{
  public:
    OtaOrchestrator(OtaService& ota_svc, std::shared_ptr<acadia::grpc::ClientRegistry> registry);
    ~OtaOrchestrator();

    void start();
    void stop();

  private:
    void loop();
    void process(const OtaUpgrade& u);

    OtaService& ota_svc_;
    std::shared_ptr<acadia::grpc::ClientRegistry> registry_; // Task 97: real dispatch to board
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace acadia::orchestration

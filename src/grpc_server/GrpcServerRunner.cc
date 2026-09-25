#include "grpc_server/GrpcServerRunner.h"

#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace acadia::grpc
{

namespace
{
// VB-8/SRV-021: fail loudly on a configured-but-unreadable path, the same
// way ACADIA_SESSION_SECRET's length check does — a TLS cert/key that
// silently fails to load and falls back to plaintext would be a much worse
// outcome than refusing to start.
std::string read_pem_file(const std::string& path, const std::string& what)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("failed to open " + what + " at '" + path + "'");
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::shared_ptr<::grpc::ServerCredentials> build_credentials(const NetworkSettings& net)
{
    const bool cert_set = !net.grpc_tls_cert_pem_path.empty();
    const bool key_set = !net.grpc_tls_key_pem_path.empty();
    if (!cert_set && !key_set) {
        static std::once_flag warn;
        std::call_once(warn, [] {
            spdlog::warn("ACADIA_GRPC_TLS_CERT_PATH/ACADIA_GRPC_TLS_KEY_PATH not set: "
                         "gRPC is PLAINTEXT (dev/bench mode — set them in production)");
        });
        return ::grpc::InsecureServerCredentials();
    }
    if (cert_set != key_set) {
        throw std::runtime_error("ACADIA_GRPC_TLS_CERT_PATH and ACADIA_GRPC_TLS_KEY_PATH must be set together");
    }

    ::grpc::SslServerCredentialsOptions opts(
        net.grpc_tls_ca_pem_path.empty() ? GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE
                                         : GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    ::grpc::SslServerCredentialsOptions::PemKeyCertPair pair;
    pair.private_key = read_pem_file(net.grpc_tls_key_pem_path, "gRPC TLS private key");
    pair.cert_chain = read_pem_file(net.grpc_tls_cert_pem_path, "gRPC TLS certificate");
    opts.pem_key_cert_pairs.push_back(pair);
    if (!net.grpc_tls_ca_pem_path.empty()) {
        // mTLS: verify client certificates against this CA.
        opts.pem_root_certs = read_pem_file(net.grpc_tls_ca_pem_path, "gRPC TLS client CA");
        spdlog::info("gRPC TLS enabled with client certificate verification (mTLS)");
    } else {
        spdlog::info("gRPC TLS enabled (server-only; no client certificate required)");
    }
    return ::grpc::SslServerCredentials(opts);
}
} // namespace

GrpcServerRunner::GrpcServerRunner(HalServiceImpl& hal_service, ClientServiceImpl& client_service, const NetworkSettings& net)
    : hal_service_(hal_service), client_service_(client_service), net_(net)
{
}

void GrpcServerRunner::start()
{
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(net_.grpc_listen, build_credentials(net_));
    builder.RegisterService(&hal_service_);
    builder.RegisterService(&client_service_);

    // Reuse port, keepalive tuning.
    builder.SetMaxReceiveMessageSize(32 * 1024 * 1024); // 32 MiB
    builder.SetMaxSendMessageSize(32 * 1024 * 1024);

    server_ = builder.BuildAndStart();
    if (!server_) {
        throw std::runtime_error("Failed to start gRPC server on " + net_.grpc_listen);
    }
    spdlog::info("gRPC server listening on {} (HalService + ClientService)", net_.grpc_listen);
    server_->Wait();
}

void GrpcServerRunner::shutdown()
{
    if (server_) {
        spdlog::info("gRPC server shutting down");
        server_->Shutdown();
    }
}

} // namespace acadia::grpc

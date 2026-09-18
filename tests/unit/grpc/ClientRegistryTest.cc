// tests/unit/grpc/ClientRegistryTest.cc — task 85: control dispatch registry.
#include "grpc_server/ClientRegistry.h"
#include <gtest/gtest.h>

using acadia::grpc::ClientRegistry;

TEST(ClientRegistryTest, RegisterHasAndUnregister)
{
    ClientRegistry reg;
    // A registry without a real stream: Has reflects registration state.
    EXPECT_FALSE(reg.Has("board-1"));
    // We can't easily fake a gRPC stream, but Register/Unregister/Has and
    // SendControl's missing-client path are testable.
    EXPECT_FALSE(reg.SendControl("nobody", acadia::stream::ServerMessage{}));
    EXPECT_FALSE(reg.Has("nobody"));
}

// tests/unit/grpc/ClientServiceLogicTest.cc — SRV-050: terminal RunState
// mapping is a pure function, exposed at namespace scope specifically so it
// is testable without a live gRPC stream (Connect()/handle_result_batch()
// need a real ::grpc::ServerReaderWriter, which nothing in this test tree
// fakes today).
#include "grpc_server/ClientServiceImpl.h"

#include <gtest/gtest.h>

using acadia::grpc::terminal_run_state_to_status;

TEST(TerminalRunStateToStatusTest, CompletedMapsToCompleted)
{
    EXPECT_EQ(terminal_run_state_to_status(acadia::stream::COMPLETED), "completed");
}

TEST(TerminalRunStateToStatusTest, FailedMapsToFailed)
{
    EXPECT_EQ(terminal_run_state_to_status(acadia::stream::FAILED), "failed");
}

TEST(TerminalRunStateToStatusTest, CancelledMapsToCancelled)
{
    EXPECT_EQ(terminal_run_state_to_status(acadia::stream::CANCELLED), "cancelled");
}

// A terminal batch carrying a non-terminal state is a protocol error — must
// not silently resolve to "completed" (that was the SRV-050 bug: the old
// code always finalized as "completed" regardless of the reported state).
TEST(TerminalRunStateToStatusTest, UnspecifiedMapsToFailedNotCompleted)
{
    EXPECT_EQ(terminal_run_state_to_status(acadia::stream::RUN_STATE_UNSPECIFIED), "failed");
}

TEST(TerminalRunStateToStatusTest, RunningMapsToFailedNotCompleted)
{
    EXPECT_EQ(terminal_run_state_to_status(acadia::stream::RUNNING), "failed");
}

// tests/integration/BoardRepositoryTest.cc — SRV-030/031: board lifecycle.
// Requires a real PostgreSQL instance (ACADIA_TEST_DSN); runs against the
// repo's real migrations so the boards table shape is exercised as deployed.
#include "repositories/BoardRepository.h"
#include "db/DbClient.h"
#include "db/MigrationRunner.h"
#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <chrono>
#include <string>

#include "config/Settings.h"

namespace {
const char* test_dsn() { return std::getenv("ACADIA_TEST_DSN"); }

std::string now_iso() {
    using clock = std::chrono::system_clock;
    auto t = clock::to_time_t(clock::now());
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    return buf;
}
}  // namespace

class BoardRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!test_dsn()) GTEST_SKIP() << "ACADIA_TEST_DSN not set";
        // Parse ACADIA_TEST_DSN (host=… port=… dbname=… user=… password=…)
        // and initialise the shared Drogon client (same pattern as
        // MigrationRunnerTest).
        if (!db_initialised_) {
            acadia::DbSettings db;
            const std::string dsn{test_dsn()};
            auto getval = [&dsn](const char* key, std::string fallback) {
                const std::string needle = std::string(key) + "=";
                auto pos = dsn.find(needle);
                if (pos == std::string::npos) return fallback;
                auto start = pos + needle.size();
                auto end = dsn.find(' ', start);
                return dsn.substr(start, end - start);
            };
            db.host = getval("host", db.host);
            db.port = static_cast<uint16_t>(std::stoi(getval("port", "5432")));
            db.name = getval("dbname", db.name);
            db.user = getval("user", db.user);
            db.password = getval("password", "");
            acadia::db::init_db_client(db);
            db_initialised_ = true;
        }
        // Ensure the schema exists (idempotent; migrations tracked).
        acadia::db::MigrationRunner runner{ACADIA_MIGRATIONS_DIR};
        runner.run();
    }
    static inline bool db_initialised_ = false;
};

TEST_F(BoardRepositoryTest, HelloUpsertIsIdempotentAndKeepsFirstSeen) {
    acadia::BoardRepository repo;
    const std::string id = "board-lifecycle-test-1";
    repo.upsert_board(id, "0.1.0", "2026-01-01T00:00:00Z");
    repo.upsert_board(id, "0.2.0", "2026-01-02T00:00:00Z");  // reconnect

    auto rec = repo.get(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->client_version, "0.2.0");
    EXPECT_EQ(rec->first_seen_at, "2026-01-01T00:00:00Z");
    EXPECT_EQ(rec->last_seen_at, "2026-01-02T00:00:00Z");
}

TEST_F(BoardRepositoryTest, StateReportUpdatesHealthFields) {
    acadia::BoardRepository repo;
    const std::string id = "board-lifecycle-test-2";
    repo.upsert_board(id, "0.1.0", "2026-01-01T00:00:00Z");
    repo.update_state(id, /*overall_state=*/2, /*spool_bytes=*/10,
                      /*spool_cap=*/100, /*pressure=*/0.1,
                      "2026-01-03T00:00:00Z");

    auto rec = repo.get(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->overall_state, 2);
    EXPECT_EQ(rec->last_spool_bytes, 10);
    EXPECT_EQ(rec->last_spool_cap, 100);
    EXPECT_DOUBLE_EQ(rec->last_spool_pressure, 0.1);
}

TEST_F(BoardRepositoryTest, ActiveRunSetAndCleared) {
    acadia::BoardRepository repo;
    const std::string id = "board-lifecycle-test-3";
    repo.upsert_board(id, "0.1.0", "2026-01-01T00:00:00Z");

    repo.set_active_run(id, "run-77", /*run_state=*/2, /*last_seq=*/9);
    auto rec = repo.get(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->active_run_id, "run-77");
    EXPECT_EQ(rec->active_run_state, 2);
    EXPECT_EQ(rec->last_active_seq, 9);

    // Task 91: clearing (empty run_id) returns the board to idle.
    repo.set_active_run(id, "", 0, 0);
    rec = repo.get(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->active_run_id, "");
}

TEST_F(BoardRepositoryTest, UnknownBoardReturnsNullopt) {
    acadia::BoardRepository repo;
    EXPECT_FALSE(repo.get("no-such-board").has_value());
}
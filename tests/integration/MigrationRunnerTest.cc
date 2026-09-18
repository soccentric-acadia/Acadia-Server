// tests/integration/MigrationRunnerTest.cc
// Requires a real PostgreSQL instance.  Set ACADIA_TEST_DSN env var.
#include "db/MigrationRunner.h"
#include "config/Settings.h"
#include "db/DbClient.h"
#include <cstdlib>
#include <drogon/drogon.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

// Skip integration test if ACADIA_TEST_DSN is not set.
static const char* test_dsn()
{
    return std::getenv("ACADIA_TEST_DSN");
}

#include "db/DbClient.h"

class MigrationRunnerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!test_dsn())
            GTEST_SKIP() << "ACADIA_TEST_DSN not set";
        // Initialise the shared DbClient from ACADIA_TEST_DSN
        // (host=… port=… dbname=… user=… password=…).
        if (!db_initialised_) {
            acadia::DbSettings db;
            const std::string dsn{test_dsn()};
            auto getval = [&dsn](const char* key, std::string fallback) {
                const std::string needle = std::string(key) + "=";
                auto pos = dsn.find(needle);
                if (pos == std::string::npos)
                    return fallback;
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
        // Create a temp migrations dir with a single migration.
        migrations_dir_ = std::filesystem::temp_directory_path() / "acadia_test_migrations";
        std::filesystem::create_directories(migrations_dir_);
        {
            std::ofstream f{migrations_dir_ / "0001_test.sql"};
            f << "BEGIN;\n"
              << "CREATE TABLE IF NOT EXISTS _test_marker (id SERIAL PRIMARY KEY);\n"
              << "COMMIT;\n";
        }
    }

    void TearDown() override
    {
        std::filesystem::remove_all(migrations_dir_);
    }

    std::filesystem::path migrations_dir_;
    static inline bool db_initialised_ = false;
};

TEST_F(MigrationRunnerTest, RunsMigrationsOnce)
{
    acadia::db::MigrationRunner runner{migrations_dir_.string()};
    EXPECT_NO_THROW(runner.run());
    // Second run should be idempotent.
    EXPECT_NO_THROW(runner.run());
}

// SRV-020: the REAL migrations tree (0001..NNNN) must apply cleanly to a
// fresh database and be idempotent on a second run. Uses the repo's own
// migrations directory, injected by CMake as ACADIA_MIGRATIONS_DIR.
TEST_F(MigrationRunnerTest, RealMigrationsApplyAndAreIdempotent)
{
    if (!test_dsn())
        GTEST_SKIP() << "ACADIA_TEST_DSN not set";
#ifdef ACADIA_MIGRATIONS_DIR
    const std::filesystem::path real_dir{ACADIA_MIGRATIONS_DIR};
#else
    GTEST_SKIP() << "ACADIA_MIGRATIONS_DIR not defined";
#endif
    ASSERT_TRUE(std::filesystem::exists(real_dir));

    // Count migration files.
    std::size_t expected = 0;
    for (const auto& e : std::filesystem::directory_iterator(real_dir))
        if (e.is_regular_file() && e.path().extension() == ".sql")
            ++expected;
    ASSERT_GT(expected, 0u);

    acadia::db::MigrationRunner runner{real_dir.string()};
    EXPECT_NO_THROW(runner.run());

    auto db = acadia::db::get_db_client();
    auto before = db->execSqlSync("SELECT count(*) AS c FROM schema_migrations");
    const auto applied_after_first = before[0]["c"].as<std::size_t>();
    EXPECT_GE(applied_after_first, expected) << "every migration file should be recorded (fresh DB) or already tracked";

    // Second run: idempotent, version count unchanged.
    EXPECT_NO_THROW(runner.run());
    auto after = db->execSqlSync("SELECT count(*) AS c FROM schema_migrations");
    EXPECT_EQ(after[0]["c"].as<std::size_t>(), applied_after_first) << "re-running migrations must not add versions";
}

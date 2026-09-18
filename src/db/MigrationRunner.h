#pragma once
// MigrationRunner — applies SQL migration files from migrations/ at startup.
// Migrations are applied in lexicographic order of filename.
// Idempotent: already-applied versions tracked in schema_migrations table.
// Aborts startup with std::runtime_error on any failure.

#include <string>
#include <vector>

namespace acadia::db
{

class MigrationRunner
{
  public:
    // migrations_dir: path to the directory containing *.sql migration files.
    explicit MigrationRunner(std::string migrations_dir);

    // Run all pending migrations against the provided Drogon DbClient.
    // Throws std::runtime_error if any migration fails.
    void run();

    // Split SQL into top-level statements, respecting quotes and dollar-quoted
    // bodies. Statements are returned WITHOUT trailing semicolons (task 95).
    static std::vector<std::string> split_statements(const std::string& sql);

  private:
    // Task 95: apply under an advisory lock (concurrency-safe), then each
    // migration runs atomically.
    void run_locked();

    std::string migrations_dir_;
};

} // namespace acadia::db

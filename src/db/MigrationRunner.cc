#include "db/MigrationRunner.h"
#include "db/DbClient.h"

#include <drogon/orm/DbClient.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace acadia::db
{

namespace fs = std::filesystem;

MigrationRunner::MigrationRunner(std::string migrations_dir) : migrations_dir_(std::move(migrations_dir))
{
}

void MigrationRunner::run()
{
    auto db = get_db_client();

    // Task 95: advisory lock so concurrent Server startups do not race on
    // applying the same migration twice.
    db->execSqlSync("SELECT pg_advisory_lock(87391)");
    try {
        run_locked();
    }
    catch (...) {
        try {
            db->execSqlSync("SELECT pg_advisory_unlock(87391)");
        }
        catch (...) {
        }
        throw;
    }
    try {
        db->execSqlSync("SELECT pg_advisory_unlock(87391)");
    }
    catch (...) {
    }
}

static std::string to_upper_ascii(const std::string& in);
static std::string trim_statement(std::string s);

void MigrationRunner::run_locked()
{
    auto db = get_db_client();

    // Ensure the migrations tracking table exists.
    {
        auto result = db->execSqlSync("CREATE TABLE IF NOT EXISTS schema_migrations ("
                                      "  version    TEXT        NOT NULL PRIMARY KEY,"
                                      "  applied_at TIMESTAMPTZ NOT NULL DEFAULT NOW()"
                                      ")");
        (void)result;
    }

    // Fetch already-applied versions.
    std::set<std::string> applied;
    {
        auto rows = db->execSqlSync("SELECT version FROM schema_migrations ORDER BY version");
        for (const auto& row : rows) {
            applied.insert(row["version"].as<std::string>());
        }
    }

    // Collect and sort migration files.
    std::vector<fs::path> files;
    if (!fs::exists(migrations_dir_)) {
        spdlog::warn("MigrationRunner: migrations directory '{}' not found — skipping", migrations_dir_);
        return;
    }
    for (const auto& entry : fs::directory_iterator(migrations_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".sql") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    int applied_count = 0;
    for (const auto& file : files) {
        std::string version = file.stem().string();
        if (applied.count(version))
            continue;

        std::ifstream f{file};
        if (!f.is_open()) {
            throw std::runtime_error("MigrationRunner: cannot open migration file: " + file.string());
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string sql = ss.str();

        spdlog::info("MigrationRunner: applying {}", version);
        // Task 95: run each migration inside ONE transaction so the version is
        // recorded atomically with its DDL — a partial migration never leaves
        // the row unrecorded (and never falsely marks it applied).
        // SRV-020: migration files may carry their own BEGIN/COMMIT (psql
        // convention). Inside the runner's transaction those are nested and
        // break atomicity (the inner COMMIT releases the transaction early),
        // so the runner strips them and owns the transaction boundary.
        db->execSqlSync("BEGIN");
        try {
            for (const std::string& s : split_statements(sql)) {
                if (s.empty())
                    continue;
                const std::string upper = to_upper_ascii(s);
                if (upper == "BEGIN" || upper == "COMMIT" || upper == "ROLLBACK" || upper == "END" ||
                    upper.rfind("START TRANSACTION", 0) == 0) {
                    continue; // runner owns the transaction boundary
                }
                auto res = db->execSqlSync(s);
                (void)res;
            }
            db->execSqlSync("INSERT INTO schema_migrations (version) VALUES ($1) "
                            "ON CONFLICT (version) DO NOTHING",
                            version);
            db->execSqlSync("COMMIT");
        }
        catch (const std::exception& e) {
            try {
                db->execSqlSync("ROLLBACK");
            }
            catch (...) {
            }
            throw std::runtime_error("MigrationRunner: migration " + version + " failed: " + e.what());
        }
        ++applied_count;
        spdlog::info("MigrationRunner: applied {}", version);
    }

    spdlog::info("MigrationRunner: {} migration(s) applied, {} already up to date", applied_count, static_cast<int>(applied.size()));
}

// Split SQL into statements at top-level semicolons, respecting single-quote
// strings, double-quote identifiers and dollar-quoted bodies (so CREATE
// FUNCTION $$ ... ; ... $$; is NOT split in the middle). Line comments (-- …)
// outside quotes are skipped so a statement never starts with a comment line
// (SRV-020: comment-prefixed SQL used to be silently dropped by the
// trim_statement comment check). Returns statements WITHOUT trailing
// semicolons.
static std::string to_upper_ascii(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        unsigned char u = static_cast<unsigned char>(c);
        out.push_back(static_cast<char>(std::toupper(u)));
    }
    return out;
}

std::vector<std::string> MigrationRunner::split_statements(const std::string& sql)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_squote = false;
    bool in_dquote = false;
    size_t i = 0;
    while (i < sql.size()) {
        char c = sql[i];
        // Line comment (-- … end of line) outside quotes/dollar blocks.
        if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-' && !in_squote && !in_dquote) {
            while (i < sql.size() && sql[i] != '\n')
                ++i;
            continue;
        }
        // Dollar-quoted block (e.g. $$ ... $$, $func$ ... $func$).
        if (c == '$' && !in_squote && !in_dquote) {
            size_t j = i + 1;
            while (j < sql.size() && (isalnum((unsigned char)sql[j]) || sql[j] == '_'))
                ++j;
            if (j < sql.size() && sql[j] == '$') {
                std::string tag = sql.substr(i, j - i + 1); // e.g. $$ or $func$
                // Find the closing tag.
                size_t close = sql.find(tag, j + 1);
                if (close != std::string::npos) {
                    cur += sql.substr(i, close + tag.size() - i);
                    i = close + tag.size();
                    continue;
                }
            }
        }
        if (c == '\'' && !in_dquote) {
            in_squote = !in_squote;
            cur += c;
            ++i;
            continue;
        }
        if (c == '"' && !in_squote) {
            in_dquote = !in_dquote;
            cur += c;
            ++i;
            continue;
        }
        if (c == ';' && !in_squote && !in_dquote) {
            out.push_back(trim_statement(cur));
            cur.clear();
            ++i;
            continue;
        }
        cur += c;
        ++i;
    }
    if (!trim_statement(cur).empty())
        out.push_back(trim_statement(cur));
    return out;
}

static std::string trim_statement(std::string s)
{
    // Strip leading/trailing whitespace and a trailing semicolon.
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    std::string t = s.substr(b, e - b + 1);
    if (!t.empty() && t.back() == ';')
        t.pop_back();
    // Skip comment-only statements.
    if (t.rfind("--", 0) == 0)
        return "";
    return t;
}

} // namespace acadia::db

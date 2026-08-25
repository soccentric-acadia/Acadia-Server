// tests/unit/db/MigrationSplitterTest.cc — task 95: SQL splitter handles
// dollar-quoted bodies and single-quoted strings (valid SQL with semicolons
// inside must not be split mid-statement).
#include "db/MigrationRunner.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using acadia::db::MigrationRunner;

TEST(MigrationSplitter, SplitsPlainStatements) {
    auto stmts = MigrationRunner::split_statements(
        "CREATE TABLE a (id int);\nCREATE TABLE b (id int);");
    ASSERT_EQ(stmts.size(), 2u);
    EXPECT_NE(stmts[0].find("CREATE TABLE a"), std::string::npos);
    EXPECT_NE(stmts[1].find("CREATE TABLE b"), std::string::npos);
}

TEST(MigrationSplitter, DoesNotSplitInsideDollarQuotedFunction) {
    // A function body with internal semicolons ($$ ... ; ... $$) must stay ONE
    // statement (the naive semicolon split would have broken this).
    std::string sql =
        "CREATE FUNCTION f() RETURNS int AS $$\n"
        "BEGIN\n"
        "  PERFORM 1;\n"
        "  PERFORM 2;\n"
        "  RETURN 1;\n"
        "END;\n"
        "$$ LANGUAGE plpgsql;\n"
        "CREATE TABLE t (x int);";
    auto stmts = MigrationRunner::split_statements(sql);
    ASSERT_EQ(stmts.size(), 2u);
    EXPECT_NE(stmts[0].find("CREATE FUNCTION f"), std::string::npos);
    EXPECT_NE(stmts[1].find("CREATE TABLE t"), std::string::npos);
}

TEST(MigrationSplitter, DoesNotSplitInsideSingleQuotedString) {
    // A single-quoted string containing a semicolon must not split.
    std::string sql =
        "INSERT INTO t (s) VALUES ('a;b');\nUPDATE t SET s='c;d';";
    auto stmts = MigrationRunner::split_statements(sql);
    ASSERT_EQ(stmts.size(), 2u);
    EXPECT_NE(stmts[0].find("VALUES ('a;b')"), std::string::npos);
    EXPECT_NE(stmts[1].find("s='c;d'"), std::string::npos);
}

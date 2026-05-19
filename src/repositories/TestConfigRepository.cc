#include "repositories/TestConfigRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

namespace {
TestConfig row_to_config(const drogon::orm::Row& r) {
    return TestConfig{
        r["id"].as<std::string>(),
        r["name"].as<std::string>(),
        r["description"].as<std::string>(),
        r["mode"].as<std::string>(),
        r["common_config"].as<std::string>(),
        r["modules"].as<std::string>(),
        r["created_at"].as<std::string>(),
        r["created_by"].isNull() ? "" : r["created_by"].as<std::string>(),
        r["is_archived"].as<bool>()
    };
}
}  // namespace

TestConfig TestConfigRepository::create(const TestConfig& c) {
    auto db = db::get_db_client();
    auto new_id = util::generate_uuid();
    auto rows = db->execSqlSync(
        "INSERT INTO test_configs (id, name, description, mode, common_config, modules, created_by)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)"
        " RETURNING id, name, description, mode, common_config, modules,"
        "  created_at, created_by, is_archived",
        new_id.c_str(),
        c.name.c_str(), c.description.c_str(), c.mode.c_str(),
        c.common_config_json.empty() ? "{}" : c.common_config_json.c_str(),
        c.modules_json.empty() ? "[]" : c.modules_json.c_str(),
        c.created_by.empty() ? nullptr : c.created_by.c_str());
    return row_to_config(rows[0]);
}

std::optional<TestConfig> TestConfigRepository::find_by_id(const std::string& id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, name, description, mode, common_config, modules,"
        "  created_at, created_by, is_archived"
        " FROM test_configs WHERE id=?",
        id.c_str());
    if (rows.empty()) return std::nullopt;
    return row_to_config(rows[0]);
}

std::vector<TestConfig> TestConfigRepository::list(bool include_archived) {
    auto db = db::get_db_client();
    std::string sql =
        "SELECT id, name, description, mode, common_config, modules,"
        "  created_at, created_by, is_archived FROM test_configs";
    if (!include_archived) sql += " WHERE is_archived=0";
    sql += " ORDER BY created_at DESC";
    auto rows = db->execSqlSync(sql);
    std::vector<TestConfig> out;
    for (const auto& r : rows) out.push_back(row_to_config(r));
    return out;
}

void TestConfigRepository::update(const TestConfig& c) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE test_configs SET name=?, description=?, mode=?,"
        "  common_config=?, modules=? WHERE id=?",
        c.name.c_str(), c.description.c_str(), c.mode.c_str(),
        c.common_config_json.c_str(), c.modules_json.c_str(), c.id.c_str());
}

void TestConfigRepository::archive(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync("UPDATE test_configs SET is_archived=1 WHERE id=?", id.c_str());
}

void TestConfigRepository::remove(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync("DELETE FROM test_configs WHERE id=?", id.c_str());
}

}  // namespace acadia

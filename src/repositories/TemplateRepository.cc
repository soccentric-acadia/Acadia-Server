#include "repositories/TemplateRepository.h"
#include "db/DbClient.h"
#include "util/Uuid.h"

#include <drogon/orm/DbClient.h>

namespace acadia {

namespace {
Template row_to_tpl(const drogon::orm::Row& r) {
    return Template{
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

Template TemplateRepository::create(const Template& t) {
    auto db = db::get_db_client();
    auto new_id = util::generate_uuid();
    auto rows = db->execSqlSync(
        "INSERT INTO templates (id, name, description, mode, common_config, modules, created_by)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)"
        " RETURNING id, name, description, mode, common_config, modules,"
        "  created_at, created_by, is_archived",
        new_id.c_str(),
        t.name.c_str(), t.description.c_str(), t.mode.c_str(),
        t.common_config_json.empty() ? "{}" : t.common_config_json.c_str(),
        t.modules_json.empty() ? "[]" : t.modules_json.c_str(),
        t.created_by.empty() ? nullptr : t.created_by.c_str());
    return row_to_tpl(rows[0]);
}

std::optional<Template> TemplateRepository::find_by_id(const std::string& id) {
    auto db = db::get_db_client();
    auto rows = db->execSqlSync(
        "SELECT id, name, description, mode, common_config, modules,"
        "  created_at, created_by, is_archived"
        " FROM templates WHERE id=?",
        id.c_str());
    if (rows.empty()) return std::nullopt;
    return row_to_tpl(rows[0]);
}

std::vector<Template> TemplateRepository::list(bool include_archived) {
    auto db = db::get_db_client();
    std::string sql =
        "SELECT id, name, description, mode, common_config, modules,"
        "  created_at, created_by, is_archived FROM templates";
    if (!include_archived) sql += " WHERE is_archived=0";
    sql += " ORDER BY created_at DESC";
    auto rows = db->execSqlSync(sql);
    std::vector<Template> out;
    for (const auto& r : rows) out.push_back(row_to_tpl(r));
    return out;
}

void TemplateRepository::update(const Template& t) {
    auto db = db::get_db_client();
    db->execSqlSync(
        "UPDATE templates SET name=?, description=?, mode=?,"
        "  common_config=?, modules=? WHERE id=?",
        t.name.c_str(), t.description.c_str(), t.mode.c_str(),
        t.common_config_json.c_str(), t.modules_json.c_str(), t.id.c_str());
}

void TemplateRepository::archive(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync("UPDATE templates SET is_archived=1 WHERE id=?", id.c_str());
}

void TemplateRepository::remove(const std::string& id) {
    auto db = db::get_db_client();
    db->execSqlSync("DELETE FROM templates WHERE id=?", id.c_str());
}

}  // namespace acadia

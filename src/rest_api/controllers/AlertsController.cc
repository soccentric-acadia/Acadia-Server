// rest_api/controllers/AlertsController.cc
// CRUD for alert_rules backed by Drogon's DbClient (PostgreSQL). Persists
// rules to the alert_rules table (migration 0008) so list/get/update/remove
// return real stored data. Evaluation of thresholds happens in RunService.

#include "rest_api/controllers/AlertsController.h"
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>
#include <optional>
#include <set>
#include <string>

namespace acadia::rest
{

namespace
{

// Build the JSON representation of an alert rule from a Drogon result row.
Json::Value rule_from_row(const drogon::orm::Result::value_type& row)
{
    Json::Value v;
    v["id"] = row["id"].as<std::string>();
    v["module_id"] = row["module_id"].as<std::string>();
    v["field_path"] = row["field_path"].as<std::string>();
    v["operator"] = row["operator"].as<std::string>();
    v["threshold"] = row["threshold"].as<double>();
    v["severity"] = row["severity"].as<std::string>();
    v["enabled"] = row["enabled"].as<bool>();
    v["created_at"] = row["created_at"].as<std::string>();
    return v;
}

// Load a rule by id. Returns nullopt if absent.
std::optional<Json::Value> load_rule(const std::string& id)
{
    auto db = drogon::app().getDbClient();
    if (!db)
        return std::nullopt;
    try {
        auto r = db->execSqlSync("SELECT id, module_id, field_path, operator, threshold, severity, "
                                 "enabled, created_at FROM alert_rules WHERE id=$1",
                                 id);
        if (r.empty())
            return std::nullopt;
        return rule_from_row(r[0]);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

void AlertsController::list(const drogon::HttpRequestPtr& /*req*/, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    Json::Value resp;
    auto db = drogon::app().getDbClient();
    if (!db) {
        Json::Value err;
        err["error"] = "database unavailable";
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
        return;
    }
    try {
        auto result = db->execSqlSync("SELECT id, module_id, field_path, operator, threshold, severity, "
                                      "enabled, created_at FROM alert_rules ORDER BY created_at DESC");
        Json::Value rules = Json::arrayValue;
        for (const auto& row : result)
            rules.append(rule_from_row(row));
        resp["rules"] = rules;
        resp["total"] = static_cast<int>(rules.size());
        cb(drogon::HttpResponse::newHttpJsonResponse(resp));
    }
    catch (const std::exception& e) {
        Json::Value err;
        err["error"] = std::string("database error: ") + e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
    }
}

void AlertsController::create(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    auto body = req->getJsonObject();
    if (!body) {
        Json::Value err;
        err["error"] = "invalid json";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        cb(resp);
        return;
    }
    // Validate required fields.
    for (const char* f : {"module_id", "field_path", "operator", "threshold"}) {
        if (!(*body).isMember(f) || (*body)[f].isNull()) {
            Json::Value err;
            err["error"] = std::string("missing field: ") + f;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k422UnprocessableEntity);
            cb(resp);
            return;
        }
    }
    const std::string operator_val = (*body)["operator"].asString();
    static const std::set<std::string> kOps = {">", "<", ">=", "<=", "=="};
    if (kOps.find(operator_val) == kOps.end()) {
        Json::Value err;
        err["error"] = "invalid operator (allowed: > < >= <= ==)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k422UnprocessableEntity);
        cb(resp);
        return;
    }
    const std::string severity = (*body).get("severity", "warning").asString();
    if (severity != "info" && severity != "warning" && severity != "critical") {
        Json::Value err;
        err["error"] = "invalid severity (allowed: info warning critical)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k422UnprocessableEntity);
        cb(resp);
        return;
    }

    auto db = drogon::app().getDbClient();
    if (!db) {
        Json::Value err;
        err["error"] = "database unavailable";
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
        return;
    }
    try {
        auto result = db->execSqlSync("INSERT INTO alert_rules (module_id, field_path, operator, threshold, "
                                      "severity, enabled) VALUES ($1,$2,$3,$4,$5,$6) "
                                      "RETURNING id, module_id, field_path, operator, threshold, severity, "
                                      "enabled, created_at",
                                      (*body)["module_id"].asString(), (*body)["field_path"].asString(), operator_val, (*body)["threshold"].asDouble(),
                                      severity, (*body).get("enabled", true).asBool());
        if (result.empty()) {
            Json::Value err;
            err["error"] = "insert failed";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err);
            r->setStatusCode(drogon::k500InternalServerError);
            cb(r);
            return;
        }
        auto resp = drogon::HttpResponse::newHttpJsonResponse(rule_from_row(result[0]));
        resp->setStatusCode(drogon::k201Created);
        cb(resp);
    }
    catch (const std::exception& e) {
        Json::Value err;
        err["error"] = std::string("database error: ") + e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
    }
}

void AlertsController::get(const drogon::HttpRequestPtr& /*req*/, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id)
{
    auto rule = load_rule(id);
    if (!rule) {
        Json::Value err;
        err["error"] = "not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
        return;
    }
    cb(drogon::HttpResponse::newHttpJsonResponse(*rule));
}

void AlertsController::update(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id)
{
    auto body = req->getJsonObject();
    if (!body) {
        Json::Value err;
        err["error"] = "invalid json";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        cb(resp);
        return;
    }
    if (!load_rule(id)) {
        Json::Value err;
        err["error"] = "not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
        return;
    }
    auto db = drogon::app().getDbClient();
    if (!db) {
        Json::Value err;
        err["error"] = "database unavailable";
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
        return;
    }

    // Build the UPDATE dynamically so we only bind plain values (Drogon's
    // DbClient does not bind std::optional directly).
    try {
        std::vector<std::string> sets;
        std::vector<std::string> params;
        auto add_field = [&](const char* col, const std::string& value) {
            sets.push_back(col);
            params.push_back(value);
        };
        if (body->isMember("module_id"))
            add_field("module_id", (*body)["module_id"].asString());
        if (body->isMember("field_path"))
            add_field("field_path", (*body)["field_path"].asString());
        if (body->isMember("operator"))
            add_field("operator", (*body)["operator"].asString());
        if (body->isMember("threshold"))
            add_field("threshold", std::to_string((*body)["threshold"].asDouble()));
        if (body->isMember("severity"))
            add_field("severity", (*body)["severity"].asString());
        if (body->isMember("enabled"))
            add_field("enabled", ((*body)["enabled"].asBool() ? "true" : "false"));

        if (sets.empty()) {
            // Nothing to update: return current rule.
            cb(drogon::HttpResponse::newHttpJsonResponse(*load_rule(id)));
            return;
        }

        std::string sql = "UPDATE alert_rules SET ";
        for (size_t i = 0; i < sets.size(); ++i) {
            if (i)
                sql += ", ";
            sql += sets[i] + " = $" + std::to_string(i + 1);
        }
        sql += ", updated_at = NOW() WHERE id = $" + std::to_string(sets.size() + 1);
        sql += " RETURNING id, module_id, field_path, operator, threshold, severity, "
               "enabled, created_at";

        params.push_back(id);
        // Bind each parameter individually (Drogon does not accept a
        // std::vector<std::string> as a single SQL parameter).
        auto binder = *db << sql;
        for (const auto& p : params) {
            binder << p;
        }
        binder << drogon::orm::Mode::Blocking;
        drogon::orm::Result result(nullptr);
        binder >> [&result](const drogon::orm::Result& r) { result = r; };
        binder.exec();
        if (result.empty()) {
            Json::Value err;
            err["error"] = "not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            cb(resp);
            return;
        }
        cb(drogon::HttpResponse::newHttpJsonResponse(rule_from_row(result[0])));
    }
    catch (const std::exception& e) {
        Json::Value err;
        err["error"] = std::string("database error: ") + e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
    }
}

void AlertsController::remove(const drogon::HttpRequestPtr& /*req*/, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id)
{
    auto db = drogon::app().getDbClient();
    if (!db) {
        Json::Value err;
        err["error"] = "database unavailable";
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
        return;
    }
    try {
        auto result = db->execSqlSync("DELETE FROM alert_rules WHERE id=$1", id);
        if (result.affectedRows() == 0) {
            Json::Value err;
            err["error"] = "not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            cb(resp);
            return;
        }
        Json::Value resp;
        resp["ok"] = true;
        cb(drogon::HttpResponse::newHttpJsonResponse(resp));
    }
    catch (const std::exception& e) {
        Json::Value err;
        err["error"] = std::string("database error: ") + e.what();
        auto r = drogon::HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(drogon::k500InternalServerError);
        cb(r);
    }
}

} // namespace acadia::rest

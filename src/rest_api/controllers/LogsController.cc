#include "rest_api/controllers/LogsController.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace acadia::rest {

LogsController::LogsController(LogService& svc) : svc_(svc) {}

void LogsController::list(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                           std::string run_id) {
    LogFilter f;
    f.run_id = run_id;
    if (auto p = req->getParameter("q"); !p.empty()) f.query = p;
    if (auto p = req->getParameter("level"); !p.empty()) f.severity = p;
    if (auto p = req->getParameter("limit"); !p.empty()) f.limit = std::stoi(p);
    if (auto p = req->getParameter("offset"); !p.empty()) f.offset = std::stoi(p);

    auto entries = svc_.list(f);
    Json::Value arr{Json::arrayValue};
    for (const auto& e : entries) {
        Json::Value v;
        v["id"]        = e.id;
        v["run_id"]    = e.run_id;
        v["level"]     = e.severity;
        v["message"]   = e.message;
        v["timestamp"] = e.timestamp;
        arr.append(v);
    }
    Json::Value resp;
    resp["logs"] = arr;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void LogsController::tail(const drogon::HttpRequestPtr& /*req*/,
                           std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                           std::string run_id) {
    // SSE response: streaming text/event-stream
    // For simplicity, we return a 200 with an SSE header and close immediately.
    // Real SSE would use Drogon's async streaming response.
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeString("text/event-stream");
    resp->addHeader("Cache-Control", "no-cache");
    resp->addHeader("X-Acadia-Run-Id", run_id);
    resp->setBody("data: {\"status\":\"connected\"}\n\n");
    cb(resp);
}

static Json::Value log_entry_to_json(const LogEntry& e) {
    Json::Value v;
    v["id"]        = e.id;
    v["run_id"]    = e.run_id;
    v["source"]    = e.source;
    v["level"]     = e.severity;
    v["severity"]  = e.severity;
    v["message"]   = e.message;
    v["timestamp"] = e.timestamp;
    return v;
}

void LogsController::global_list(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    LogFilter f;
    if (auto p = req->getParameter("source"); !p.empty())  f.source   = p;
    if (auto p = req->getParameter("level");  !p.empty())  f.severity = p;
    if (auto p = req->getParameter("run_id"); !p.empty())  f.run_id   = p;
    if (auto p = req->getParameter("query");  !p.empty())  f.query    = p;
    if (auto p = req->getParameter("after");  !p.empty())  f.from_ts  = p;
    if (auto p = req->getParameter("limit");  !p.empty())  f.limit    = std::stoi(p);
    if (auto p = req->getParameter("offset"); !p.empty())  f.offset   = std::stoi(p);

    auto entries = svc_.list(f);
    Json::Value arr{Json::arrayValue};
    for (const auto& e : entries) arr.append(log_entry_to_json(e));
    Json::Value resp;
    resp["logs"]  = arr;
    resp["total"] = static_cast<int>(entries.size());
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void LogsController::search(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    LogFilter f;
    if (auto p = req->getParameter("q");      !p.empty()) f.query  = p;
    if (auto p = req->getParameter("run_id"); !p.empty()) f.run_id = p;
    if (auto p = req->getParameter("limit");  !p.empty()) f.limit  = std::stoi(p);
    f.limit = std::min(f.limit, 500);

    auto entries = svc_.list(f);
    Json::Value arr{Json::arrayValue};
    for (const auto& e : entries) arr.append(log_entry_to_json(e));
    Json::Value resp;
    resp["logs"]  = arr;
    resp["total"] = static_cast<int>(entries.size());
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void LogsController::list_bookmarks(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto items = svc_.list_bookmarks(user_id);
    Json::Value arr{Json::arrayValue};
    for (const auto& b : items) {
        Json::Value v;
        v["id"]         = b.id;
        v["log_id"]     = b.log_id;
        v["note"]       = b.note;
        v["created_at"] = b.created_at;
        arr.append(v);
    }
    Json::Value resp;
    resp["bookmarks"] = arr;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void LogsController::create_bookmark(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto body = req->getJsonObject();
    std::string log_id = body ? (*body)["log_id"].asString() : "";
    std::string note   = body ? (*body)["note"].asString()   : "";
    auto b = svc_.create_bookmark(user_id, log_id, note);
    Json::Value v;
    v["id"]         = b.id;
    v["log_id"]     = b.log_id;
    v["note"]       = b.note;
    v["created_at"] = b.created_at;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(v);
    resp->setStatusCode(drogon::k201Created);
    cb(resp);
}

void LogsController::delete_bookmark(const drogon::HttpRequestPtr& /*req*/,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                      std::string id) {
    svc_.remove_bookmark(id);
    Json::Value resp; resp["ok"] = true;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void LogsController::list_views(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto items = svc_.list_views(user_id);
    Json::Value arr{Json::arrayValue};
    for (const auto& v : items) {
        Json::Value o;
        o["id"]           = v.id;
        o["name"]         = v.name;
        o["filters_json"] = v.filters_json;
        o["created_at"]   = v.created_at;
        arr.append(o);
    }
    Json::Value resp;
    resp["views"] = arr;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void LogsController::create_view(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto body  = req->getJsonObject();
    std::string name         = body ? (*body)["name"].asString()         : "";
    std::string filters_json = body ? (*body)["filters_json"].asString() : "{}";
    auto sv = svc_.create_view(user_id, name, filters_json);
    Json::Value o;
    o["id"]           = sv.id;
    o["name"]         = sv.name;
    o["filters_json"] = sv.filters_json;
    o["created_at"]   = sv.created_at;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(o);
    resp->setStatusCode(drogon::k201Created);
    cb(resp);
}

}  // namespace acadia::rest

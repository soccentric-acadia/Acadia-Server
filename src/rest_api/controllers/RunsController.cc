#include "rest_api/controllers/RunsController.h"
#include "repositories/ResultRepository.h"
#include "util/Errors.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <sstream>

namespace acadia::rest {

static Json::Value run_to_json(const Run& r) {
    Json::Value v;
    v["id"]               = r.id;
    v["name"]             = r.name;
    v["description"]      = r.description;
    v["mode"]             = r.mode;
    v["status"]           = r.status;
    v["baseline_flag"]    = r.baseline_flag;
    v["notes"]            = r.notes;
    v["error_message"]    = r.error_message;
    v["created_at"]       = r.created_at;
    v["created_by"]       = r.created_by;
    v["started_at"]       = r.started_at;
    v["ended_at"]         = r.ended_at;
    // Parse tags_json inline so callers get an object, not a string.
    if (!r.tags_json.empty() && r.tags_json != "null") {
        Json::CharReaderBuilder rb;
        Json::Value tags;
        std::string errs;
        std::istringstream ss(r.tags_json);
        if (Json::parseFromStream(rb, ss, &tags, &errs)) v["tags"] = tags;
        else v["tags"] = Json::objectValue;
    } else {
        v["tags"] = Json::objectValue;
    }
    return v;
}

RunsController::RunsController(RunService& svc) : svc_(svc) {}

void RunsController::list(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    RunFilter f;
    f.status = req->getParameter("status");
    f.mode   = req->getParameter("mode");
    auto lim = req->getParameter("limit");
    auto off = req->getParameter("offset");
    if (!lim.empty()) f.limit  = std::stoi(lim);
    if (!off.empty()) f.offset = std::stoi(off);
    auto runs = svc_.list_runs(f);
    Json::Value arr{Json::arrayValue};
    for (const auto& r : runs) arr.append(run_to_json(r));
    Json::Value resp;
    resp["runs"]  = arr;
    resp["total"] = static_cast<int>(runs.size());
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void RunsController::create(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    auto body = req->getJsonObject();
    std::string config_id = body ? (*body)["config_id"].asString() : "";
    std::string mode      = body ? (*body)["mode"].asString()      : "single_shot";
    std::string user_id   = req->getAttributes()->get<std::string>("user_id");
    auto run = svc_.start_run(config_id, mode, mode, user_id);
    cb(drogon::HttpResponse::newHttpJsonResponse(run_to_json(run)));
}

void RunsController::get(const drogon::HttpRequestPtr& /*req*/,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                          std::string id) {
    try {
        auto run = svc_.get_run(id);
        cb(drogon::HttpResponse::newHttpJsonResponse(run_to_json(run)));
    } catch (const NotFoundError& e) {
        Json::Value err; err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
    }
}

void RunsController::cancel(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                              std::string id) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    svc_.cancel_run(id, user_id);
    Json::Value resp; resp["ok"] = true;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void RunsController::pause(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                            std::string id) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    svc_.pause_run(id, user_id);
    Json::Value resp; resp["ok"] = true;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void RunsController::resume(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string id) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    svc_.resume_run(id, user_id);
    Json::Value resp; resp["ok"] = true;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void RunsController::results(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                              std::string id) {
    ResultFilter f;
    f.run_id = id;
    if (auto v = req->getOptionalParameter<std::string>("module_id")) f.module_id = *v;
    if (auto v = req->getOptionalParameter<std::string>("status"))    f.status    = *v;
    if (auto v = req->getOptionalParameter<std::string>("since"))     f.since     = *v;
    if (auto v = req->getOptionalParameter<std::string>("until"))     f.until     = *v;
    if (auto v = req->getOptionalParameter<int>("limit"))             f.limit     = *v;
    if (auto v = req->getOptionalParameter<int>("offset"))            f.offset    = *v;

    auto rows = svc_.get_results(f);

    Json::Value arr(Json::arrayValue);
    for (const auto& r : rows) {
        Json::Value o;
        o["id"]               = r.id;
        o["run_id"]           = r.run_id;
        o["module_id"]        = r.module_id;
        o["module_version"]   = r.module_version;
        o["timestamp"]        = r.timestamp;
        o["mode"]             = r.mode;
        o["status"]           = r.status;
        o["error_message"]    = r.error_message;
        o["payload_type_url"] = r.payload_type_url;
        // payload_bytes as base64 is omitted here; clients use the dedicated
        // /api/v1/results/{id}/payload endpoint if they need raw bytes.
        arr.append(o);
    }

    Json::Value resp;
    resp["results"] = arr;
    resp["total"]   = static_cast<int>(rows.size());
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void RunsController::events(const drogon::HttpRequestPtr& /*req*/,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string id) {
    // SSE endpoint — returns a minimal event-stream response with the run's
    // current state. A full implementation would push events asynchronously
    // via subscribe_results() as state transitions and results arrive.
    std::string body;
    try {
        auto run = svc_.get_run(id);
        body += "event: state\n";
        body += "data: {\"type\":\"state\",\"run_id\":\"" + id + "\",\"status\":\"" + run.status + "\"}\n\n";
    } catch (...) {
        body += "event: error\n";
        body += "data: {\"type\":\"error\",\"message\":\"run not found\"}\n\n";
    }

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeString("text/event-stream");
    resp->addHeader("Cache-Control", "no-cache");
    resp->addHeader("X-Acadia-Run-Id", id);
    resp->setBody(body);
    cb(resp);
}

}  // namespace acadia::rest

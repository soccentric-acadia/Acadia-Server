#include "rest_api/controllers/RunsController.h"
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

}  // namespace acadia::rest

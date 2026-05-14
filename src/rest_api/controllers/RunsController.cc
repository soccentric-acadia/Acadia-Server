#include "rest_api/controllers/RunsController.h"
#include "repositories/ResultRepository.h"
#include "util/Errors.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <map>
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

void RunsController::remove(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string id) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    try {
        svc_.delete_run(id, user_id);
        Json::Value resp; resp["ok"] = true;
        cb(drogon::HttpResponse::newHttpJsonResponse(resp));
    } catch (const NotFoundError& e) {
        Json::Value err; err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
    }
}

void RunsController::baseline(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                               std::string id) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    try {
        svc_.set_baseline(id, user_id);
        Json::Value resp; resp["ok"] = true;
        cb(drogon::HttpResponse::newHttpJsonResponse(resp));
    } catch (const NotFoundError& e) {
        Json::Value err; err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
    }
}

void RunsController::rerun(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                            std::string id) {
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    try {
        auto new_run = svc_.rerun(id, user_id);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(run_to_json(new_run));
        resp->setStatusCode(drogon::k201Created);
        cb(resp);
    } catch (const NotFoundError& e) {
        Json::Value err; err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
    }
}

void RunsController::export_run(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                 std::string id) {
    std::string fmt = req->getParameter("format");
    if (fmt.empty()) fmt = "json";

    ResultFilter f;
    f.run_id  = id;
    f.limit   = 10000;
    auto rows = svc_.get_results(f);

    auto resp = drogon::HttpResponse::newHttpResponse();
    if (fmt == "csv") {
        std::ostringstream csv;
        csv << "id,run_id,module_id,module_version,timestamp,mode,status,error_message,payload_type_url\n";
        for (const auto& r : rows) {
            csv << r.id << "," << r.run_id << "," << r.module_id << ","
                << r.module_version << "," << r.timestamp << "," << r.mode << ","
                << r.status << "," << r.error_message << "," << r.payload_type_url << "\n";
        }
        resp->setContentTypeString("text/csv");
        resp->addHeader("Content-Disposition", "attachment; filename=\"run-" + id + ".csv\"");
        resp->setBody(csv.str());
    } else {
        Json::Value arr{Json::arrayValue};
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
            arr.append(o);
        }
        Json::Value root;
        root["run_id"]  = id;
        root["results"] = arr;
        Json::FastWriter fw;
        std::string body = fw.write(root);
        resp->setContentTypeString("application/json");
        resp->addHeader("Content-Disposition", "attachment; filename=\"run-" + id + ".json\"");
        resp->setBody(body);
    }
    cb(resp);
}

void RunsController::report(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                             std::string id) {
    std::string fmt = req->getParameter("format");
    if (fmt.empty()) fmt = "html";

    try {
        auto run  = svc_.get_run(id);
        ResultFilter f;
        f.run_id = id;
        f.limit  = 10000;
        auto rows = svc_.get_results(f);

        // Build pass/fail counts per module.
        std::map<std::string, int> pass_count, fail_count;
        for (const auto& r : rows) {
            if (r.status == "pass") pass_count[r.module_id]++;
            else                    fail_count[r.module_id]++;
        }

        std::ostringstream html;
        html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             << "<title>Acadia Report — " << run.name << "</title>"
             << "<style>body{font-family:sans-serif;margin:2rem}table{border-collapse:collapse;width:100%}"
             << "th,td{border:1px solid #ccc;padding:.5rem .75rem;text-align:left}"
             << "th{background:#f5f5f5}.pass{color:#16a34a}.fail{color:#dc2626}</style></head><body>"
             << "<h1>Acadia Run Report</h1>"
             << "<table><tr><th>Field</th><th>Value</th></tr>"
             << "<tr><td>Run ID</td><td>" << run.id << "</td></tr>"
             << "<tr><td>Name</td><td>" << run.name << "</td></tr>"
             << "<tr><td>Mode</td><td>" << run.mode << "</td></tr>"
             << "<tr><td>Status</td><td>" << run.status << "</td></tr>"
             << "<tr><td>Started</td><td>" << run.started_at << "</td></tr>"
             << "<tr><td>Ended</td><td>" << run.ended_at << "</td></tr>"
             << "</table><h2>Module Results</h2>"
             << "<table><tr><th>Module</th><th>Pass</th><th>Fail</th></tr>";
        for (const auto& [mod, pc] : pass_count) {
            int fc = fail_count.count(mod) ? fail_count.at(mod) : 0;
            html << "<tr><td>" << mod << "</td>"
                 << "<td class=\"pass\">" << pc << "</td>"
                 << "<td class=\"" << (fc > 0 ? "fail" : "pass") << "\">" << fc << "</td></tr>";
        }
        for (const auto& [mod, fc] : fail_count) {
            if (!pass_count.count(mod)) {
                html << "<tr><td>" << mod << "</td><td class=\"pass\">0</td>"
                     << "<td class=\"fail\">" << fc << "</td></tr>";
            }
        }
        html << "</table></body></html>";

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeString("text/html; charset=utf-8");
        resp->addHeader("Content-Disposition",
                        "attachment; filename=\"report-" + id + ".html\"");
        resp->setBody(html.str());
        cb(resp);
    } catch (const NotFoundError& e) {
        Json::Value err; err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
    }
}

}  // namespace acadia::rest

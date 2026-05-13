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

}  // namespace acadia::rest

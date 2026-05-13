#include "rest_api/controllers/TargetsController.h"
#include "util/Errors.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace acadia::rest {

TargetsController::TargetsController(TargetService& svc) : svc_(svc) {}

static Json::Value board_to_json(const Board& b) {
    Json::Value v;
    v["id"]           = b.id;
    v["hostname"]     = b.hostname;
    v["ip_address"]   = b.ip_address;
    v["status"]       = b.status;
    v["last_seen_at"] = b.last_seen_at;
    return v;
}

void TargetsController::get_board(const drogon::HttpRequestPtr& /*req*/,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    try {
        auto b = svc_.get_board();
        cb(drogon::HttpResponse::newHttpJsonResponse(board_to_json(b)));
    } catch (const NotFoundError& e) {
        Json::Value err; err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
    }
}

void TargetsController::register_board(const drogon::HttpRequestPtr& req,
                                        std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    auto body = req->getJsonObject();
    Board b;
    if (body) {
        b.hostname   = (*body)["hostname"].asString();
        b.ip_address = (*body)["ip_address"].asString();
    }
    auto created = svc_.register_board(b);
    cb(drogon::HttpResponse::newHttpJsonResponse(board_to_json(created)));
}

void TargetsController::update_board(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    auto body = req->getJsonObject();
    Board b;
    if (body) {
        b.hostname   = (*body)["hostname"].asString();
        b.ip_address = (*body)["ip_address"].asString();
    }
    auto updated = svc_.update_board(b);
    cb(drogon::HttpResponse::newHttpJsonResponse(board_to_json(updated)));
}

void TargetsController::heartbeat(const drogon::HttpRequestPtr& /*req*/,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    svc_.heartbeat();
    Json::Value resp; resp["ok"] = true;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

}  // namespace acadia::rest

#include "rest_api/controllers/TargetsController.h"
#include "util/Errors.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace acadia::rest {

TargetsController::TargetsController(TargetService& svc) : svc_(svc) {}

static Json::Value board_to_json(const Board& b) {
    Json::Value v;
    v["id"]        = b.id;
    v["name"]      = b.name;
    v["hostname"]  = b.hostname;
    v["model"]     = b.model;
    v["status"]    = b.status;
    v["last_seen"] = b.last_seen;
    return v;
}

void TargetsController::get_board(const drogon::HttpRequestPtr& /*req*/,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    try {
        auto b = svc_.get_board();
        if (!b) {
            Json::Value err; err["error"] = "no board registered";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            cb(resp);
            return;
        }
        cb(drogon::HttpResponse::newHttpJsonResponse(board_to_json(*b)));
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
        b.name     = (*body)["name"].asString();
        b.hostname = (*body)["hostname"].asString();
        b.model    = (*body)["model"].asString();
    }
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto created = svc_.register_board(b, user_id);
    cb(drogon::HttpResponse::newHttpJsonResponse(board_to_json(created)));
}

void TargetsController::update_board(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    auto body = req->getJsonObject();
    Board b;
    if (body) {
        b.name     = (*body)["name"].asString();
        b.hostname = (*body)["hostname"].asString();
        b.model    = (*body)["model"].asString();
    }
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    svc_.update_board(b, user_id);
    auto updated = svc_.get_board();
    if (updated) {
        cb(drogon::HttpResponse::newHttpJsonResponse(board_to_json(*updated)));
    } else {
        cb(drogon::HttpResponse::newHttpJsonResponse(Json::Value{}));
    }
}

void TargetsController::heartbeat(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    // Get board id from body or from existing board record
    auto board = svc_.get_board();
    if (board) {
        svc_.heartbeat(board->id, "online");
    }
    Json::Value resp; resp["ok"] = true;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

}  // namespace acadia::rest

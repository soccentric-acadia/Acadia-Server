#include "rest_api/controllers/OtaController.h"
#include "rest_api/AccessControl.h"
#include "util/Errors.h"
#include <drogon/HttpResponse.h>
#include <drogon/MultiPart.h>
#include <json/json.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace acadia::rest
{

OtaController::OtaController(OtaService& svc) : svc_(svc)
{
}

// SRV-007: OTA mutations require admin or operator; viewer is read-only.
static bool denied(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    if (has_role(req, {"admin", "operator"}))
        return false;
    Json::Value err;
    err["error"] = "Forbidden: admin or operator role required";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
    resp->setStatusCode(drogon::k403Forbidden);
    cb(resp);
    return true;
}

// SRV-026: reduce an operator/filename to a safe basename whitelist, so a
// crafted "../../etc/passwd" can never escape the upload directory.
static std::string safe_artifact_name(const std::string& raw)
{
    std::string base = raw;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    std::string out;
    for (char c : base) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (ok) out += c;
    }
    // Strip leading dots (never allow hidden files / relative markers).
    out.erase(0, out.find_first_not_of('.'));
    return out;
}

static Json::Value ota_to_json(const OtaUpgrade& u)
{
    Json::Value v;
    v["id"] = u.id;
    v["status"] = u.status;
    v["progress"] = u.progress;
    v["artifact_path"] = u.artifact_path;
    v["artifact_hash"] = u.artifact_hash;
    v["from_version"] = u.from_version;
    v["to_version"] = u.to_version;
    v["created_at"] = u.created_at;
    v["ended_at"] = u.ended_at;
    return v;
}

void OtaController::list(const drogon::HttpRequestPtr& /*req*/, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    auto upgrades = svc_.list();
    Json::Value arr{Json::arrayValue};
    for (const auto& u : upgrades)
        arr.append(ota_to_json(u));
    Json::Value resp;
    resp["upgrades"] = arr;
    cb(drogon::HttpResponse::newHttpJsonResponse(resp));
}

void OtaController::get(const drogon::HttpRequestPtr& /*req*/, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id)
{
    try {
        cb(drogon::HttpResponse::newHttpJsonResponse(ota_to_json(svc_.get(id))));
    }
    catch (const NotFoundError& e) {
        Json::Value err;
        err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        cb(resp);
    }
}

void OtaController::upload(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb)
{
    // SRV-007: role gate.
    if (denied(req, std::move(cb)))
        return;

    // Multipart upload: parse the multipart body to extract the uploaded file.
    drogon::MultiPartParser mp;
    if (mp.parse(req) != 0 || mp.getFiles().empty()) {
        Json::Value err;
        err["error"] = "no file uploaded";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        cb(resp);
        return;
    }
    const auto& f = mp.getFiles()[0];
    // SRV-026: sanitize the client-supplied filename to a basename whitelist
    // before it is used to build a server-side path.
    std::string name = safe_artifact_name(f.getFileName());
    // SRV-027: validate the artifact kind — only signed SWUpdate images.
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (name.empty() || lower.size() <= 4 || lower.substr(lower.size() - 4) != ".swu") {
            Json::Value err;
            err["error"] = "only .swu artifacts are accepted";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k400BadRequest);
            cb(resp);
            return;
        }
    }
    // Save the uploaded artifact to a temporary path under the upload dir.
    std::string tmp_name = "ota_" + name;
    f.saveAs(tmp_name);
    std::string saved_path = drogon::app().getUploadPath() + "/" + tmp_name;
    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto upgrade = svc_.accept_artifact(name, saved_path, user_id);
    cb(drogon::HttpResponse::newHttpJsonResponse(ota_to_json(upgrade)));
}

void OtaController::trigger(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id)
{
    // SRV-007: role gate.
    if (denied(req, std::move(cb)))
        return;

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    try {
        auto upgrade = svc_.trigger_upgrade(id, user_id);
        cb(drogon::HttpResponse::newHttpJsonResponse(ota_to_json(upgrade)));
    }
    catch (const std::exception& e) {
        Json::Value err;
        err["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        cb(resp);
    }
}

void OtaController::rollback(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, std::string id)
{
    // SRV-007: role gate.
    if (denied(req, std::move(cb)))
        return;

    std::string user_id = req->getAttributes()->get<std::string>("user_id");
    auto rb = svc_.rollback(id, user_id);
    cb(drogon::HttpResponse::newHttpJsonResponse(ota_to_json(rb)));
}

} // namespace acadia::rest

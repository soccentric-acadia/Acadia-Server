#pragma once
// rest_api/controllers/NotificationsController.h
#include "services/NotificationService.h"
#include <drogon/HttpController.h>

namespace acadia::rest
{

class NotificationsController : public drogon::HttpController<NotificationsController, false>
{
  public:
    explicit NotificationsController(NotificationService& svc);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(NotificationsController::list, "/api/v1/notifications", "acadia::rest::AuthFilter", drogon::Get);
    ADD_METHOD_TO(NotificationsController::mark_read, "/api/v1/notifications/{id}/read", "acadia::rest::AuthFilter", drogon::Post);
    ADD_METHOD_TO(NotificationsController::mark_all, "/api/v1/notifications/read-all", "acadia::rest::AuthFilter", drogon::Post);
    ADD_METHOD_TO(NotificationsController::unread_count, "/api/v1/notifications/count", "acadia::rest::AuthFilter", drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void mark_read(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&, std::string id);
    void mark_all(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);
    void unread_count(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&);

  private:
    NotificationService& svc_;
};

} // namespace acadia::rest

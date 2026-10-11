#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

bool app_wifi_portal_authorize_request(httpd_req_t *request,
                                       bool require_token,
                                       bool allow_closing);
esp_err_t app_wifi_portal_register_http_handlers(httpd_handle_t server);
void app_wifi_portal_on_softap_started(void);
void app_wifi_portal_on_softap_stopped(void);

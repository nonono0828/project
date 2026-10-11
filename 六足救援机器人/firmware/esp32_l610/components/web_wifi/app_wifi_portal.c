/*
 * Robot local configuration portal.
 *
 * This module deliberately contains no modem AT implementation and no camera
 * driver calls.  It validates HTTP input, serialises local operations and
 * delegates application-owned work through app_wifi_portal_callbacks_t.
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/ip_addr.h"

#include "app_wifi.h"
#include "app_wifi_portal_internal.h"

#define PORTAL_SESSION_SECONDS 600U
#define PORTAL_EXTENSION_DEFAULT_MINUTES 10U
#define PORTAL_EXTENSION_MAX_MINUTES 1440U
#define PORTAL_EXTENSION_HISTORY_SIZE 16U
#define PORTAL_CLOSE_DELAY_US 500000ULL
#define PORTAL_REQUEST_BODY_MAX 768
#define PORTAL_WORK_QUEUE_LENGTH 6
#define PORTAL_MODEM_TIMEOUT_US (120LL * 1000000LL)
#define PORTAL_VIDEO_TIMEOUT_US (15LL * 1000000LL)
#define PORTAL_CLOSE_NOTIFY BIT0

static const char *TAG = "robot portal";

extern const unsigned char portal_index_html_start[]
    asm("_binary_index_html_start");
extern const unsigned char portal_index_html_end[]
    asm("_binary_index_html_end");

typedef enum {
    PORTAL_WIFI_QUERY = 0,
    PORTAL_WIFI_SET_ENABLED,
    PORTAL_WIFI_SCAN,
    PORTAL_WIFI_CONNECT,
    PORTAL_WIFI_SELECT_UPLINK,
} portal_wifi_action_t;

typedef enum {
    PORTAL_JOB_WIFI = 0,
    PORTAL_JOB_MODEM,
    PORTAL_JOB_VIDEO,
} portal_job_type_t;

typedef struct {
    bool valid;
    char request_id[APP_WIFI_PORTAL_REQUEST_ID_MAX + 1];
    uint32_t fingerprint;
    app_wifi_portal_operation_state_t state;
    esp_err_t error;
    int64_t submitted_us;
    char message[96];
} portal_operation_t;

typedef struct {
    portal_job_type_t type;
    char request_id[APP_WIFI_PORTAL_REQUEST_ID_MAX + 1];
    union {
        struct {
            portal_wifi_action_t action;
            bool enabled;
            bool use_wifi;
            char ssid[33];
            char password[64];
            char security[16];
        } wifi;
        app_wifi_portal_modem_command_t modem;
        app_wifi_portal_video_settings_t video;
    } data;
} portal_job_t;

typedef enum {
    PORTAL_ACCEPTED = 0,
    PORTAL_DUPLICATE,
    PORTAL_REUSED_ID,
    PORTAL_BUSY,
} portal_accept_result_t;

typedef struct {
    char request_id[APP_WIFI_PORTAL_REQUEST_ID_MAX + 1];
    uint32_t minutes;
} portal_extension_record_t;

typedef struct {
    bool active;
    bool closing;
    bool extended;
    uint32_t extension_count;
    size_t next_extension_record;
    int64_t deadline_us;
    app_wifi_portal_close_reason_t close_reason;
    char token[33];
    portal_extension_record_t
        extension_history[PORTAL_EXTENSION_HISTORY_SIZE];
    char close_request_id[APP_WIFI_PORTAL_REQUEST_ID_MAX + 1];
} portal_session_t;

static portMUX_TYPE s_portal_mux = portMUX_INITIALIZER_UNLOCKED;
static portal_session_t s_session;
static const uint16_t s_supported_lte_bands[] = {
    1, 3, 5, 7, 8, 20, 34, 39, 40, 41,
};
static portal_operation_t s_wifi_operation;
static portal_operation_t s_modem_operation;
static portal_operation_t s_video_operation;
static app_wifi_network_t s_scan_results[APP_WIFI_MAX_SCAN_RESULTS];
static size_t s_scan_result_count;
static app_wifi_portal_callbacks_t s_callbacks;
static void *s_callbacks_ctx;
static QueueHandle_t s_work_queue;
static TaskHandle_t s_worker_task;
static TaskHandle_t s_close_task;
static esp_timer_handle_t s_session_timer;

static const char *operation_state_name(app_wifi_portal_operation_state_t state)
{
    switch (state) {
    case APP_WIFI_PORTAL_OPERATION_QUEUED: return "queued";
    case APP_WIFI_PORTAL_OPERATION_APPLYING: return "applying";
    case APP_WIFI_PORTAL_OPERATION_SUCCESS: return "success";
    case APP_WIFI_PORTAL_OPERATION_ERROR: return "error";
    default: return "idle";
    }
}

static const char *close_reason_name(app_wifi_portal_close_reason_t reason)
{
    switch (reason) {
    case APP_WIFI_PORTAL_CLOSE_TIMEOUT: return "timeout";
    case APP_WIFI_PORTAL_CLOSE_USER: return "user";
    default: return "cloud";
    }
}

static const char *modem_action_name(app_wifi_portal_modem_action_t action)
{
    switch (action) {
    case APP_WIFI_PORTAL_MODEM_RESELECT: return "reselect";
    case APP_WIFI_PORTAL_MODEM_SET_BANDS: return "set_bands";
    case APP_WIFI_PORTAL_MODEM_SET_CELL_LOCK: return "set_cell_lock";
    case APP_WIFI_PORTAL_MODEM_CLEAR_CELL_LOCK: return "clear_cell_lock";
    default: return "query";
    }
}

static uint32_t hash_bytes(uint32_t hash, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t hash_string(uint32_t hash, const char *value)
{
    return hash_bytes(hash, value, strlen(value));
}

static bool request_id_valid(const char *request_id)
{
    if (request_id == NULL) {
        return false;
    }
    size_t length = strnlen(request_id, APP_WIFI_PORTAL_REQUEST_ID_MAX + 1);
    if (length == 0 || length > APP_WIFI_PORTAL_REQUEST_ID_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)request_id[i];
        if (!(isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == ':')) {
            return false;
        }
    }
    return true;
}

static bool operation_pending(const portal_operation_t *operation)
{
    return operation->valid &&
        (operation->state == APP_WIFI_PORTAL_OPERATION_QUEUED ||
         operation->state == APP_WIFI_PORTAL_OPERATION_APPLYING);
}

static bool operation_has_request_id_locked(
    const portal_operation_t *operation, const char *request_id)
{
    return operation->valid &&
           strcmp(operation->request_id, request_id) == 0;
}

static portal_extension_record_t *extension_record_locked(
    const char *request_id)
{
    for (size_t index = 0; index < PORTAL_EXTENSION_HISTORY_SIZE; ++index) {
        portal_extension_record_t *record =
            &s_session.extension_history[index];
        if (record->request_id[0] != '\0' &&
                strcmp(record->request_id, request_id) == 0) {
            return record;
        }
    }
    return NULL;
}

static bool request_id_used_by_other_operation_locked(
    const portal_operation_t *owner, const char *request_id)
{
    return (&s_wifi_operation != owner &&
            operation_has_request_id_locked(&s_wifi_operation, request_id)) ||
           (&s_modem_operation != owner &&
            operation_has_request_id_locked(&s_modem_operation, request_id)) ||
           (&s_video_operation != owner &&
            operation_has_request_id_locked(&s_video_operation, request_id)) ||
           extension_record_locked(request_id) != NULL;
}

static portal_accept_result_t accept_operation(portal_operation_t *operation,
                                                const char *request_id,
                                                uint32_t fingerprint)
{
    portal_accept_result_t result = PORTAL_ACCEPTED;
    portENTER_CRITICAL(&s_portal_mux);
    if (operation->valid && strcmp(operation->request_id, request_id) == 0) {
        result = operation->fingerprint == fingerprint
            ? PORTAL_DUPLICATE : PORTAL_REUSED_ID;
    } else if (request_id_used_by_other_operation_locked(operation, request_id) ||
               strcmp(s_session.close_request_id, request_id) == 0) {
        result = PORTAL_REUSED_ID;
    } else if (operation_pending(operation)) {
        result = PORTAL_BUSY;
    } else {
        memset(operation, 0, sizeof(*operation));
        operation->valid = true;
        snprintf(operation->request_id, sizeof(operation->request_id), "%s",
                 request_id);
        operation->fingerprint = fingerprint;
        operation->state = APP_WIFI_PORTAL_OPERATION_QUEUED;
        operation->submitted_us = esp_timer_get_time();
        snprintf(operation->message, sizeof(operation->message), "请求已进入队列");
    }
    portEXIT_CRITICAL(&s_portal_mux);
    return result;
}

static void set_operation_state(portal_operation_t *operation,
                                app_wifi_portal_operation_state_t state,
                                esp_err_t error, const char *message)
{
    portENTER_CRITICAL(&s_portal_mux);
    operation->state = state;
    operation->error = error;
    snprintf(operation->message, sizeof(operation->message), "%s",
             message == NULL ? "" : message);
    portEXIT_CRITICAL(&s_portal_mux);
}

static portal_operation_t operation_snapshot(const portal_operation_t *operation)
{
    portal_operation_t snapshot;
    portENTER_CRITICAL(&s_portal_mux);
    snapshot = *operation;
    portEXIT_CRITICAL(&s_portal_mux);
    return snapshot;
}

static void add_operation_json(cJSON *parent, const portal_operation_t *source)
{
    portal_operation_t operation = operation_snapshot(source);
    cJSON *json = cJSON_AddObjectToObject(parent, "operation");
    cJSON_AddStringToObject(json, "state", operation_state_name(operation.state));
    if (operation.valid) {
        cJSON_AddStringToObject(json, "request_id", operation.request_id);
    } else {
        cJSON_AddNullToObject(json, "request_id");
    }
    cJSON_AddStringToObject(json, "message", operation.message);
    if (operation.error == ESP_OK) {
        cJSON_AddNullToObject(json, "error");
    } else {
        cJSON_AddStringToObject(json, "error", esp_err_to_name(operation.error));
    }
}

static void generate_session_token(char output[33])
{
    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        snprintf(output + i * 2, 3, "%02x", random_bytes[i]);
    }
}

static void session_timer_callback(void *arg)
{
    (void)arg;
    if (s_close_task == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_portal_mux);
    if (s_session.active && !s_session.closing) {
        s_session.closing = true;
        s_session.close_reason = APP_WIFI_PORTAL_CLOSE_TIMEOUT;
        s_session.deadline_us = 0;
    }
    const bool close = s_session.active && s_session.closing;
    portEXIT_CRITICAL(&s_portal_mux);
    if (close) {
        xTaskNotifyGive(s_close_task);
    }
}

static void close_worker(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        app_wifi_portal_callbacks_t callbacks;
        void *ctx;
        app_wifi_portal_close_reason_t reason;
        portENTER_CRITICAL(&s_portal_mux);
        callbacks = s_callbacks;
        ctx = s_callbacks_ctx;
        reason = s_session.close_reason;
        portEXIT_CRITICAL(&s_portal_mux);

        esp_err_t ret = callbacks.set_portal_enabled != NULL
            ? callbacks.set_portal_enabled(false, reason, ctx)
            : app_wifi_stop_softap();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Could not close local portal (%s): %s",
                     close_reason_name(reason), esp_err_to_name(ret));
            portENTER_CRITICAL(&s_portal_mux);
            if (s_session.active) {
                s_session.closing = false;
                s_session.deadline_us = esp_timer_get_time() + 30LL * 1000000LL;
            }
            portEXIT_CRITICAL(&s_portal_mux);
            esp_timer_stop(s_session_timer);
            esp_timer_start_once(s_session_timer, 30LL * 1000000LL);
        }
    }
}

static esp_err_t ensure_portal_runtime(void);

esp_err_t app_wifi_portal_register_callbacks(
    const app_wifi_portal_callbacks_t *callbacks, void *ctx)
{
    ESP_RETURN_ON_FALSE(callbacks != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Portal callbacks are required");
    ESP_RETURN_ON_ERROR(ensure_portal_runtime(), TAG,
                        "Portal runtime initialization failed");
    portENTER_CRITICAL(&s_portal_mux);
    s_callbacks = *callbacks;
    s_callbacks_ctx = ctx;
    portEXIT_CRITICAL(&s_portal_mux);
    return ESP_OK;
}

esp_err_t app_wifi_portal_get_session_status(
    app_wifi_portal_session_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Session status output is required");
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_portal_mux);
    status->active = s_session.active;
    status->closing = s_session.closing;
    status->extended = s_session.extended;
    status->can_extend = s_session.active && !s_session.closing;
    status->close_reason = s_session.close_reason;
    const int64_t remaining = s_session.deadline_us > now
        ? s_session.deadline_us - now : 0;
    status->remaining_seconds = (uint32_t)((remaining + 999999LL) / 1000000LL);
    portEXIT_CRITICAL(&s_portal_mux);
    return ESP_OK;
}

void app_wifi_portal_on_softap_started(void)
{
    if (ensure_portal_runtime() != ESP_OK) {
        ESP_LOGE(TAG, "Portal session could not start");
        return;
    }
    char token[33];
    generate_session_token(token);
    const int64_t deadline = esp_timer_get_time() +
                             (int64_t)PORTAL_SESSION_SECONDS * 1000000LL;
    portENTER_CRITICAL(&s_portal_mux);
    memset(&s_session, 0, sizeof(s_session));
    s_session.active = true;
    s_session.close_reason = APP_WIFI_PORTAL_CLOSE_CLOUD;
    s_session.deadline_us = deadline;
    snprintf(s_session.token, sizeof(s_session.token), "%s", token);
    portEXIT_CRITICAL(&s_portal_mux);
    esp_timer_stop(s_session_timer);
    esp_err_t ret = esp_timer_start_once(
        s_session_timer, (uint64_t)PORTAL_SESSION_SECONDS * 1000000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Could not arm portal timeout: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "Local configuration session started for %u seconds",
             PORTAL_SESSION_SECONDS);
}

void app_wifi_portal_on_softap_stopped(void)
{
    if (s_session_timer != NULL) {
        esp_timer_stop(s_session_timer);
    }
    portENTER_CRITICAL(&s_portal_mux);
    s_session.active = false;
    s_session.closing = false;
    s_session.deadline_us = 0;
    memset(s_session.token, 0, sizeof(s_session.token));
    portEXIT_CRITICAL(&s_portal_mux);
}

static bool request_from_softap(httpd_req_t *req)
{
    esp_netif_t *ap_netif = app_wifi_get_ap_netif();
    if (!app_wifi_softap_is_started() || ap_netif == NULL) {
        return false;
    }
    struct sockaddr_storage peer = {0};
    struct sockaddr_storage local = {0};
    socklen_t peer_len = sizeof(peer);
    socklen_t local_len = sizeof(local);
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0 || getpeername(fd, (struct sockaddr *)&peer, &peer_len) != 0 ||
            getsockname(fd, (struct sockaddr *)&local, &local_len) != 0 ||
            peer.ss_family != AF_INET || local.ss_family != AF_INET) {
        return false;
    }
    esp_netif_ip_info_t ap_ip = {0};
    if (esp_netif_get_ip_info(ap_netif, &ap_ip) != ESP_OK) {
        return false;
    }
    const struct sockaddr_in *peer_v4 = (const struct sockaddr_in *)&peer;
    const struct sockaddr_in *local_v4 = (const struct sockaddr_in *)&local;
    return (peer_v4->sin_addr.s_addr & ap_ip.netmask.addr) ==
           (ap_ip.ip.addr & ap_ip.netmask.addr) &&
           local_v4->sin_addr.s_addr == ap_ip.ip.addr;
}

static void set_response_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *root)
{
    set_response_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }
    esp_err_t ret = httpd_resp_sendstr(req, payload);
    free(payload);
    return ret;
}

static esp_err_t send_error(httpd_req_t *req, const char *status,
                            const char *error, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", atoi(status));
    cJSON_AddStringToObject(root, "error", error);
    cJSON_AddStringToObject(root, "message", message);
    return send_json(req, status, root);
}

static bool constant_time_equal(const char *left, const char *right)
{
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    unsigned difference = (unsigned)(left_len ^ right_len);
    size_t length = left_len > right_len ? left_len : right_len;
    for (size_t i = 0; i < length; ++i) {
        unsigned char a = i < left_len ? (unsigned char)left[i] : 0;
        unsigned char b = i < right_len ? (unsigned char)right[i] : 0;
        difference |= (unsigned)(a ^ b);
    }
    return difference == 0;
}

bool app_wifi_portal_authorize_request(httpd_req_t *req, bool require_token,
                                       bool allow_closing)
{
    if (!request_from_softap(req)) {
        send_error(req, "403 Forbidden", "HOTSPOT_ONLY",
                   "配置接口只能通过机器人热点访问");
        return false;
    }
    if (!require_token) {
        return true;
    }
    size_t length = httpd_req_get_hdr_value_len(req, "X-Portal-Token");
    if (length == 0 || length >= 40) {
        send_error(req, "401 Unauthorized", "TOKEN_REQUIRED",
                   "配置会话令牌无效，请刷新页面");
        return false;
    }
    char supplied[40] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Portal-Token", supplied,
                                    sizeof(supplied)) != ESP_OK) {
        send_error(req, "401 Unauthorized", "TOKEN_REQUIRED",
                   "配置会话令牌无效，请刷新页面");
        return false;
    }
    char expected[33];
    bool active;
    portENTER_CRITICAL(&s_portal_mux);
    snprintf(expected, sizeof(expected), "%s", s_session.token);
    active = s_session.active && (allow_closing || !s_session.closing);
    portEXIT_CRITICAL(&s_portal_mux);
    if (!active || !constant_time_equal(supplied, expected)) {
        send_error(req, "401 Unauthorized", "SESSION_EXPIRED",
                   "配置会话已结束，请重新开启本地配置热点");
        return false;
    }
    return true;
}

static cJSON *receive_json(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= PORTAL_REQUEST_BODY_MAX) {
        return NULL;
    }
    char body[PORTAL_REQUEST_BODY_MAX] = {0};
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received,
                                 req->content_len - received);
        if (ret <= 0) {
            return NULL;
        }
        received += (size_t)ret;
    }
    body[received] = '\0';
    return cJSON_Parse(body);
}

static bool get_device_status(app_wifi_portal_device_status_t *status,
                              esp_err_t *status_error)
{
    memset(status, 0, sizeof(*status));
    status->modem.registration = -1;
    status->modem.rssi = -1;
    app_wifi_portal_callbacks_t callbacks;
    void *ctx;
    portENTER_CRITICAL(&s_portal_mux);
    callbacks = s_callbacks;
    ctx = s_callbacks_ctx;
    portEXIT_CRITICAL(&s_portal_mux);
    if (callbacks.get_device_status == NULL) {
        *status_error = ESP_ERR_NOT_SUPPORTED;
        return false;
    }
    *status_error = callbacks.get_device_status(status, ctx);
    return *status_error == ESP_OK;
}

static void reconcile_external_operations(
    const app_wifi_portal_device_status_t *device)
{
    portal_operation_t modem = operation_snapshot(&s_modem_operation);
    const int64_t now = esp_timer_get_time();
    const bool modem_pending = operation_pending(&modem);
    const bool modem_timed_out = modem.valid &&
        modem.state == APP_WIFI_PORTAL_OPERATION_ERROR &&
        modem.error == ESP_ERR_TIMEOUT;
    if (modem_pending || modem_timed_out) {
        const bool matching_request = device->modem.request_id[0] != '\0' &&
            strcmp(device->modem.request_id, modem.request_id) == 0;
        if (matching_request) {
            if (device->modem.state == APP_WIFI_PORTAL_OPERATION_SUCCESS) {
                set_operation_state(
                    &s_modem_operation, APP_WIFI_PORTAL_OPERATION_SUCCESS,
                    ESP_OK, device->modem.reboot_required
                        ? "设置已保存，请断电重启设备后生效"
                        : "4G 操作已生效");
            } else if (device->modem.state == APP_WIFI_PORTAL_OPERATION_ERROR) {
                set_operation_state(&s_modem_operation,
                                    APP_WIFI_PORTAL_OPERATION_ERROR, ESP_FAIL,
                                    device->modem.error[0] != '\0'
                                        ? device->modem.error : "4G 操作失败");
            } else if (modem_pending &&
                       now - modem.submitted_us > PORTAL_MODEM_TIMEOUT_US) {
                set_operation_state(&s_modem_operation,
                                    APP_WIFI_PORTAL_OPERATION_ERROR,
                                    ESP_ERR_TIMEOUT, "等待 4G 操作结果超时");
            } else if (modem_pending &&
                       (device->modem.state == APP_WIFI_PORTAL_OPERATION_APPLYING ||
                        device->modem.state == APP_WIFI_PORTAL_OPERATION_QUEUED)) {
                set_operation_state(&s_modem_operation,
                                    APP_WIFI_PORTAL_OPERATION_APPLYING, ESP_OK,
                                    "4G 模块正在处理");
            }
        } else if (modem_pending &&
                   now - modem.submitted_us > PORTAL_MODEM_TIMEOUT_US) {
            set_operation_state(&s_modem_operation,
                                APP_WIFI_PORTAL_OPERATION_ERROR,
                                ESP_ERR_TIMEOUT, "等待 4G 操作结果超时");
        }
    }

    portal_operation_t video = operation_snapshot(&s_video_operation);
    if (operation_pending(&video)) {
        if (!device->video.pending && device->video.error[0] == '\0') {
            set_operation_state(&s_video_operation,
                                APP_WIFI_PORTAL_OPERATION_SUCCESS, ESP_OK,
                                "视频设置已应用");
        } else if (device->video.error[0] != '\0') {
            set_operation_state(&s_video_operation,
                                APP_WIFI_PORTAL_OPERATION_ERROR, ESP_FAIL,
                                device->video.error);
        } else if (now - video.submitted_us > PORTAL_VIDEO_TIMEOUT_US) {
            set_operation_state(&s_video_operation,
                                APP_WIFI_PORTAL_OPERATION_ERROR,
                                ESP_ERR_TIMEOUT, "等待相机应用设置超时");
        }
    }
}

static void add_session_json(cJSON *parent, bool include_token)
{
    app_wifi_portal_session_status_t status = {0};
    app_wifi_portal_get_session_status(&status);
    cJSON_AddBoolToObject(parent, "active", status.active);
    cJSON_AddBoolToObject(parent, "closing", status.closing);
    cJSON_AddNumberToObject(parent, "remaining_seconds",
                            status.remaining_seconds);
    cJSON_AddBoolToObject(parent, "extended", status.extended);
    cJSON_AddBoolToObject(parent, "can_extend", status.can_extend);
    portENTER_CRITICAL(&s_portal_mux);
    const uint32_t extension_count = s_session.extension_count;
    portEXIT_CRITICAL(&s_portal_mux);
    cJSON_AddNumberToObject(parent, "initial_session_seconds",
                            PORTAL_SESSION_SECONDS);
    cJSON_AddNumberToObject(parent, "extension_count", extension_count);
    cJSON_AddNumberToObject(parent, "max_extension_minutes",
                            PORTAL_EXTENSION_MAX_MINUTES);
    cJSON_AddStringToObject(parent, "close_reason",
                            close_reason_name(status.close_reason));
    if (include_token) {
        char token[33];
        portENTER_CRITICAL(&s_portal_mux);
        snprintf(token, sizeof(token), "%s", s_session.token);
        portEXIT_CRITICAL(&s_portal_mux);
        cJSON_AddStringToObject(parent, "session_token", token);
    }
}

static void add_wifi_json(cJSON *data)
{
    app_wifi_status_t status = {0};
    esp_err_t ret = app_wifi_get_status(&status);
    cJSON *wifi = cJSON_AddObjectToObject(data, "wifi");
    cJSON_AddBoolToObject(wifi, "feature_enabled", status.feature_enabled);
    cJSON_AddBoolToObject(wifi, "credentials_saved", status.credentials_saved);
    cJSON_AddBoolToObject(wifi, "scanning", status.scanning);
    cJSON_AddBoolToObject(wifi, "connected", status.connected);
    cJSON_AddStringToObject(wifi, "ssid", status.ssid);
    cJSON_AddStringToObject(wifi, "ip", status.ip);
    if (status.rssi_valid) {
        cJSON_AddNumberToObject(wifi, "rssi", status.rssi);
    } else {
        cJSON_AddNullToObject(wifi, "rssi");
    }
    cJSON_AddBoolToObject(wifi, "wifi_uplink_selected",
                          status.wifi_uplink_selected);
    const char *uplink = status.active_uplink == APP_WIFI_UPLINK_WIFI ? "wifi"
        : (status.active_uplink == APP_WIFI_UPLINK_L610 ? "l610" : "none");
    cJSON_AddStringToObject(wifi, "active_uplink", uplink);
    cJSON_AddStringToObject(wifi, "error",
                            ret == ESP_OK ? status.error : esp_err_to_name(ret));

    cJSON *networks = cJSON_AddArrayToObject(wifi, "networks");
    app_wifi_network_t records[APP_WIFI_MAX_SCAN_RESULTS];
    size_t count;
    portENTER_CRITICAL(&s_portal_mux);
    count = s_scan_result_count;
    memcpy(records, s_scan_results, count * sizeof(records[0]));
    portEXIT_CRITICAL(&s_portal_mux);
    for (size_t i = 0; i < count; ++i) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(network, "channel", records[i].channel);
        cJSON_AddStringToObject(network, "security", records[i].security);
        cJSON_AddBoolToObject(network, "secured", records[i].secured);
        cJSON_AddBoolToObject(network, "supported", records[i].supported);
        cJSON_AddItemToArray(networks, network);
    }
    add_operation_json(wifi, &s_wifi_operation);
}

static void add_modem_json(cJSON *data,
                           const app_wifi_portal_modem_status_t *status)
{
    cJSON *modem = cJSON_AddObjectToObject(data, "modem");
    cJSON_AddBoolToObject(modem, "supported", status->supported);
    cJSON_AddBoolToObject(modem, "at_ready", status->at_ready);
    cJSON_AddBoolToObject(modem, "sim_ready", status->sim_ready);
    cJSON_AddBoolToObject(modem, "rndis_ready", status->rndis_ready);
    cJSON_AddStringToObject(modem, "state", operation_state_name(status->state));
    if (status->request_id[0] != '\0') {
        cJSON_AddStringToObject(modem, "request_id", status->request_id);
    } else {
        cJSON_AddNullToObject(modem, "request_id");
    }
    cJSON_AddStringToObject(modem, "action", modem_action_name(status->action));
    cJSON_AddBoolToObject(modem, "reboot_required", status->reboot_required);
    cJSON_AddStringToObject(modem, "error", status->error);
    cJSON_AddStringToObject(modem, "operator", status->operator_name);
    cJSON_AddNumberToObject(modem, "registration", status->registration);
    if (status->rssi >= 0) {
        cJSON_AddNumberToObject(modem, "rssi", status->rssi);
    } else {
        cJSON_AddNullToObject(modem, "rssi");
    }
    cJSON_AddStringToObject(modem, "rat", status->rat);
    cJSON_AddStringToObject(modem, "band_config", status->band_config);
    cJSON_AddStringToObject(modem, "cell_lock_config",
                            status->cell_lock_config);
    cJSON *cells = cJSON_AddArrayToObject(modem, "cells");
    size_t count = status->cell_count > APP_WIFI_PORTAL_MODEM_MAX_CELLS
        ? APP_WIFI_PORTAL_MODEM_MAX_CELLS : status->cell_count;
    for (size_t i = 0; i < count; ++i) {
        const app_wifi_portal_modem_cell_t *source = &status->cells[i];
        cJSON *cell = cJSON_CreateObject();
        cJSON_AddBoolToObject(cell, "serving", source->serving);
        cJSON_AddNumberToObject(cell, "mcc", source->mcc);
        cJSON_AddNumberToObject(cell, "mnc", source->mnc);
        cJSON_AddStringToObject(cell, "tac", source->tac);
        cJSON_AddStringToObject(cell, "cell_id", source->cell_id);
        cJSON_AddNumberToObject(cell, "earfcn", source->earfcn);
        cJSON_AddNumberToObject(cell, "pci", source->pci);
        cJSON_AddNumberToObject(cell, "band", source->band);
        cJSON_AddNumberToObject(cell, "rxlev", source->rxlev);
        cJSON_AddNumberToObject(cell, "rsrp", source->rsrp);
        cJSON_AddNumberToObject(cell, "rsrq", source->rsrq);
        cJSON_AddItemToArray(cells, cell);
    }
    add_operation_json(modem, &s_modem_operation);
}

static void add_video_json(cJSON *data,
                           const app_wifi_portal_video_status_t *status)
{
    cJSON *video = cJSON_AddObjectToObject(data, "video");
    cJSON_AddBoolToObject(video, "supported", status->supported);
    cJSON_AddBoolToObject(video, "camera_ready", status->camera_ready);
    cJSON_AddBoolToObject(video, "pending", status->pending);
    cJSON_AddBoolToObject(video, "local_mjpeg_enabled",
                          status->local_mjpeg_enabled);
    cJSON_AddNumberToObject(video, "fps", status->fps);
    cJSON_AddNumberToObject(video, "width", status->width);
    cJSON_AddNumberToObject(video, "height", status->height);
    cJSON_AddStringToObject(video, "resolution", status->resolution);
    cJSON_AddStringToObject(video, "error", status->error);
    cJSON_AddStringToObject(video, "mjpeg_url",
                            "http://192.168.4.1:8080/live/mjpeg");
    add_operation_json(video, &s_video_operation);
}

static cJSON *create_full_status(void)
{
    app_wifi_portal_device_status_t device;
    esp_err_t device_error = ESP_OK;
    get_device_status(&device, &device_error);
    reconcile_external_operations(&device);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", 200);
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "device_id", device.device_id);
    if (device_error == ESP_OK) {
        cJSON_AddNullToObject(data, "snapshot_error");
    } else {
        cJSON_AddStringToObject(data, "snapshot_error",
                                esp_err_to_name(device_error));
    }
    cJSON *session = cJSON_AddObjectToObject(data, "session");
    add_session_json(session, false);
    add_wifi_json(data);
    add_modem_json(data, &device.modem);
    add_video_json(data, &device.video);
    return root;
}

static esp_err_t send_operation_response(httpd_req_t *req,
                                         portal_operation_t *operation)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", 202);
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    add_operation_json(data, operation);
    return send_json(req, "202 Accepted", root);
}

static esp_err_t handle_accept_result(httpd_req_t *req,
                                      portal_accept_result_t result,
                                      portal_operation_t *operation)
{
    if (result == PORTAL_ACCEPTED || result == PORTAL_DUPLICATE) {
        return send_operation_response(req, operation);
    }
    if (result == PORTAL_REUSED_ID) {
        return send_error(req, "409 Conflict", "REQUEST_ID_REUSED",
                          "相同 request_id 不能用于不同操作");
    }
    return send_error(req, "409 Conflict", "BUSY",
                      "该子系统正在处理上一项操作");
}

static esp_err_t enqueue_job(const portal_job_t *job,
                             portal_operation_t *operation)
{
    if (s_work_queue == NULL ||
            xQueueSend(s_work_queue, job, 0) != pdTRUE) {
        set_operation_state(operation, APP_WIFI_PORTAL_OPERATION_ERROR,
                            ESP_ERR_TIMEOUT, "操作队列已满");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void process_wifi_job(portal_job_t *job)
{
    set_operation_state(&s_wifi_operation,
                        APP_WIFI_PORTAL_OPERATION_APPLYING, ESP_OK,
                        "正在处理 Wi-Fi 操作");
    esp_err_t ret = ESP_OK;
    switch (job->data.wifi.action) {
    case PORTAL_WIFI_QUERY:
        break;
    case PORTAL_WIFI_SET_ENABLED:
        ret = app_wifi_set_sta_feature_enabled(job->data.wifi.enabled);
        if (ret == ESP_OK && !job->data.wifi.enabled) {
            portENTER_CRITICAL(&s_portal_mux);
            s_scan_result_count = 0;
            portEXIT_CRITICAL(&s_portal_mux);
        }
        break;
    case PORTAL_WIFI_SCAN:
        {
            app_wifi_network_t records[APP_WIFI_MAX_SCAN_RESULTS] = {0};
            size_t count = 0;
            ret = app_wifi_scan_networks(records, APP_WIFI_MAX_SCAN_RESULTS,
                                         &count);
            if (ret == ESP_OK) {
                portENTER_CRITICAL(&s_portal_mux);
                memcpy(s_scan_results, records, count * sizeof(records[0]));
                s_scan_result_count = count;
                portEXIT_CRITICAL(&s_portal_mux);
            }
        }
        break;
    case PORTAL_WIFI_CONNECT:
        ret = app_wifi_connect_sta(job->data.wifi.ssid,
                                   job->data.wifi.password,
                                   job->data.wifi.security);
        if (ret == ESP_OK) {
            ret = ESP_ERR_TIMEOUT;
            for (int attempt = 0; attempt < 80; ++attempt) {
                app_wifi_status_t status = {0};
                if (app_wifi_get_status(&status) == ESP_OK) {
                    if (status.connected &&
                            strcmp(status.ssid, job->data.wifi.ssid) == 0) {
                        ret = ESP_OK;
                        break;
                    }
                    if (status.error[0] != '\0') {
                        ret = ESP_FAIL;
                        break;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        }
        break;
    case PORTAL_WIFI_SELECT_UPLINK:
        ret = app_wifi_select_cloud_uplink(job->data.wifi.use_wifi);
        break;
    default:
        ret = ESP_ERR_INVALID_ARG;
        break;
    }
    memset(job->data.wifi.password, 0, sizeof(job->data.wifi.password));
    set_operation_state(
        &s_wifi_operation,
        ret == ESP_OK ? APP_WIFI_PORTAL_OPERATION_SUCCESS
                      : APP_WIFI_PORTAL_OPERATION_ERROR,
        ret, ret == ESP_OK ? "Wi-Fi 操作已完成" : "Wi-Fi 操作失败");
}

static void process_modem_job(const portal_job_t *job)
{
    app_wifi_portal_callbacks_t callbacks;
    void *ctx;
    portENTER_CRITICAL(&s_portal_mux);
    callbacks = s_callbacks;
    ctx = s_callbacks_ctx;
    portEXIT_CRITICAL(&s_portal_mux);
    if (callbacks.submit_modem_command == NULL) {
        set_operation_state(&s_modem_operation,
                            APP_WIFI_PORTAL_OPERATION_ERROR,
                            ESP_ERR_NOT_SUPPORTED, "当前固件不支持 4G 管理");
        return;
    }
    set_operation_state(&s_modem_operation,
                        APP_WIFI_PORTAL_OPERATION_APPLYING, ESP_OK,
                        "4G 操作已提交");
    esp_err_t ret = callbacks.submit_modem_command(&job->data.modem, ctx);
    if (ret != ESP_OK) {
        set_operation_state(&s_modem_operation,
                            APP_WIFI_PORTAL_OPERATION_ERROR, ret,
                            ret == ESP_ERR_INVALID_STATE
                                ? "4G AT 接口尚未就绪"
                                : "4G 操作提交失败");
    }
}

static void process_video_job(const portal_job_t *job)
{
    app_wifi_portal_callbacks_t callbacks;
    void *ctx;
    portENTER_CRITICAL(&s_portal_mux);
    callbacks = s_callbacks;
    ctx = s_callbacks_ctx;
    portEXIT_CRITICAL(&s_portal_mux);
    if (callbacks.apply_video_settings == NULL) {
        set_operation_state(&s_video_operation,
                            APP_WIFI_PORTAL_OPERATION_ERROR,
                            ESP_ERR_NOT_SUPPORTED, "当前固件不支持视频设置");
        return;
    }
    set_operation_state(&s_video_operation,
                        APP_WIFI_PORTAL_OPERATION_APPLYING, ESP_OK,
                        "正在应用视频设置");
    esp_err_t ret = callbacks.apply_video_settings(&job->data.video, ctx);
    if (ret != ESP_OK) {
        set_operation_state(&s_video_operation,
                            APP_WIFI_PORTAL_OPERATION_ERROR, ret,
                            "视频设置应用失败");
        return;
    }
    app_wifi_portal_device_status_t status;
    esp_err_t status_error = ESP_OK;
    bool snapshot_ok = get_device_status(&status, &status_error);
    set_operation_state(&s_video_operation,
                        APP_WIFI_PORTAL_OPERATION_SUCCESS, ESP_OK,
                        snapshot_ok && status.video.camera_ready &&
                                !status.video.pending
                            ? "视频设置已应用"
                            : "视频设置已保存，将在相机启动后应用");
}

static void operation_worker(void *arg)
{
    (void)arg;
    while (true) {
        portal_job_t job = {0};
        if (xQueueReceive(s_work_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (job.type == PORTAL_JOB_WIFI) {
            process_wifi_job(&job);
        } else if (job.type == PORTAL_JOB_MODEM) {
            process_modem_job(&job);
        } else if (job.type == PORTAL_JOB_VIDEO) {
            process_video_job(&job);
        }
        memset(&job, 0, sizeof(job));
    }
}

static esp_err_t ensure_portal_runtime(void)
{
    if (s_work_queue == NULL) {
        s_work_queue = xQueueCreate(PORTAL_WORK_QUEUE_LENGTH,
                                    sizeof(portal_job_t));
        ESP_RETURN_ON_FALSE(s_work_queue != NULL, ESP_ERR_NO_MEM, TAG,
                            "Could not create portal work queue");
    }
    if (s_worker_task == NULL) {
        BaseType_t created = xTaskCreate(operation_worker, "portal_ops", 6144,
                                         NULL, 5, &s_worker_task);
        ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG,
                            "Could not create portal operation worker");
    }
    if (s_close_task == NULL) {
        BaseType_t created = xTaskCreate(close_worker, "portal_close", 3072,
                                         NULL, 6, &s_close_task);
        ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG,
                            "Could not create portal close worker");
    }
    if (s_session_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = session_timer_callback,
            .name = "portal_session",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_session_timer), TAG,
                            "Could not create portal timer");
    }
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, false, true)) {
        return ESP_OK;
    }
    set_response_headers(req);
    httpd_resp_set_hdr(
        req, "Content-Security-Policy",
        "default-src 'self'; img-src 'self' data: http://192.168.4.1:8080; "
        "style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; "
        "connect-src 'self'");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    size_t length = (size_t)(portal_index_html_end - portal_index_html_start);
    if (length > 0 && portal_index_html_start[length - 1] == '\0') {
        --length;
    }
    return httpd_resp_send(req, (const char *)portal_index_html_start, length);
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, false, true)) {
        return ESP_OK;
    }
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t session_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, false, true)) {
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", 200);
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "name", "机器人现场配置台");
    cJSON_AddStringToObject(data, "subtitle", "连接机器人热点后使用");
    add_session_json(data, true);
    cJSON_AddStringToObject(data, "mjpeg_url",
                            "http://192.168.4.1:8080/live/mjpeg");
    cJSON *capabilities = cJSON_AddObjectToObject(data, "capabilities");
    cJSON_AddBoolToObject(capabilities, "wifi", true);
    app_wifi_portal_callbacks_t callbacks;
    portENTER_CRITICAL(&s_portal_mux);
    callbacks = s_callbacks;
    portEXIT_CRITICAL(&s_portal_mux);
    cJSON_AddBoolToObject(capabilities, "modem",
                          callbacks.submit_modem_command != NULL);
    cJSON_AddBoolToObject(capabilities, "video",
                          callbacks.apply_video_settings != NULL);
    return send_json(req, "200 OK", root);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, false, true)) {
        return ESP_OK;
    }
    return send_json(req, "200 OK", create_full_status());
}

static portal_wifi_action_t parse_wifi_action(const char *name, bool *valid)
{
    *valid = true;
    if (strcmp(name, "query") == 0) return PORTAL_WIFI_QUERY;
    if (strcmp(name, "set_enabled") == 0) return PORTAL_WIFI_SET_ENABLED;
    if (strcmp(name, "scan") == 0) return PORTAL_WIFI_SCAN;
    if (strcmp(name, "connect") == 0) return PORTAL_WIFI_CONNECT;
    if (strcmp(name, "select_uplink") == 0) return PORTAL_WIFI_SELECT_UPLINK;
    *valid = false;
    return PORTAL_WIFI_QUERY;
}

static esp_err_t wifi_commands_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, true, false)) {
        return ESP_OK;
    }
    cJSON *body = receive_json(req);
    cJSON *request_id = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "request_id");
    cJSON *action = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "action");
    if (!cJSON_IsString(request_id) || !request_id_valid(request_id->valuestring) ||
            !cJSON_IsString(action)) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "INVALID_REQUEST",
                          "request_id 或 action 无效");
    }
    bool valid_action;
    portal_job_t job = {
        .type = PORTAL_JOB_WIFI,
    };
    job.data.wifi.action = parse_wifi_action(action->valuestring, &valid_action);
    snprintf(job.request_id, sizeof(job.request_id), "%s",
             request_id->valuestring);
    if (!valid_action) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "INVALID_ACTION",
                          "不支持的 Wi-Fi 操作");
    }

    uint32_t fingerprint = hash_string(2166136261U, action->valuestring);
    if (job.data.wifi.action == PORTAL_WIFI_SET_ENABLED) {
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(body, "enabled");
        if (!cJSON_IsBool(enabled)) {
            cJSON_Delete(body);
            return send_error(req, "400 Bad Request", "INVALID_ENABLED",
                              "enabled 必须是布尔值");
        }
        job.data.wifi.enabled = cJSON_IsTrue(enabled);
        fingerprint = hash_bytes(fingerprint, &job.data.wifi.enabled,
                                 sizeof(job.data.wifi.enabled));
    } else if (job.data.wifi.action == PORTAL_WIFI_CONNECT) {
        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(body, "ssid");
        cJSON *password = cJSON_GetObjectItemCaseSensitive(body, "password");
        cJSON *security = cJSON_GetObjectItemCaseSensitive(body, "security");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(password) ||
                !cJSON_IsString(security) || strlen(ssid->valuestring) > 32 ||
                strlen(password->valuestring) > 63 ||
                strlen(security->valuestring) >= sizeof(job.data.wifi.security)) {
            cJSON_Delete(body);
            return send_error(req, "400 Bad Request", "INVALID_CREDENTIALS",
                              "Wi-Fi 名称、密码或加密方式无效");
        }
        snprintf(job.data.wifi.ssid, sizeof(job.data.wifi.ssid), "%s",
                 ssid->valuestring);
        snprintf(job.data.wifi.password, sizeof(job.data.wifi.password), "%s",
                 password->valuestring);
        snprintf(job.data.wifi.security, sizeof(job.data.wifi.security), "%s",
                 security->valuestring);
        fingerprint = hash_string(fingerprint, job.data.wifi.ssid);
        fingerprint = hash_string(fingerprint, job.data.wifi.password);
        fingerprint = hash_string(fingerprint, job.data.wifi.security);
    } else if (job.data.wifi.action == PORTAL_WIFI_SELECT_UPLINK) {
        cJSON *use_wifi = cJSON_GetObjectItemCaseSensitive(body, "use_wifi");
        if (!cJSON_IsBool(use_wifi)) {
            cJSON_Delete(body);
            return send_error(req, "400 Bad Request", "INVALID_UPLINK",
                              "use_wifi 必须是布尔值");
        }
        job.data.wifi.use_wifi = cJSON_IsTrue(use_wifi);
        fingerprint = hash_bytes(fingerprint, &job.data.wifi.use_wifi,
                                 sizeof(job.data.wifi.use_wifi));
    }
    cJSON_Delete(body);

    portal_accept_result_t accepted = accept_operation(
        &s_wifi_operation, job.request_id, fingerprint);
    if (accepted == PORTAL_ACCEPTED &&
            enqueue_job(&job, &s_wifi_operation) != ESP_OK) {
        return send_error(req, "503 Service Unavailable", "QUEUE_FULL",
                          "Wi-Fi 操作队列已满");
    }
    return handle_accept_result(req, accepted, &s_wifi_operation);
}

static app_wifi_portal_modem_action_t parse_modem_action(const char *name,
                                                          bool *valid)
{
    *valid = true;
    if (strcmp(name, "query") == 0) return APP_WIFI_PORTAL_MODEM_QUERY;
    if (strcmp(name, "reselect") == 0) return APP_WIFI_PORTAL_MODEM_RESELECT;
    if (strcmp(name, "set_bands") == 0 || strcmp(name, "restore_auto") == 0) {
        return APP_WIFI_PORTAL_MODEM_SET_BANDS;
    }
    if (strcmp(name, "set_cell_lock") == 0) {
        return APP_WIFI_PORTAL_MODEM_SET_CELL_LOCK;
    }
    if (strcmp(name, "clear_cell_lock") == 0) {
        return APP_WIFI_PORTAL_MODEM_CLEAR_CELL_LOCK;
    }
    *valid = false;
    return APP_WIFI_PORTAL_MODEM_QUERY;
}

static bool parse_band_list(const cJSON *array,
                            app_wifi_portal_modem_command_t *command)
{
    if (!cJSON_IsArray(array)) {
        return false;
    }
    int count = cJSON_GetArraySize(array);
    if (count <= 0 || count > APP_WIFI_PORTAL_MODEM_MAX_BANDS) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint) {
            return false;
        }
        bool supported = false;
        for (size_t band_index = 0;
                band_index < sizeof(s_supported_lte_bands) /
                             sizeof(s_supported_lte_bands[0]);
                ++band_index) {
            if (item->valueint == s_supported_lte_bands[band_index]) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            return false;
        }
        for (int previous = 0; previous < i; ++previous) {
            if (command->bands[previous] == (uint16_t)item->valueint) {
                return false;
            }
        }
        command->bands[i] = (uint16_t)item->valueint;
    }
    command->band_count = (size_t)count;
    return true;
}

static esp_err_t modem_commands_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, true, false)) {
        return ESP_OK;
    }
    cJSON *body = receive_json(req);
    cJSON *request_id = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "request_id");
    cJSON *action = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "action");
    if (!cJSON_IsString(request_id) || !request_id_valid(request_id->valuestring) ||
            !cJSON_IsString(action)) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "INVALID_REQUEST",
                          "request_id 或 action 无效");
    }

    portal_job_t job = {
        .type = PORTAL_JOB_MODEM,
    };
    bool valid_action;
    job.data.modem.action = parse_modem_action(action->valuestring,
                                               &valid_action);
    snprintf(job.request_id, sizeof(job.request_id), "%s",
             request_id->valuestring);
    snprintf(job.data.modem.request_id, sizeof(job.data.modem.request_id), "%s",
             request_id->valuestring);
    if (!valid_action) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "INVALID_ACTION",
                          "不支持的 4G 操作");
    }
    if (job.data.modem.action != APP_WIFI_PORTAL_MODEM_QUERY) {
        cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(body, "confirmed");
        if (!cJSON_IsTrue(confirmed)) {
            cJSON_Delete(body);
            return send_error(req, "400 Bad Request", "CONFIRMATION_REQUIRED",
                              "该操作会中断网络，必须先确认");
        }
    }

    uint32_t fingerprint = hash_string(2166136261U, action->valuestring);
    if (strcmp(action->valuestring, "restore_auto") == 0) {
        memcpy(job.data.modem.bands, s_supported_lte_bands,
               sizeof(s_supported_lte_bands));
        job.data.modem.band_count = sizeof(s_supported_lte_bands) /
                                    sizeof(s_supported_lte_bands[0]);
        job.data.modem.reselect_after_bands = true;
        fingerprint = hash_bytes(fingerprint, s_supported_lte_bands,
                                 sizeof(s_supported_lte_bands));
    } else if (job.data.modem.action == APP_WIFI_PORTAL_MODEM_SET_BANDS) {
        cJSON *bands = cJSON_GetObjectItemCaseSensitive(body, "bands");
        if (!parse_band_list(bands, &job.data.modem)) {
            cJSON_Delete(body);
            return send_error(req, "400 Bad Request", "INVALID_BANDS",
                              "频段列表无效");
        }
        fingerprint = hash_bytes(fingerprint, job.data.modem.bands,
                                 job.data.modem.band_count *
                                 sizeof(job.data.modem.bands[0]));
    } else if (job.data.modem.action ==
               APP_WIFI_PORTAL_MODEM_SET_CELL_LOCK) {
        cJSON *earfcn = cJSON_GetObjectItemCaseSensitive(body, "earfcn");
        cJSON *pci = cJSON_GetObjectItemCaseSensitive(body, "pci");
        cJSON *lock_pci = cJSON_GetObjectItemCaseSensitive(body, "lock_pci");
        if (!cJSON_IsNumber(earfcn) || earfcn->valuedouble < 0 ||
                earfcn->valuedouble > UINT32_MAX ||
                !cJSON_IsBool(lock_pci) ||
                (cJSON_IsTrue(lock_pci) &&
                 (!cJSON_IsNumber(pci) || pci->valueint < 0 ||
                  pci->valueint > 503))) {
            cJSON_Delete(body);
            return send_error(req, "400 Bad Request", "INVALID_CELL",
                              "EARFCN 或 PCI 无效");
        }
        job.data.modem.earfcn = (uint32_t)earfcn->valuedouble;
        job.data.modem.lock_pci = cJSON_IsTrue(lock_pci);
        job.data.modem.pci = job.data.modem.lock_pci ? pci->valueint : 0;
        fingerprint = hash_bytes(fingerprint, &job.data.modem.earfcn,
                                 sizeof(job.data.modem.earfcn));
        fingerprint = hash_bytes(fingerprint, &job.data.modem.lock_pci,
                                 sizeof(job.data.modem.lock_pci));
        fingerprint = hash_bytes(fingerprint, &job.data.modem.pci,
                                 sizeof(job.data.modem.pci));
    }
    cJSON_Delete(body);

    app_wifi_portal_device_status_t device;
    esp_err_t status_error = ESP_OK;
    get_device_status(&device, &status_error);
    if (!device.modem.supported) {
        return send_error(req, "501 Not Implemented", "MODEM_UNSUPPORTED",
                          "当前固件不支持 4G 管理");
    }
    if (!device.modem.at_ready) {
        return send_error(req, "409 Conflict", "AT_NOT_READY",
                          "4G AT 接口仍在初始化，请稍后重试");
    }

    portal_accept_result_t accepted = accept_operation(
        &s_modem_operation, job.request_id, fingerprint);
    if (accepted == PORTAL_ACCEPTED &&
            enqueue_job(&job, &s_modem_operation) != ESP_OK) {
        return send_error(req, "503 Service Unavailable", "QUEUE_FULL",
                          "4G 操作队列已满");
    }
    return handle_accept_result(req, accepted, &s_modem_operation);
}

static bool valid_video_fps(int fps)
{
    return fps == 5 || fps == 8 || fps == 15 || fps == 20 || fps == 30;
}

static bool valid_video_resolution(const char *resolution)
{
    return strcmp(resolution, "640x480") == 0 ||
           strcmp(resolution, "1280x720") == 0 ||
           strcmp(resolution, "1920x1080") == 0;
}

static esp_err_t video_settings_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, true, false)) {
        return ESP_OK;
    }
    cJSON *body = receive_json(req);
    cJSON *request_id = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "request_id");
    cJSON *fps = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "fps");
    cJSON *resolution = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "resolution");
    if (!cJSON_IsString(request_id) || !request_id_valid(request_id->valuestring) ||
            !cJSON_IsNumber(fps) || !valid_video_fps(fps->valueint) ||
            !cJSON_IsString(resolution) ||
            !valid_video_resolution(resolution->valuestring)) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "INVALID_VIDEO_SETTINGS",
                          "分辨率或帧率档位无效");
    }
    portal_job_t job = {
        .type = PORTAL_JOB_VIDEO,
    };
    snprintf(job.request_id, sizeof(job.request_id), "%s",
             request_id->valuestring);
    snprintf(job.data.video.request_id,
             sizeof(job.data.video.request_id), "%s",
             request_id->valuestring);
    job.data.video.fps = fps->valueint;
    snprintf(job.data.video.resolution,
             sizeof(job.data.video.resolution), "%s",
             resolution->valuestring);
    uint32_t fingerprint = hash_bytes(2166136261U, &job.data.video.fps,
                                      sizeof(job.data.video.fps));
    fingerprint = hash_string(fingerprint, job.data.video.resolution);
    cJSON_Delete(body);

    portal_accept_result_t accepted = accept_operation(
        &s_video_operation, job.request_id, fingerprint);
    if (accepted == PORTAL_ACCEPTED &&
            enqueue_job(&job, &s_video_operation) != ESP_OK) {
        return send_error(req, "503 Service Unavailable", "QUEUE_FULL",
                          "视频设置队列已满");
    }
    return handle_accept_result(req, accepted, &s_video_operation);
}

static esp_err_t extend_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, true, false)) {
        return ESP_OK;
    }
    cJSON *body = receive_json(req);
    cJSON *request_id = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "request_id");
    cJSON *minutes_json = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "minutes");
    if (!cJSON_IsString(request_id) ||
            !request_id_valid(request_id->valuestring) ||
            (minutes_json != NULL &&
             (!cJSON_IsNumber(minutes_json) ||
              minutes_json->valuedouble != minutes_json->valueint ||
              minutes_json->valueint < 1 ||
              minutes_json->valueint > PORTAL_EXTENSION_MAX_MINUTES))) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "INVALID_REQUEST",
                          "request_id 或延长分钟数无效");
    }
    const uint32_t minutes = minutes_json == NULL
        ? PORTAL_EXTENSION_DEFAULT_MINUTES
        : (uint32_t)minutes_json->valueint;
    char current_request_id[APP_WIFI_PORTAL_REQUEST_ID_MAX + 1];
    snprintf(current_request_id, sizeof(current_request_id), "%s",
             request_id->valuestring);
    cJSON_Delete(body);

    bool allowed;
    bool duplicate;
    bool reused;
    int64_t deadline;
    size_t record_index = 0;
    portENTER_CRITICAL(&s_portal_mux);
    portal_extension_record_t *existing =
        extension_record_locked(current_request_id);
    duplicate = existing != NULL && existing->minutes == minutes;
    reused = strcmp(s_session.close_request_id, current_request_id) == 0 ||
             (existing != NULL && existing->minutes != minutes) ||
             operation_has_request_id_locked(&s_wifi_operation,
                                             current_request_id) ||
             operation_has_request_id_locked(&s_modem_operation,
                                             current_request_id) ||
             operation_has_request_id_locked(&s_video_operation,
                                             current_request_id);
    allowed = !duplicate && !reused && s_session.active &&
              !s_session.closing;
    if (allowed) {
        s_session.extended = true;
        s_session.deadline_us +=
            (int64_t)minutes * 60LL * 1000000LL;
        record_index = s_session.next_extension_record %
                       PORTAL_EXTENSION_HISTORY_SIZE;
        portal_extension_record_t *record =
            &s_session.extension_history[record_index];
        memset(record, 0, sizeof(*record));
        snprintf(record->request_id, sizeof(record->request_id), "%s",
                 current_request_id);
        record->minutes = minutes;
        ++s_session.next_extension_record;
        ++s_session.extension_count;
    }
    deadline = s_session.deadline_us;
    portEXIT_CRITICAL(&s_portal_mux);
    if (reused) {
        return send_error(req, "409 Conflict", "REQUEST_ID_REUSED",
                          "相同 request_id 不能用于不同操作");
    }
    if (duplicate) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "code", 202);
        cJSON *data = cJSON_AddObjectToObject(root, "data");
        add_session_json(data, false);
        cJSON_AddStringToObject(data, "message", "配置时间已经延长");
        return send_json(req, "202 Accepted", root);
    }
    if (!allowed) {
        return send_error(req, "409 Conflict", "EXTENSION_UNAVAILABLE",
                          "当前配置会话不能延长");
    }
    const int64_t remaining = deadline - esp_timer_get_time();
    esp_timer_stop(s_session_timer);
    esp_err_t ret = esp_timer_start_once(
        s_session_timer, remaining > 0 ? (uint64_t)remaining : 1);
    if (ret != ESP_OK) {
        portENTER_CRITICAL(&s_portal_mux);
        s_session.deadline_us -=
            (int64_t)minutes * 60LL * 1000000LL;
        memset(&s_session.extension_history[record_index], 0,
               sizeof(s_session.extension_history[record_index]));
        if (s_session.extension_count > 0) {
            --s_session.extension_count;
        }
        s_session.extended = s_session.extension_count > 0;
        portEXIT_CRITICAL(&s_portal_mux);
        return send_error(req, "500 Internal Server Error", "TIMER_FAILED",
                          "延长配置时间失败");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", 202);
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    add_session_json(data, false);
    char message[64];
    snprintf(message, sizeof(message), "配置时间已延长 %lu 分钟",
             (unsigned long)minutes);
    cJSON_AddStringToObject(data, "message", message);
    return send_json(req, "202 Accepted", root);
}

static esp_err_t close_handler(httpd_req_t *req)
{
    if (!app_wifi_portal_authorize_request(req, true, true)) {
        return ESP_OK;
    }
    cJSON *body = receive_json(req);
    cJSON *request_id = body == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(body, "request_id");
    if (!cJSON_IsString(request_id) ||
            !request_id_valid(request_id->valuestring)) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "INVALID_REQUEST",
                          "request_id 无效");
    }
    char current_request_id[APP_WIFI_PORTAL_REQUEST_ID_MAX + 1];
    snprintf(current_request_id, sizeof(current_request_id), "%s",
             request_id->valuestring);
    cJSON_Delete(body);

    bool accepted;
    bool duplicate;
    bool reused;
    portENTER_CRITICAL(&s_portal_mux);
    duplicate = strcmp(s_session.close_request_id, current_request_id) == 0;
    reused = extension_record_locked(current_request_id) != NULL ||
             request_id_used_by_other_operation_locked(NULL,
                                                       current_request_id);
    accepted = !duplicate && !reused && s_session.active &&
               !s_session.closing;
    if (accepted) {
        s_session.closing = true;
        s_session.close_reason = APP_WIFI_PORTAL_CLOSE_USER;
        s_session.deadline_us = 0;
        snprintf(s_session.close_request_id,
                 sizeof(s_session.close_request_id), "%s",
                 current_request_id);
    }
    portEXIT_CRITICAL(&s_portal_mux);
    if (reused) {
        return send_error(req, "409 Conflict", "REQUEST_ID_REUSED",
                          "相同 request_id 不能用于不同操作");
    }
    if (duplicate) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "code", 202);
        cJSON *data = cJSON_AddObjectToObject(root, "data");
        cJSON_AddStringToObject(data, "state", "closing");
        cJSON_AddStringToObject(data, "message", "热点正在关闭");
        return send_json(req, "202 Accepted", root);
    }
    if (!accepted) {
        return send_error(req, "409 Conflict", "SESSION_CLOSING",
                          "本地配置热点已经在关闭");
    }
    esp_timer_stop(s_session_timer);
    esp_err_t ret = esp_timer_start_once(s_session_timer,
                                         PORTAL_CLOSE_DELAY_US);
    if (ret != ESP_OK) {
        portENTER_CRITICAL(&s_portal_mux);
        s_session.closing = false;
        s_session.close_request_id[0] = '\0';
        portEXIT_CRITICAL(&s_portal_mux);
        return send_error(req, "500 Internal Server Error", "TIMER_FAILED",
                          "无法安排热点关闭操作");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", 202);
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "state", "closing");
    cJSON_AddStringToObject(data, "message", "热点将在 0.5 秒后关闭");
    return send_json(req, "202 Accepted", root);
}

esp_err_t app_wifi_portal_register_http_handlers(httpd_handle_t server)
{
    ESP_RETURN_ON_FALSE(server != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "HTTP server is required");
    ESP_RETURN_ON_ERROR(ensure_portal_runtime(), TAG,
                        "Portal runtime initialization failed");
    const httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_handler },
        { .uri = "/favicon.ico", .method = HTTP_GET,
          .handler = favicon_handler },
        { .uri = "/api/local/v1/session", .method = HTTP_GET,
          .handler = session_handler },
        { .uri = "/api/local/v1/status", .method = HTTP_GET,
          .handler = status_handler },
        { .uri = "/api/local/v1/wifi/commands", .method = HTTP_POST,
          .handler = wifi_commands_handler },
        { .uri = "/api/local/v1/modem/commands", .method = HTTP_POST,
          .handler = modem_commands_handler },
        { .uri = "/api/local/v1/video/settings", .method = HTTP_POST,
          .handler = video_settings_handler },
        { .uri = "/api/local/v1/portal/extend", .method = HTTP_POST,
          .handler = extend_handler },
        { .uri = "/api/local/v1/portal/close", .method = HTTP_POST,
          .handler = close_handler },
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]), TAG,
                            "Could not register portal URI %s", handlers[i].uri);
    }
    ESP_LOGI(TAG, "Robot local configuration portal ready on port 8081");
    return ESP_OK;
}

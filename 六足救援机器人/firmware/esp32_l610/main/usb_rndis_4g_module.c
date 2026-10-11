/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/
#include <stdbool.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "iot_usbh_rndis.h"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "iot_usbh_cdc.h"
#include "at_3gpp_ts_27_007.h"
#include "camera_stream.h"
#include "telemetry_uart.h"
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
#include "app_wifi.h"
#endif

static const char *TAG = "RNDIS_4G_MODULE";

static EventGroupHandle_t s_event_group;
static iot_eth_driver_t *s_rndis_eth_driver = NULL;
static esp_netif_t *s_rndis_netif = NULL;
static char s_rndis_dns[16] = "unknown";
static bool s_camera_task_started = false;
static bool s_telemetry_task_started = false;
static volatile bool s_rndis_has_ip = false;
static volatile bool s_cloud_uplink_ready = false;
static void start_camera_task(void);
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
static modem_wifi_config_t s_modem_wifi_config = MODEM_WIFI_DEFAULT_CONFIG();
static volatile bool s_ap_stream_requested = true;
static QueueHandle_t s_wifi_command_queue;
static TaskHandle_t s_wifi_state_publish_task_handle;
static void schedule_wifi_failover_policy(void);
#endif

#define L610_USB_VID 0x1782
#define L610_RNDIS_PID 0x4D11

#define EVENT_GOT_IP_BIT (BIT0)
#define EVENT_AT_READY_BIT (BIT1)
#define EVENT_WIFI_POLICY_BIT (BIT2)

static esp_err_t set_cloud_video_fps(int fps, const char *resolution, void *ctx)
{
    (void)ctx;
    return camera_stream_set_video_settings(fps, resolution);
}

static esp_err_t activate_cloud_uplink(esp_netif_t *netif, const char *label)
{
    if (netif == NULL) {
        s_cloud_uplink_ready = false;
        camera_stream_set_network_ready(false);
        telemetry_uart_set_network_ready(false);
        return ESP_ERR_INVALID_STATE;
    }

    s_cloud_uplink_ready = false;
    camera_stream_set_network_ready(false);
    telemetry_uart_set_network_ready(false);
    ESP_RETURN_ON_ERROR(esp_netif_set_default_netif(netif), TAG,
                        "Failed to select %s as default route", label);

    char uplink_ifname[6] = {0};
    if (esp_netif_get_netif_impl_name(netif, uplink_ifname) == ESP_OK) {
        camera_stream_set_uplink_interface(uplink_ifname);
        telemetry_uart_set_uplink_interface(uplink_ifname);
    } else {
        ESP_LOGW(TAG, "Could not resolve %s lwIP interface name; using default route",
                 label);
        camera_stream_set_uplink_interface(NULL);
        telemetry_uart_set_uplink_interface(NULL);
    }
    /* The telemetry task sends one heartbeat before it accepts any command.
     * Mark the selected uplink ready only after the route/interface is final. */
    s_cloud_uplink_ready = true;
    camera_stream_set_network_ready(true);
    telemetry_uart_set_network_ready(true);
    ESP_LOGI(TAG, "Cloud video and telemetry uplink -> %s", label);
    return ESP_OK;
}

#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
static esp_netif_t *current_cloud_netif(void)
{
    if (app_wifi_sta_uplink_is_active()) {
        return app_wifi_get_sta_netif();
    }
    return s_rndis_has_ip ? s_rndis_netif : NULL;
}

static esp_err_t wifi_uplink_changed(bool use_wifi, bool connected,
                                     esp_netif_t *sta_netif, void *ctx)
{
    (void)ctx;
    if (use_wifi && connected && sta_netif != NULL) {
        /* A deliberately selected Wi-Fi uplink must not wait for L610 DHCP.
         * The normal L610 profile still keeps its proven AP -> camera order,
         * while Wi-Fi-only operation can bring video online independently. */
        start_camera_task();
        esp_err_t ret = activate_cloud_uplink(sta_netif, "Wi-Fi STA");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to activate Wi-Fi cloud uplink");
        }
        return ret;
    }
    if (s_rndis_has_ip && s_rndis_netif != NULL) {
        esp_err_t ret = activate_cloud_uplink(s_rndis_netif, "L610 RNDIS");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restore L610 cloud uplink");
        }
        return ret;
    } else {
        s_cloud_uplink_ready = false;
        camera_stream_set_network_ready(false);
        telemetry_uart_set_network_ready(false);
        ESP_LOGW(TAG, "No cloud uplink is currently available");
        return ESP_ERR_INVALID_STATE;
    }
}

static esp_err_t set_ap_stream_enabled(bool enabled, void *ctx)
{
    (void)ctx;
    esp_netif_t *cloud_netif = current_cloud_netif();

    /* SoftAP is always a LAN. Preserve whichever upstream the user selected. */
    if (cloud_netif != NULL) {
        esp_err_t route_before = esp_netif_set_default_netif(cloud_netif);
        if (route_before != ESP_OK) {
            ESP_LOGE(TAG, "Cannot preserve cloud route before AP transition: %s",
                     esp_err_to_name(route_before));
            return route_before;
        }
    } else if (enabled) {
        ESP_LOGI(TAG, "Starting local configuration hotspot before a cloud uplink is ready");
    }

    esp_err_t operation;
    if (enabled) {
        operation = app_wifi_start_softap(&s_modem_wifi_config);
        if (operation == ESP_OK) {
            operation = camera_stream_set_local_mjpeg_enabled(true);
            if (operation != ESP_OK) {
                ESP_LOGE(TAG, "Local MJPEG start failed; rolling back SoftAP: %s",
                         esp_err_to_name(operation));
                app_wifi_stop_softap();
            }
        }
    } else {
        esp_err_t stream_stop = camera_stream_set_local_mjpeg_enabled(false);
        operation = app_wifi_stop_softap();
        if (operation == ESP_OK && stream_stop != ESP_OK) {
            operation = stream_stop;
        }
    }
    const bool actual_enabled = app_wifi_softap_is_started() &&
                                camera_stream_local_mjpeg_is_enabled();
    /* Keep the requested state separate from the observed state. If startup
     * fails transiently, a later DHCP recovery may retry the requested AP. */
    s_ap_stream_requested = enabled;
    if (operation == ESP_OK && actual_enabled != enabled) {
        operation = ESP_ERR_INVALID_STATE;
    }

    cloud_netif = current_cloud_netif();
    esp_err_t route_after = cloud_netif == NULL
        ? ESP_OK : esp_netif_set_default_netif(cloud_netif);
    if (route_after != ESP_OK) {
        ESP_LOGE(TAG, "Cannot restore cloud route after AP transition: %s",
                 esp_err_to_name(route_after));
    }
    schedule_wifi_failover_policy();
    return operation != ESP_OK ? operation : route_after;
}

static void copy_wifi_status(telemetry_wifi_state_t *target,
                             const app_wifi_status_t *source)
{
    target->feature_enabled = source->feature_enabled;
    target->scanning = source->scanning;
    target->connected = source->connected;
    snprintf(target->ssid, sizeof(target->ssid), "%s", source->ssid);
    snprintf(target->ip, sizeof(target->ip), "%s", source->ip);
    target->rssi_valid = source->rssi_valid;
    target->rssi = source->rssi;
    target->wifi_uplink_selected = source->wifi_uplink_selected;
    const char *uplink = source->active_uplink == APP_WIFI_UPLINK_WIFI ? "wifi"
        : (source->active_uplink == APP_WIFI_UPLINK_L610 ? "l610" : "none");
    snprintf(target->active_uplink, sizeof(target->active_uplink), "%s", uplink);
}

static void publish_initial_wifi_state(void)
{
    telemetry_wifi_state_t state = {
        .state = TELEMETRY_WIFI_IDLE,
        .action = TELEMETRY_WIFI_ACTION_QUERY,
    };
    snprintf(state.active_uplink, sizeof(state.active_uplink), "none");
    app_wifi_status_t status = {0};
    if (app_wifi_get_status(&status) == ESP_OK) {
        copy_wifi_status(&state, &status);
    }
    telemetry_uart_set_wifi_state(&state);
}

static void publish_current_wifi_observation(void)
{
    telemetry_wifi_state_t observation = {0};
    snprintf(observation.active_uplink,
             sizeof(observation.active_uplink), "none");
    app_wifi_status_t status = {0};
    if (app_wifi_get_status(&status) == ESP_OK) {
        copy_wifi_status(&observation, &status);
        telemetry_uart_update_wifi_observation(&observation);
    }
}

static void wifi_state_changed(void *ctx)
{
    (void)ctx;
    if (s_wifi_state_publish_task_handle != NULL) {
        xTaskNotifyGive(s_wifi_state_publish_task_handle);
    }
}

static void wifi_state_publish_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        publish_current_wifi_observation();
    }
}

static void schedule_wifi_failover_policy(void)
{
    if (s_event_group != NULL) {
        xEventGroupSetBits(s_event_group, EVENT_WIFI_POLICY_BIT);
    }
}

static void handle_l610_network_lost(void)
{
    s_rndis_has_ip = false;
    xEventGroupClearBits(s_event_group, EVENT_GOT_IP_BIT);
    schedule_wifi_failover_policy();
    if (app_wifi_sta_uplink_is_active()) {
        ESP_LOGI(TAG, "L610 network lost while Wi-Fi cloud uplink remains active");
        return;
    }
    app_wifi_refresh_uplink();
    camera_stream_set_network_ready(false);
    telemetry_uart_set_network_ready(false);
}

static esp_err_t queue_wifi_command(const telemetry_wifi_command_t *command,
                                    void *ctx)
{
    (void)ctx;
    if (s_wifi_command_queue == NULL || command == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_wifi_command_queue, command, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void wifi_control_task(void *arg)
{
    (void)arg;
    telemetry_wifi_state_t state = {
        .state = TELEMETRY_WIFI_IDLE,
        .action = TELEMETRY_WIFI_ACTION_QUERY,
    };
    snprintf(state.active_uplink, sizeof(state.active_uplink), "none");

    while (true) {
        telemetry_wifi_command_t command = {0};
        if (xQueueReceive(s_wifi_command_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        state.state = TELEMETRY_WIFI_APPLYING;
        state.action = command.action;
        snprintf(state.request_id, sizeof(state.request_id), "%s",
                 command.request_id);
        state.error[0] = '\0';
        state.scanning = command.action == TELEMETRY_WIFI_ACTION_SCAN;
        telemetry_uart_set_wifi_state(&state);

        esp_err_t ret = ESP_OK;
        app_wifi_network_t scan_results[TELEMETRY_WIFI_MAX_NETWORKS] = {0};
        size_t scan_count = 0;
        switch (command.action) {
        case TELEMETRY_WIFI_ACTION_QUERY:
            break;
        case TELEMETRY_WIFI_ACTION_SET_ENABLED:
            ret = app_wifi_set_sta_feature_enabled(command.enabled);
            if (ret == ESP_OK && !command.enabled) {
                state.network_count = 0;
            }
            break;
        case TELEMETRY_WIFI_ACTION_SCAN:
            ret = app_wifi_scan_networks(scan_results,
                                         TELEMETRY_WIFI_MAX_NETWORKS,
                                         &scan_count);
            if (ret == ESP_OK) {
                state.network_count = scan_count;
                for (size_t i = 0; i < scan_count; ++i) {
                    snprintf(state.networks[i].ssid,
                             sizeof(state.networks[i].ssid), "%s",
                             scan_results[i].ssid);
                    state.networks[i].rssi = scan_results[i].rssi;
                    state.networks[i].channel = scan_results[i].channel;
                    snprintf(state.networks[i].security,
                             sizeof(state.networks[i].security), "%s",
                             scan_results[i].security);
                    state.networks[i].secured = scan_results[i].secured;
                    state.networks[i].supported = scan_results[i].supported;
                }
            }
            break;
        case TELEMETRY_WIFI_ACTION_CONNECT:
            ret = app_wifi_connect_sta(command.ssid, command.password,
                                       command.security);
            if (ret == ESP_OK) {
                ret = ESP_ERR_TIMEOUT;
                for (int attempt = 0; attempt < 60; ++attempt) {
                    app_wifi_status_t status = {0};
                    if (app_wifi_get_status(&status) == ESP_OK) {
                        copy_wifi_status(&state, &status);
                        if (status.connected &&
                                strcmp(status.ssid, command.ssid) == 0) {
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
        case TELEMETRY_WIFI_ACTION_SELECT_UPLINK:
            ret = app_wifi_select_cloud_uplink(command.use_wifi);
            break;
        default:
            ret = ESP_ERR_INVALID_ARG;
            break;
        }

        app_wifi_status_t status = {0};
        esp_err_t status_ret = app_wifi_get_status(&status);
        if (status_ret == ESP_OK) {
            copy_wifi_status(&state, &status);
        } else if (ret == ESP_OK) {
            ret = status_ret;
        }
        state.scanning = false;
        state.state = ret == ESP_OK ? TELEMETRY_WIFI_SUCCESS
                                    : TELEMETRY_WIFI_ERROR;
        snprintf(state.error, sizeof(state.error), "%s",
                 ret == ESP_OK ? "" : esp_err_to_name(ret));
        telemetry_uart_set_wifi_state(&state);
        ESP_LOGI(TAG, "Wi-Fi management request=%s action=%d completed: %s",
                 command.request_id, (int)command.action,
                 ret == ESP_OK ? "success" : state.error);

        /* Credentials are never logged or copied into telemetry state. */
        memset(command.password, 0, sizeof(command.password));
    }
}
#endif

#if CONFIG_EXAMPLE_ENABLE_AT_CMD
typedef struct {
    usbh_cdc_port_handle_t cdc_port;      /*!< CDC port handle */
    at_handle_t at_handle;                /*!< AT command parser handle */
} at_ctx_t;

/* Keep the registration and RNDIS-control CDC ports independent. L610 mode 33
 * exposes both at once; reusing one context races with delayed close callbacks. */
static at_ctx_t s_l610_registration_ctx = {0};
at_ctx_t g_at_ctx = {0};
static volatile bool s_l610_at_configured = false;
static volatile bool s_l610_management_at_ready = false;
static volatile bool s_l610_sim_ready = false;
static volatile bool s_l610_network_registered = false;
static QueueHandle_t s_modem_command_queue;
static portMUX_TYPE s_modem_status_mux = portMUX_INITIALIZER_UNLOCKED;
static telemetry_modem_state_t s_cached_modem_state = {
    .state = TELEMETRY_MODEM_IDLE,
    .registration = -1,
    .rssi = -1,
};
static bool s_cached_modem_reboot_required;

#define L610_AT_RESPONSE_MAX 2048

typedef struct {
    bool ok;
    char response[L610_AT_RESPONSE_MAX];
} l610_at_response_t;

static esp_err_t l610_send_capture(at_handle_t at_handle, const char *command,
                                   uint32_t timeout_ms,
                                   l610_at_response_t *response);
static void l610_process_modem_command(const telemetry_modem_command_t *command);

static void publish_modem_state(const telemetry_modem_state_t *state,
                                bool reboot_required)
{
    portENTER_CRITICAL(&s_modem_status_mux);
    s_cached_modem_state = *state;
    s_cached_modem_reboot_required = reboot_required;
    portEXIT_CRITICAL(&s_modem_status_mux);
    telemetry_uart_set_modem_state(state);
}

static bool l610_rndis_connect_ready(void *ctx)
{
    return s_l610_at_configured;
}

static esp_err_t _at_send_cmd(const char *command, size_t length, void *usr_data)
{
    at_ctx_t *at_ctx = (at_ctx_t *)usr_data;
    return usbh_cdc_write_bytes(at_ctx->cdc_port, (const uint8_t *)command, length, pdMS_TO_TICKS(500));
}

static void _at_port_closed_cb(usbh_cdc_port_handle_t cdc_port_handle, void *arg)
{
    at_ctx_t *at_ctx = (at_ctx_t *)arg;
    ESP_LOGI(TAG, "AT port closed");
    at_ctx->cdc_port = NULL;
    if (at_ctx == &s_l610_registration_ctx) {
        s_l610_management_at_ready = false;
        s_l610_sim_ready = false;
        s_l610_network_registered = false;
    } else if (at_ctx == &g_at_ctx) {
        s_l610_at_configured = false;
    }

    if (at_ctx->at_handle) {
        modem_at_stop(at_ctx->at_handle);
        modem_at_parser_destroy(at_ctx->at_handle);
        at_ctx->at_handle = NULL;
    }
}

static void _at_recv_data_cb(usbh_cdc_port_handle_t cdc_port_handle, void *arg)
{
    at_ctx_t *at_ctx = (at_ctx_t *)arg;
    size_t length = 0;
    usbh_cdc_get_rx_buffer_size(cdc_port_handle, &length);
    if (at_ctx->at_handle == NULL) {
        ESP_LOGW(TAG, "Discarding %zu bytes from a closed AT interface", length);
        return;
    }
    char *buffer;
    size_t buffer_remain;
    modem_at_get_response_buffer(at_ctx->at_handle, &buffer, &buffer_remain);
    if (buffer_remain < length) {
        length = buffer_remain;
        ESP_LOGE(TAG, "data size is too big, truncated to %zu", length);
    }
    usbh_cdc_read_bytes(cdc_port_handle, (uint8_t *)buffer, &length, 0);
    // Parse the AT command response
    modem_at_write_response_done(at_ctx->at_handle, length);
}

static esp_err_t at_init(at_ctx_t *at_ctx, uint8_t interface_num)
{
    ESP_LOGI(TAG, "Opening L610 AT interface %u", interface_num);
    // Open a CDC port for AT command
    usbh_cdc_port_handle_t _port = usb_rndis_get_cdc_port_handle(s_rndis_eth_driver);
    ESP_RETURN_ON_FALSE(_port != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "RNDIS CDC device disappeared before opening IF%u",
                        interface_num);
    usb_device_handle_t _dev_hdl = NULL;
    ESP_RETURN_ON_ERROR(usbh_cdc_get_dev_handle(_port, &_dev_hdl), TAG,
                        "Could not resolve USB device for IF%u", interface_num);
    usb_device_info_t device_info;
    ESP_RETURN_ON_ERROR(usb_host_device_info(_dev_hdl, &device_info), TAG,
                        "Could not read USB device info for IF%u", interface_num);
    usbh_cdc_port_config_t cdc_port_config = {
        .dev_addr = device_info.dev_addr,
        .itf_num = interface_num,
        .in_transfer_buffer_size = L610_AT_RESPONSE_MAX,
        .out_transfer_buffer_size = 512,
        .cbs = {
            .notif_cb = NULL,
            .recv_data = _at_recv_data_cb,
            .closed = _at_port_closed_cb,
            .user_data = at_ctx,
        },
    };
    ESP_RETURN_ON_ERROR(usbh_cdc_port_open(&cdc_port_config, &at_ctx->cdc_port), TAG, "Failed to open AT CDC port");

    // init the AT command parser
    modem_at_config_t at_config = {
        .send_buffer_length = 256,
        .recv_buffer_length = L610_AT_RESPONSE_MAX,
        .io = {
            .send_cmd = _at_send_cmd,
            .usr_data = at_ctx,
        }
    };
    at_ctx->at_handle = modem_at_parser_create(&at_config);
    if (at_ctx->at_handle == NULL) {
        usbh_cdc_port_close(at_ctx->cdc_port);
        at_ctx->cdc_port = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = modem_at_start(at_ctx->at_handle);
    if (ret != ESP_OK) {
        modem_at_parser_destroy(at_ctx->at_handle);
        at_ctx->at_handle = NULL;
        usbh_cdc_port_close(at_ctx->cdc_port);
        at_ctx->cdc_port = NULL;
    }
    return ret;
}

static esp_err_t l610_wait_until_ready(at_handle_t at_handle)
{
    for (int retry = 0; retry < 30; retry++) {
        if (at_cmd_at(at_handle) == ESP_OK) {
            break;
        }
        if (retry == 29) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    s_l610_management_at_ready = true;

    at_cmd_set_echo(at_handle, false);
    at_send_command_response_ok(at_handle, "AT+CMEE=2");

    for (int retry = 0; retry < 60; retry++) {
        esp_modem_pin_state_t pin_state = PIN_UNKNOWN;
        if (at_cmd_read_pin(at_handle, &pin_state) == ESP_OK && pin_state == PIN_READY) {
            s_l610_sim_ready = true;
            ESP_LOGI(TAG, "L610 SIM is ready");
            break;
        }
        if (s_modem_command_queue != NULL) {
            telemetry_modem_command_t command = {0};
            if (xQueueReceive(s_modem_command_queue, &command, 0) == pdTRUE) {
                l610_process_modem_command(&command);
            }
        }
        if (retry == 59) {
            ESP_LOGE(TAG, "L610 SIM did not become ready");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    for (int retry = 0; retry < 90; retry++) {
        /* Local recovery must remain available while registration is failing.
         * The same manager task executes the queued operation, so AT commands
         * remain strictly serialized with normal L610 bring-up. */
        if (s_modem_command_queue != NULL) {
            telemetry_modem_command_t command = {0};
            if (xQueueReceive(s_modem_command_queue, &command, 0) == pdTRUE) {
                l610_process_modem_command(&command);
            }
        }
        esp_modem_at_cereg_t registration = {0};
        if (at_cmd_get_network_reg_status(at_handle, &registration) == ESP_OK &&
                (registration.stat == 1 || registration.stat == 5)) {
            ESP_LOGI(TAG, "L610 registered on LTE network (CEREG stat=%d)", registration.stat);
            return ESP_OK;
        }
        if ((retry % 10) == 0) {
            ESP_LOGI(TAG, "Waiting for L610 network registration...");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return ESP_ERR_TIMEOUT;
}

/* IF7 is the L610's working registration AT port on the ADP-L610 board.
 * IF2 is used separately below to request the RNDIS data session. */
static esp_err_t l610_prepare_network(at_handle_t at_handle)
{
    ESP_RETURN_ON_ERROR(l610_wait_until_ready(at_handle), TAG, "L610 did not become network-ready");
    ESP_RETURN_ON_ERROR(at_send_command_response_ok(at_handle, "AT+CGATT=1"), TAG, "L610 packet attach failed");

    esp_modem_at_pdp_t pdp = {
        .cid = 1,
        .type = "IP",
        .apn = CONFIG_L610_APN,
    };
    ESP_RETURN_ON_ERROR(at_cmd_set_pdp_context(at_handle, &pdp), TAG, "Failed to configure L610 APN");

    ESP_LOGI(TAG, "L610 network preparation completed on IF7 (APN=%s, CID=1)", CONFIG_L610_APN);
    return ESP_OK;
}

static esp_err_t l610_enable_rndis(at_handle_t at_handle, uint8_t interface_num)
{
    ESP_LOGI(TAG, "Requesting L610 RNDIS data session on IF%u", interface_num);

    /* An ESP-only reset does not necessarily reset the L610. Query the
     * persistent RNDIS state before sending a duplicate enable command. */
    l610_at_response_t status = {0};
    if (l610_send_capture(at_handle, "AT+GTRNDIS?", 3000, &status) == ESP_OK) {
        const char *line = strstr(status.response, "+GTRNDIS:");
        int state = -1;
        int cid = -1;
        char ip[40] = {0};
        if (line != NULL &&
                sscanf(line, "+GTRNDIS: %d,%d,%39[^,\r\n]", &state, &cid, ip) == 3 &&
                state == 1 && cid == 1 && ip[0] != '\0' &&
                strcmp(ip, "0.0.0.0") != 0) {
            ESP_LOGI(TAG,
                     "L610 retained an active RNDIS session on IF%u (IP=%s)",
                     interface_num, ip);
            return ESP_OK;
        }
    }

    for (int retry = 1; retry <= 3; retry++) {
        /* L610 firmware owns PDP activation for a RNDIS session. Do not issue
         * CGACT before GTRNDIS on the tested L610-CN firmware. */
        esp_err_t ret = at_send_command_response_ok(at_handle, "AT+GTRNDIS=1,1");
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "L610 RNDIS data session enabled automatically (APN=%s, CID=1)", CONFIG_L610_APN);
            return ESP_OK;
        }
        if (xEventGroupGetBits(s_event_group) & EVENT_GOT_IP_BIT) {
            ESP_LOGI(TAG, "L610 RNDIS already has an IP address");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "AT+GTRNDIS=1,1 attempt %d/3 failed", retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    /* Preserve the verified pre-V7 warm-reset recovery. The L610 can retain
     * its RNDIS/PDP session while only the ESP resets, then reject a duplicate
     * enable command. Let the USB host and DHCP verify that retained session. */
    if (usb_rndis_get_cdc_port_handle(s_rndis_eth_driver) != NULL) {
        ESP_LOGW(TAG,
                 "L610 RNDIS command was not acknowledged, but USB RNDIS is present; "
                 "continuing for warm-reset DHCP recovery");
        return ESP_OK;
    }
    return ESP_FAIL;
}

static bool l610_at_response_handler(at_handle_t at_handle, const char *line)
{
    l610_at_response_t *result = modem_at_get_handle_line_ctx(at_handle);
    if (result == NULL || line == NULL) {
        return true;
    }
    snprintf(result->response, sizeof(result->response), "%s", line);
    if (strstr(line, "\r\nOK\r\n") != NULL ||
            strcmp(line, "OK\r\n") == 0) {
        result->ok = true;
        return true;
    }
    if (strstr(line, "\r\nERROR\r\n") != NULL ||
            strstr(line, "+CME ERROR:") != NULL ||
            strcmp(line, "ERROR\r\n") == 0) {
        result->ok = false;
        return true;
    }
    return false;
}

static esp_err_t l610_send_capture(at_handle_t at_handle, const char *command,
                                   uint32_t timeout_ms,
                                   l610_at_response_t *response)
{
    if (at_handle == NULL || command == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(response, 0, sizeof(*response));
    esp_err_t ret = modem_at_send_command(at_handle, command, timeout_ms,
                                          l610_at_response_handler, response);
    if (ret != ESP_OK) {
        return ret;
    }
    return response->ok ? ESP_OK : ESP_FAIL;
}

static void copy_at_value(char *destination, size_t destination_size,
                          const char *response, const char *prefix)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }
    destination[0] = '\0';
    const char *start = response == NULL ? NULL : strstr(response, prefix);
    if (start == NULL) {
        return;
    }
    start += strlen(prefix);
    while (*start == ' ' || *start == '\t') {
        ++start;
    }
    const char *end = strpbrk(start, "\r\n");
    size_t length = end == NULL ? strlen(start) : (size_t)(end - start);
    if (length >= destination_size) {
        length = destination_size - 1;
    }
    memcpy(destination, start, length);
    destination[length] = '\0';
}

static void parse_operator_name(const char *response,
                                telemetry_modem_state_t *state)
{
    const char *line = response == NULL ? NULL : strstr(response, "+COPS:");
    if (line == NULL) {
        return;
    }
    const char *quoted = strchr(line, '"');
    if (quoted != NULL) {
        const char *end = strchr(quoted + 1, '"');
        if (end != NULL) {
            size_t length = (size_t)(end - quoted - 1);
            if (length >= sizeof(state->operator_name)) {
                length = sizeof(state->operator_name) - 1;
            }
            memcpy(state->operator_name, quoted + 1, length);
            state->operator_name[length] = '\0';
            return;
        }
    }
    copy_at_value(state->operator_name, sizeof(state->operator_name), line,
                  "+COPS:");
}

static char *trim_gtccinfo_token(char *token)
{
    while (*token == ' ' || *token == '\t') {
        ++token;
    }
    char *end = token + strlen(token);
    while (end > token && (end[-1] == ' ' || end[-1] == '\t' ||
            end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return token;
}

static bool parse_gtccinfo_long(const char *token, int base,
                                long minimum, long maximum, long *value)
{
    if (token == NULL || token[0] == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long parsed = strtol(token, &end, base);
    if (errno == ERANGE || end == token || *end != '\0' ||
            parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_gtccinfo_hex_u32(const char *token, uint32_t maximum,
                                   uint32_t *value)
{
    if (token == NULL || token[0] == '\0' || token[0] == '-') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(token, &end, 16);
    if (errno == ERANGE || end == token || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_lte_cell_line(const char *source, size_t source_length,
                                bool serving, telemetry_modem_cell_t *cell)
{
    if (source == NULL || cell == NULL || source_length == 0 ||
            source_length >= 256) {
        return false;
    }
    char line[256];
    memcpy(line, source, source_length);
    line[source_length] = '\0';
    char *tokens[16] = {0};
    size_t token_count = 1;
    tokens[0] = line;
    for (char *character = line; *character != '\0' &&
            token_count < sizeof(tokens) / sizeof(tokens[0]); ++character) {
        if (*character == ',') {
            *character = '\0';
            tokens[token_count++] = character + 1;
        }
    }
    const size_t required_tokens = serving ? 14 : 12;
    if (token_count < required_tokens) {
        return false;
    }
    for (size_t index = 0; index < token_count; ++index) {
        tokens[index] = trim_gtccinfo_token(tokens[index]);
    }

    *cell = (telemetry_modem_cell_t) {
        .serving = serving,
        .mcc = -1,
        .mnc = -1,
        .pci = -1,
        .band = -1,
        .rxlev = -1,
        .rsrp = -1,
        .rsrq = -1,
    };
    long marker = 0;
    long rat = 0;
    long mcc = 0;
    long mnc = 0;
    long band = -1;
    long bandwidth = 0;
    long rssnr = 0;
    long rxlev = 0;
    long rsrp = 0;
    long rsrq = 0;
    uint32_t pci = 0;
    const bool common_valid =
        parse_gtccinfo_long(tokens[0], 10, 0, INT_MAX, &marker) &&
        parse_gtccinfo_long(tokens[1], 10, 0, INT_MAX, &rat) &&
        parse_gtccinfo_long(tokens[2], 10, 0, 999, &mcc) &&
        parse_gtccinfo_long(tokens[3], 10, 0, 999, &mnc) &&
        tokens[4][0] != '\0' && tokens[5][0] != '\0' &&
        parse_gtccinfo_hex_u32(tokens[6], UINT32_MAX, &cell->earfcn) &&
        parse_gtccinfo_hex_u32(tokens[7], 503, &pci);
    bool metrics_valid = false;
    if (common_valid && serving) {
        metrics_valid =
            parse_gtccinfo_long(tokens[8], 10, 0, INT_MAX, &band) &&
            parse_gtccinfo_long(tokens[9], 10, 0, INT_MAX, &bandwidth) &&
            parse_gtccinfo_long(tokens[10], 10, INT_MIN, INT_MAX, &rssnr) &&
            parse_gtccinfo_long(tokens[11], 10, INT_MIN, INT_MAX, &rxlev) &&
            parse_gtccinfo_long(tokens[12], 10, INT_MIN, INT_MAX, &rsrp) &&
            parse_gtccinfo_long(tokens[13], 10, INT_MIN, INT_MAX, &rsrq);
    } else if (common_valid) {
        metrics_valid =
            parse_gtccinfo_long(tokens[8], 10, 0, INT_MAX, &bandwidth) &&
            parse_gtccinfo_long(tokens[9], 10, INT_MIN, INT_MAX, &rxlev) &&
            parse_gtccinfo_long(tokens[10], 10, INT_MIN, INT_MAX, &rsrp) &&
            parse_gtccinfo_long(tokens[11], 10, INT_MIN, INT_MAX, &rsrq);
    }
    if (!common_valid || !metrics_valid) {
        return false;
    }
    (void)marker;
    (void)rat;
    (void)bandwidth;
    (void)rssnr;
    cell->mcc = (int)mcc;
    cell->mnc = (int)mnc;
    cell->pci = (int)pci;
    cell->band = serving ? (int)band : -1;
    cell->rxlev = (int)rxlev;
    cell->rsrp = (int)rsrp;
    cell->rsrq = (int)rsrq;
    snprintf(cell->tac, sizeof(cell->tac), "%s", tokens[4]);
    snprintf(cell->cell_id, sizeof(cell->cell_id), "%s", tokens[5]);
    return true;
}

static const char *next_gtccinfo_line(const char *line_end)
{
    if (line_end == NULL || *line_end == '\0') {
        return NULL;
    }
    if (*line_end == '\r' && line_end[1] == '\n') {
        return line_end + 2;
    }
    return line_end + 1;
}

static size_t add_lte_cells(const char *response,
                            telemetry_modem_state_t *state, bool serving)
{
    const char *label = serving ? "LTE service cell:" : "LTE neighbor cell:";
    const char *cursor = response == NULL ? NULL : strstr(response, label);
    if (cursor == NULL) {
        return 0;
    }
    cursor += strlen(label);
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    /* L610 emits the section label and its first row on separate lines. */
    if (*cursor == '\r' || *cursor == '\n') {
        cursor = next_gtccinfo_line(cursor);
    }

    size_t added = 0;
    while (cursor != NULL && *cursor != '\0' &&
            state->cell_count < TELEMETRY_MODEM_MAX_CELLS) {
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\r' || *cursor == '\n') {
            break; /* Empty line terminates this section. */
        }
        const char *line_end = strpbrk(cursor, "\r\n");
        size_t line_length = line_end == NULL
            ? strlen(cursor) : (size_t)(line_end - cursor);
        while (line_length > 0 &&
                (cursor[line_length - 1] == ' ' || cursor[line_length - 1] == '\t')) {
            --line_length;
        }
        if (line_length == 0 ||
                (line_length >= 2 && strncmp(cursor, "OK", 2) == 0) ||
                (line_length >= 5 && strncmp(cursor, "ERROR", 5) == 0) ||
                (line_length >= 4 && strncmp(cursor, "LTE ", 4) == 0)) {
            break;
        }

        telemetry_modem_cell_t cell;
        if (parse_lte_cell_line(cursor, line_length, serving, &cell)) {
            state->cells[state->cell_count++] = cell;
            ++added;
        }
        if (serving || line_end == NULL) {
            break;
        }
        cursor = next_gtccinfo_line(line_end);
    }
    return added;
}

static bool l610_wait_registered(at_handle_t at_handle, uint32_t timeout_seconds,
                                 int *registration)
{
    const uint32_t attempts = timeout_seconds / 2 + 1;
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        esp_modem_at_cereg_t result = {0};
        if (at_cmd_get_network_reg_status(at_handle, &result) == ESP_OK) {
            if (registration != NULL) {
                *registration = result.stat;
            }
            if (result.stat == 1 || result.stat == 5) {
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return false;
}

static bool l610_query_modem(at_handle_t at_handle,
                             telemetry_modem_state_t *state,
                             l610_at_response_t *response)
{
    if (response == NULL) {
        return false;
    }
    bool operator_ok = false;
    bool band_ok = false;

    if (l610_send_capture(at_handle, "AT+COPS?", 5000, response) == ESP_OK) {
        parse_operator_name(response->response, state);
        operator_ok = true;
    }

    esp_modem_at_cereg_t registration = {0};
    if (at_cmd_get_network_reg_status(at_handle, &registration) == ESP_OK) {
        state->registration = registration.stat;
    }
    esp_modem_at_csq_t signal = {0};
    if (at_cmd_get_signal_quality(at_handle, &signal) == ESP_OK) {
        state->rssi = signal.rssi;
    }

    if (l610_send_capture(at_handle, "AT+GTACT?", 5000, response) == ESP_OK) {
        copy_at_value(state->band_config, sizeof(state->band_config),
                      response->response, "+GTACT:");
        const int rat = atoi(state->band_config);
        snprintf(state->rat, sizeof(state->rat), "%s",
                 rat == 2 ? "LTE" : (rat == 5 ? "LTE/GSM" :
                 (rat == 10 ? "AUTO" : "GSM")));
        band_ok = true;
    }
    if (l610_send_capture(at_handle, "AT+GTCELLLOCK?", 5000, response) == ESP_OK) {
        copy_at_value(state->cell_lock_config,
                      sizeof(state->cell_lock_config), response->response,
                      "+GTCELLLOCK:");
    }
    state->cell_count = 0;
    bool serving_cell_ok = false;
    if (l610_send_capture(at_handle, "AT+GTCCINFO?", 30000, response) == ESP_OK) {
        const size_t serving_cells = add_lte_cells(response->response, state, true);
        add_lte_cells(response->response, state, false);
        serving_cell_ok = serving_cells > 0;
        if (!serving_cell_ok &&
                (state->registration == 1 || state->registration == 5)) {
            snprintf(state->error, sizeof(state->error),
                     "SERVING_CELL_PARSE_FAILED");
        }
    } else if (state->registration == 1 || state->registration == 5) {
        snprintf(state->error, sizeof(state->error), "GTCCINFO_FAILED");
    }
    const bool registered = state->registration == 1 || state->registration == 5;
    if (!registered && band_ok) {
        /* Registration recovery still needs the current band/lock state even
         * when COPS/GTCCINFO have no serving cell to report. */
        state->error[0] = '\0';
    }
    return band_ok && (!registered || (operator_ok && serving_cell_ok));
}

static bool band_is_selected(const telemetry_modem_command_t *command,
                             int band)
{
    for (size_t i = 0; i < command->band_count; ++i) {
        if (command->bands[i] == band) {
            return true;
        }
    }
    return false;
}

static esp_err_t l610_apply_modem_command(
    at_handle_t at_handle, const telemetry_modem_command_t *command,
    telemetry_modem_state_t *result)
{
    l610_at_response_t response = {0};
    esp_err_t ret = ESP_OK;

    if (command->action == TELEMETRY_MODEM_ACTION_RESELECT) {
        ret = l610_send_capture(at_handle, "AT+COPS=0", 90000, &response);
    } else if (command->action == TELEMETRY_MODEM_ACTION_SET_BANDS) {
        char at_command[160] = "AT+GTACT=2,,";
        size_t used = strlen(at_command);
        for (size_t i = 0; i < command->band_count; ++i) {
            int written = snprintf(at_command + used, sizeof(at_command) - used,
                                   ",%u", 100U + command->bands[i]);
            if (written <= 0 || (size_t)written >= sizeof(at_command) - used) {
                return ESP_ERR_INVALID_SIZE;
            }
            used += (size_t)written;
        }
        ret = l610_send_capture(at_handle, at_command, 10000, &response);
        if (ret == ESP_OK && command->reselect_after_bands) {
            ret = l610_send_capture(at_handle, "AT+COPS=0", 90000, &response);
        }
    } else if (command->action == TELEMETRY_MODEM_ACTION_SET_CELL_LOCK) {
        char at_command[96];
        if (command->lock_pci) {
            snprintf(at_command, sizeof(at_command),
                     "AT+GTCELLLOCK=1,0,0,%" PRIu32 ",%d",
                     command->earfcn, command->pci);
        } else {
            snprintf(at_command, sizeof(at_command),
                     "AT+GTCELLLOCK=1,0,1,%" PRIu32, command->earfcn);
        }
        ret = l610_send_capture(at_handle, at_command, 10000, &response);
        if (ret == ESP_OK) {
            ret = l610_send_capture(at_handle, "AT+COPS=0", 90000, &response);
        }
    } else if (command->action == TELEMETRY_MODEM_ACTION_CLEAR_CELL_LOCK) {
        ret = l610_send_capture(at_handle, "AT+GTCELLLOCK=0", 10000, &response);
    }

    if (ret != ESP_OK) {
        return ret;
    }
    if (command->action == TELEMETRY_MODEM_ACTION_SET_BANDS &&
            !command->reselect_after_bands) {
        /* GTACT is persistent on the tested L610 firmware. Its OK response is
         * the completed operation; waiting for the old registration session
         * causes a correct setting to be reported as a timeout. */
        snprintf(result->band_config, sizeof(result->band_config),
                 "saved (%u band%s)", (unsigned)command->band_count,
                 command->band_count == 1 ? "" : "s");
        result->error[0] = '\0';
        return ESP_OK;
    }
    if (command->action == TELEMETRY_MODEM_ACTION_CLEAR_CELL_LOCK) {
        /* Clearing the persistent lock is itself the recovery operation. Do
         * not turn its confirmed OK into a failure merely because automatic
         * network selection has not completed yet; reselect is a separate
         * explicit action. */
        snprintf(result->cell_lock_config,
                 sizeof(result->cell_lock_config), "0");
        result->error[0] = '\0';
        return ESP_OK;
    }
    if (command->action != TELEMETRY_MODEM_ACTION_QUERY &&
            !l610_wait_registered(at_handle, 45, &result->registration)) {
        return ESP_ERR_TIMEOUT;
    }
    if (!l610_query_modem(at_handle, result, &response)) {
        return ESP_FAIL;
    }

    if (command->action == TELEMETRY_MODEM_ACTION_SET_CELL_LOCK) {
        const telemetry_modem_cell_t *serving = NULL;
        for (size_t i = 0; i < result->cell_count; ++i) {
            if (result->cells[i].serving) {
                serving = &result->cells[i];
                break;
            }
        }
        if (serving == NULL || serving->earfcn != command->earfcn ||
                (command->lock_pci && serving->pci != command->pci)) {
            snprintf(result->error, sizeof(result->error), "CELL_NOT_SELECTED");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (command->action == TELEMETRY_MODEM_ACTION_SET_BANDS) {
        for (size_t i = 0; i < result->cell_count; ++i) {
            if (result->cells[i].serving && result->cells[i].band > 0 &&
                    !band_is_selected(command, result->cells[i].band)) {
                snprintf(result->error, sizeof(result->error), "BAND_NOT_SELECTED");
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    return ESP_OK;
}

static void l610_process_modem_command(const telemetry_modem_command_t *command)
{
    telemetry_modem_state_t result = {
        .state = command->action == TELEMETRY_MODEM_ACTION_QUERY
            ? TELEMETRY_MODEM_QUERYING : TELEMETRY_MODEM_APPLYING,
        .action = command->action,
        .registration = -1,
        .rssi = -1,
    };
    snprintf(result.request_id, sizeof(result.request_id), "%s",
             command->request_id);
    publish_modem_state(&result, false);

    esp_err_t ret = l610_apply_modem_command(
        s_l610_registration_ctx.at_handle, command, &result);
    result.state = ret == ESP_OK ? TELEMETRY_MODEM_SUCCESS
                                 : TELEMETRY_MODEM_ERROR;
    if (ret != ESP_OK && result.error[0] == '\0') {
        snprintf(result.error, sizeof(result.error), "%s", esp_err_to_name(ret));
    }
    const bool reboot_required = ret == ESP_OK &&
        command->action == TELEMETRY_MODEM_ACTION_SET_BANDS &&
        !command->reselect_after_bands;
    publish_modem_state(&result, reboot_required);
    ESP_LOGI(TAG, "4G management request=%s completed: %s",
             command->request_id, ret == ESP_OK ? "success" : result.error);
}

static esp_err_t queue_modem_command(const telemetry_modem_command_t *command,
                                     void *ctx)
{
    (void)ctx;
    if (s_modem_command_queue == NULL || command == NULL ||
            !s_l610_management_at_ready ||
            s_l610_registration_ctx.at_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    telemetry_modem_state_t queued_state = {
        .state = command->action == TELEMETRY_MODEM_ACTION_QUERY
            ? TELEMETRY_MODEM_QUERYING : TELEMETRY_MODEM_APPLYING,
        .action = command->action,
        .registration = -1,
        .rssi = -1,
    };
    snprintf(queued_state.request_id, sizeof(queued_state.request_id), "%s",
             command->request_id);
    publish_modem_state(&queued_state, false);
    if (xQueueSend(s_modem_command_queue, command, 0) == pdTRUE) {
        return ESP_OK;
    }
    queued_state.state = TELEMETRY_MODEM_ERROR;
    snprintf(queued_state.error, sizeof(queued_state.error), "QUEUE_BUSY");
    publish_modem_state(&queued_state, false);
    return ESP_ERR_TIMEOUT;
}

static void l610_at_manager_task(void *arg)
{
    const uint8_t registration_interface = CONFIG_EXAMPLE_AT_INTERFACE_NUM;
    const uint8_t rndis_control_interface = 2;

    while (true) {
        usbh_cdc_port_handle_t rndis_port = usb_rndis_get_cdc_port_handle(s_rndis_eth_driver);
        if (rndis_port == NULL) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!s_l610_network_registered) {
            if (s_l610_registration_ctx.at_handle == NULL &&
                    at_init(&s_l610_registration_ctx, registration_interface) != ESP_OK) {
                ESP_LOGW(TAG, "L610 registration interface %u unavailable", registration_interface);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            if (s_l610_registration_ctx.at_handle != NULL) {
                if (l610_prepare_network(s_l610_registration_ctx.at_handle) == ESP_OK) {
                    s_l610_network_registered = true;
                    ESP_LOGI(TAG, "Keeping IF%u for registration and opening IF%u for RNDIS control",
                             registration_interface, rndis_control_interface);
                    continue;
                }
                ESP_LOGE(TAG, "L610 network preparation failed on IF%u; retrying", registration_interface);
            }
        }

        if (!s_l610_at_configured && g_at_ctx.at_handle == NULL &&
                at_init(&g_at_ctx, rndis_control_interface) != ESP_OK) {
            ESP_LOGW(TAG, "L610 RNDIS control interface %u unavailable", rndis_control_interface);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (g_at_ctx.at_handle != NULL && !s_l610_at_configured) {
            if (l610_enable_rndis(g_at_ctx.at_handle, rndis_control_interface) == ESP_OK) {
                s_l610_at_configured = true;
                xEventGroupSetBits(s_event_group, EVENT_AT_READY_BIT);
            } else {
                ESP_LOGW(TAG, "L610 RNDIS setup failed on IF%u; falling back to IF%u",
                         rndis_control_interface, registration_interface);
                if (s_l610_registration_ctx.at_handle != NULL &&
                        l610_enable_rndis(s_l610_registration_ctx.at_handle, registration_interface) == ESP_OK) {
                    ESP_LOGI(TAG, "L610 RNDIS data session enabled via IF%u fallback", registration_interface);
                    s_l610_at_configured = true;
                    xEventGroupSetBits(s_event_group, EVENT_AT_READY_BIT);
                } else {
                    ESP_LOGE(TAG, "L610 RNDIS setup failed on IF%u and IF%u; retrying",
                             rndis_control_interface, registration_interface);
                    if (g_at_ctx.cdc_port != NULL) {
                        usbh_cdc_port_close(g_at_ctx.cdc_port);
                    }
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    continue;
                }
            }
        }

        if (s_l610_management_at_ready &&
                s_l610_registration_ctx.at_handle != NULL &&
                s_modem_command_queue != NULL) {
            telemetry_modem_command_t command = {0};
            if (xQueueReceive(s_modem_command_queue, &command, 0) == pdTRUE) {
                l610_process_modem_command(&command);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
static app_wifi_portal_operation_state_t portal_operation_state(
    telemetry_modem_state_code_t state)
{
    switch (state) {
    case TELEMETRY_MODEM_QUERYING:
    case TELEMETRY_MODEM_APPLYING:
        return APP_WIFI_PORTAL_OPERATION_APPLYING;
    case TELEMETRY_MODEM_SUCCESS:
        return APP_WIFI_PORTAL_OPERATION_SUCCESS;
    case TELEMETRY_MODEM_ERROR:
        return APP_WIFI_PORTAL_OPERATION_ERROR;
    default:
        return APP_WIFI_PORTAL_OPERATION_IDLE;
    }
}

static app_wifi_portal_modem_action_t portal_modem_action(
    telemetry_modem_action_t action)
{
    switch (action) {
    case TELEMETRY_MODEM_ACTION_RESELECT:
        return APP_WIFI_PORTAL_MODEM_RESELECT;
    case TELEMETRY_MODEM_ACTION_SET_BANDS:
        return APP_WIFI_PORTAL_MODEM_SET_BANDS;
    case TELEMETRY_MODEM_ACTION_SET_CELL_LOCK:
        return APP_WIFI_PORTAL_MODEM_SET_CELL_LOCK;
    case TELEMETRY_MODEM_ACTION_CLEAR_CELL_LOCK:
        return APP_WIFI_PORTAL_MODEM_CLEAR_CELL_LOCK;
    default:
        return APP_WIFI_PORTAL_MODEM_QUERY;
    }
}

static esp_err_t portal_get_device_status(
    app_wifi_portal_device_status_t *status, void *ctx)
{
    (void)ctx;
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Portal status output is required");
    memset(status, 0, sizeof(*status));
    snprintf(status->device_id, sizeof(status->device_id), "%s",
             CONFIG_TELEMETRY_DEVICE_ID);

#if CONFIG_EXAMPLE_ENABLE_AT_CMD
    telemetry_modem_state_t modem = {0};
    bool reboot_required = false;
    portENTER_CRITICAL(&s_modem_status_mux);
    modem = s_cached_modem_state;
    reboot_required = s_cached_modem_reboot_required;
    portEXIT_CRITICAL(&s_modem_status_mux);

    status->modem.supported = true;
    status->modem.at_ready = s_l610_management_at_ready;
    status->modem.sim_ready = s_l610_sim_ready;
    status->modem.rndis_ready = s_rndis_has_ip;
    status->modem.state = portal_operation_state(modem.state);
    status->modem.action = portal_modem_action(modem.action);
    status->modem.reboot_required = reboot_required;
    status->modem.registration = modem.registration;
    status->modem.rssi = modem.rssi;
    snprintf(status->modem.request_id, sizeof(status->modem.request_id),
             "%s", modem.request_id);
    snprintf(status->modem.error, sizeof(status->modem.error), "%s",
             modem.error);
    snprintf(status->modem.operator_name,
             sizeof(status->modem.operator_name), "%s",
             modem.operator_name);
    snprintf(status->modem.rat, sizeof(status->modem.rat), "%s", modem.rat);
    snprintf(status->modem.band_config, sizeof(status->modem.band_config),
             "%s", modem.band_config);
    snprintf(status->modem.cell_lock_config,
             sizeof(status->modem.cell_lock_config), "%s",
             modem.cell_lock_config);
    status->modem.cell_count = modem.cell_count < APP_WIFI_PORTAL_MODEM_MAX_CELLS
        ? modem.cell_count : APP_WIFI_PORTAL_MODEM_MAX_CELLS;
    for (size_t index = 0; index < status->modem.cell_count; ++index) {
        const telemetry_modem_cell_t *source = &modem.cells[index];
        app_wifi_portal_modem_cell_t *target = &status->modem.cells[index];
        target->serving = source->serving;
        target->mcc = source->mcc;
        target->mnc = source->mnc;
        target->earfcn = source->earfcn;
        target->pci = source->pci;
        target->band = source->band;
        target->rxlev = source->rxlev;
        target->rsrp = source->rsrp;
        target->rsrq = source->rsrq;
        snprintf(target->tac, sizeof(target->tac), "%s", source->tac);
        snprintf(target->cell_id, sizeof(target->cell_id), "%s",
                 source->cell_id);
    }
#else
    status->modem.supported = false;
    status->modem.registration = -1;
    status->modem.rssi = -1;
#endif

#if CONFIG_VIDEO_CAMERA_ENABLE
    status->video.supported = true;
#endif
    status->video.camera_ready = camera_stream_is_initialized();
    status->video.pending = camera_stream_video_settings_pending();
    status->video.local_mjpeg_enabled =
        camera_stream_local_mjpeg_is_enabled();
    status->video.fps = camera_stream_get_target_fps();
    status->video.width = camera_stream_get_width();
    status->video.height = camera_stream_get_height();
    snprintf(status->video.resolution, sizeof(status->video.resolution), "%s",
             camera_stream_get_resolution());
    return ESP_OK;
}

static esp_err_t portal_submit_modem_command(
    const app_wifi_portal_modem_command_t *portal_command, void *ctx)
{
    (void)ctx;
#if CONFIG_EXAMPLE_ENABLE_AT_CMD
    ESP_RETURN_ON_FALSE(portal_command != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Portal modem command is required");
    telemetry_modem_command_t command = {0};
    switch (portal_command->action) {
    case APP_WIFI_PORTAL_MODEM_QUERY:
        command.action = TELEMETRY_MODEM_ACTION_QUERY;
        break;
    case APP_WIFI_PORTAL_MODEM_RESELECT:
        command.action = TELEMETRY_MODEM_ACTION_RESELECT;
        break;
    case APP_WIFI_PORTAL_MODEM_SET_BANDS:
        command.action = TELEMETRY_MODEM_ACTION_SET_BANDS;
        break;
    case APP_WIFI_PORTAL_MODEM_SET_CELL_LOCK:
        command.action = TELEMETRY_MODEM_ACTION_SET_CELL_LOCK;
        break;
    case APP_WIFI_PORTAL_MODEM_CLEAR_CELL_LOCK:
        command.action = TELEMETRY_MODEM_ACTION_CLEAR_CELL_LOCK;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(command.request_id, sizeof(command.request_id), "%s",
             portal_command->request_id);
    command.band_count = portal_command->band_count;
    if (command.band_count > TELEMETRY_MODEM_MAX_BANDS) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(command.bands, portal_command->bands,
           command.band_count * sizeof(command.bands[0]));
    command.reselect_after_bands = portal_command->reselect_after_bands;
    command.earfcn = portal_command->earfcn;
    command.pci = portal_command->pci;
    command.lock_pci = portal_command->lock_pci;
    return queue_modem_command(&command, NULL);
#else
    (void)portal_command;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t portal_apply_video_settings(
    const app_wifi_portal_video_settings_t *settings, void *ctx)
{
    (void)ctx;
    ESP_RETURN_ON_FALSE(settings != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Portal video settings are required");
    const char *resolution = settings->resolution[0] == '\0'
        ? NULL : settings->resolution;
    esp_err_t ret = camera_stream_set_video_settings(settings->fps, resolution);
    telemetry_uart_set_video_settings_state(
        settings->request_id, settings->fps, resolution, ret);
    return ret;
}

static esp_err_t portal_set_enabled(bool enabled,
                                    app_wifi_portal_close_reason_t reason,
                                    void *ctx)
{
    (void)ctx;
    const char *reason_name = reason == APP_WIFI_PORTAL_CLOSE_TIMEOUT ? "timeout"
        : (reason == APP_WIFI_PORTAL_CLOSE_USER ? "user" : "cloud");
    ESP_LOGI(TAG, "Local configuration hotspot %s requested (%s)",
             enabled ? "enable" : "disable", reason_name);
    telemetry_uart_set_ap_stream_state(
        enabled ? TELEMETRY_AP_STREAM_STARTING : TELEMETRY_AP_STREAM_STOPPING,
        ESP_OK);
    esp_err_t ret = set_ap_stream_enabled(enabled, NULL);
    telemetry_uart_set_ap_stream_state(
        ret != ESP_OK ? TELEMETRY_AP_STREAM_ERROR
                      : (enabled ? TELEMETRY_AP_STREAM_ENABLED
                                 : TELEMETRY_AP_STREAM_DISABLED),
        ret);
    return ret;
}
#endif

static void iot_event_handle(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IOT_ETH_EVENT) {
        switch (event_id) {
        case IOT_ETH_EVENT_START:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_START");
            break;
        case IOT_ETH_EVENT_STOP:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_STOP");
            break;
        case IOT_ETH_EVENT_CONNECTED:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_CONNECTED");
            break;
        case IOT_ETH_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_DISCONNECTED");
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
            handle_l610_network_lost();
#else
            s_rndis_has_ip = false;
            xEventGroupClearBits(s_event_group, EVENT_GOT_IP_BIT);
            camera_stream_set_network_ready(false);
            telemetry_uart_set_network_ready(false);
#endif
            break;
        default:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_UNKNOWN");
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_LOST_IP) {
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
        ESP_LOGW(TAG, "L610 RNDIS lost its IPv4 address");
        handle_l610_network_lost();
#else
        s_rndis_has_ip = false;
        xEventGroupClearBits(s_event_group, EVENT_GOT_IP_BIT);
        camera_stream_set_network_ready(false);
        telemetry_uart_set_network_ready(false);
#endif
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_dns_info_t dns = {0};

        ESP_LOGI(TAG, "L610 RNDIS got IPv4: " IPSTR ", netmask: " IPSTR ", gateway: " IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
        if (esp_netif_get_dns_info(s_rndis_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
                dns.ip.type == ESP_IPADDR_TYPE_V4 && dns.ip.u_addr.ip4.addr != 0) {
            snprintf(s_rndis_dns, sizeof(s_rndis_dns), IPSTR,
                     IP2STR(&dns.ip.u_addr.ip4));
            ESP_LOGI(TAG, "L610 RNDIS DNS: %s", s_rndis_dns);
        } else {
            snprintf(s_rndis_dns, sizeof(s_rndis_dns), "223.5.5.5");
            ESP_LOGW(TAG, "No upstream DNS supplied; fallback is %s", s_rndis_dns);
        }
        s_rndis_has_ip = true;
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
        schedule_wifi_failover_policy();
        if (!app_wifi_sta_uplink_is_active()) {
            /* Match the proven pre-V7 startup order. Select the RNDIS route
             * now, but do not publish telemetry/camera readiness until
             * app_main has completed AP -> camera startup. */
            esp_err_t route_ret = esp_netif_set_default_netif(s_rndis_netif);
            if (route_ret != ESP_OK) {
                ESP_LOGE(TAG, "L610 DHCP default route selection failed: %s",
                         esp_err_to_name(route_ret));
                return;
            }
            char uplink_ifname[6] = {0};
            if (esp_netif_get_netif_impl_name(s_rndis_netif, uplink_ifname) == ESP_OK) {
                camera_stream_set_uplink_interface(uplink_ifname);
                telemetry_uart_set_uplink_interface(uplink_ifname);
            } else {
                ESP_LOGW(TAG, "Could not resolve RNDIS lwIP interface name; using default route");
                camera_stream_set_uplink_interface(NULL);
                telemetry_uart_set_uplink_interface(NULL);
            }
        } else {
            ESP_LOGI(TAG, "L610 DHCP refreshed; keeping selected Wi-Fi cloud uplink");
        }
#else
        ESP_ERROR_CHECK(activate_cloud_uplink(s_rndis_netif, "L610 RNDIS"));
#endif
        xEventGroupSetBits(s_event_group, EVENT_GOT_IP_BIT);
    }
}

static void install_rndis(uint16_t idVendor, uint16_t idProduct, const char *netif_name)
{
    esp_err_t ret = ESP_OK;
    iot_eth_handle_t eth_handle = NULL;
    iot_eth_netif_glue_handle_t glue = NULL;

    usb_device_match_id_t *dev_match_id = calloc(2, sizeof(usb_device_match_id_t));
    if (dev_match_id == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for device match ID");
        return;
    }
    dev_match_id[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
    dev_match_id[0].idVendor = idVendor;
    dev_match_id[0].idProduct = idProduct;
    memset(&dev_match_id[1], 0, sizeof(usb_device_match_id_t)); // end of list
    iot_usbh_rndis_config_t rndis_cfg = {
        .match_id_list = dev_match_id,
#if CONFIG_EXAMPLE_ENABLE_AT_CMD
        .connect_ready_cb = l610_rndis_connect_ready,
        .connect_ready_ctx = NULL,
#endif
    };

    ret = iot_eth_new_usb_rndis(&rndis_cfg, &s_rndis_eth_driver);
    if (ret != ESP_OK || s_rndis_eth_driver == NULL) {
        ESP_LOGE(TAG, "Failed to create USB RNDIS driver");
        free(dev_match_id);
        return;
    }
    // Note: dev_match_id is now managed by the driver, don't free it here

    iot_eth_config_t eth_cfg = {
        .driver = s_rndis_eth_driver,
        .stack_input = NULL,
    };
    ret = iot_eth_install(&eth_cfg, &eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB RNDIS driver");
        return;
    }

    esp_netif_inherent_config_t _inherent_eth_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    _inherent_eth_config.if_key = netif_name;
    _inherent_eth_config.if_desc = netif_name;
    esp_netif_config_t netif_cfg = {
        .base = &_inherent_eth_config,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_rndis_netif = esp_netif_new(&netif_cfg);
    if (s_rndis_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create network interface");
        return;
    }

    glue = iot_eth_new_netif_glue(eth_handle);
    if (glue == NULL) {
        ESP_LOGE(TAG, "Failed to create netif glue");
        return;
    }
    esp_netif_attach(s_rndis_netif, glue);
    iot_eth_start(eth_handle);
}

static void start_camera_task(void)
{
    if (s_camera_task_started) {
        return;
    }
#if CONFIG_VIDEO_CAMERA_ENABLE
    if (camera_stream_start() != ESP_OK) {
        ESP_LOGE(TAG, "Camera UDP uploader could not be started");
    }
#else
    ESP_LOGW(TAG, "OV5640 capture is disabled in this diagnostic build; no XCLK or video task");
#endif
    s_camera_task_started = true;
}

static void start_telemetry_task(void)
{
    if (s_telemetry_task_started) {
        return;
    }
    if (telemetry_uart_start() != ESP_OK) {
        ESP_LOGE(TAG, "Unified USART telemetry could not be started");
    }
    s_telemetry_task_started = true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 + OV5640 + L610 RNDIS UDP video uplink");
    ESP_LOGI(TAG, "Expected USB device: %04X:%04X (L610 USB mode 33)", L610_USB_VID, L610_RNDIS_PID);
    ESP_LOGI(TAG, "Power path: ESP32-S3 Host VBUS -> USB-C cable -> ADP-L610; do not attach a second 5 V source");
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
    ESP_LOGI(TAG, "Local configuration hotspot enabled for 10 minutes; administrator extension is available");
#else
    ESP_LOGI(TAG, "Wi-Fi SoftAP runtime control is unsupported in this profile");
#endif

    /* Initialize NVS for Wi-Fi storage */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated and needs to be erased
         * Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Initialize default TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_event_group = xEventGroupCreate();
    ESP_RETURN_VOID_ON_FALSE(s_event_group != NULL, TAG, "Failed to create event group");
    esp_event_handler_register(IOT_ETH_EVENT, ESP_EVENT_ANY_ID, iot_event_handle, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, iot_event_handle, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, iot_event_handle, NULL);

#if CONFIG_EXAMPLE_ENABLE_AT_CMD
    s_modem_command_queue = xQueueCreate(3, sizeof(telemetry_modem_command_t));
    ESP_RETURN_VOID_ON_FALSE(s_modem_command_queue != NULL, TAG,
                             "Failed to create L610 management queue");
    telemetry_uart_configure_modem(true, queue_modem_command, NULL);
#else
    telemetry_uart_configure_modem(false, NULL, NULL);
#endif
    telemetry_uart_configure_video_fps(
        true, camera_stream_get_target_fps(), camera_stream_get_resolution(),
        set_cloud_video_fps, NULL);

#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
    app_wifi_set_uplink_callback(wifi_uplink_changed, NULL);
    s_wifi_command_queue = xQueueCreate(3, sizeof(telemetry_wifi_command_t));
    ESP_RETURN_VOID_ON_FALSE(s_wifi_command_queue != NULL, TAG,
                             "Failed to create Wi-Fi management queue");
    telemetry_uart_configure_wifi(true, queue_wifi_command, NULL);
    BaseType_t wifi_task_ok = xTaskCreate(wifi_control_task, "wifi_control",
                                          6144, NULL, 7, NULL);
    ESP_RETURN_VOID_ON_FALSE(wifi_task_ok == pdPASS, TAG,
                             "Failed to create Wi-Fi management task");
    BaseType_t wifi_state_task_ok = xTaskCreate(
        wifi_state_publish_task, "wifi_state_publish", 3072, NULL, 6,
        &s_wifi_state_publish_task_handle);
    ESP_RETURN_VOID_ON_FALSE(wifi_state_task_ok == pdPASS, TAG,
                             "Failed to create Wi-Fi state publisher task");
    app_wifi_set_state_changed_callback(wifi_state_changed, NULL);
    static const app_wifi_portal_callbacks_t portal_callbacks = {
        .get_device_status = portal_get_device_status,
        .submit_modem_command = portal_submit_modem_command,
        .apply_video_settings = portal_apply_video_settings,
        .set_portal_enabled = portal_set_enabled,
    };
    ESP_ERROR_CHECK(app_wifi_portal_register_callbacks(&portal_callbacks, NULL));
    telemetry_uart_configure_ap_stream(true, TELEMETRY_AP_STREAM_STARTING,
                                       set_ap_stream_enabled, NULL);
    telemetry_uart_set_ap_stream_state(TELEMETRY_AP_STREAM_STARTING, ESP_OK);
    esp_err_t initial_ap_ret = set_ap_stream_enabled(true, NULL);
    telemetry_uart_set_ap_stream_state(
        initial_ap_ret == ESP_OK ? TELEMETRY_AP_STREAM_ENABLED
                                 : TELEMETRY_AP_STREAM_ERROR,
        initial_ap_ret);
    if (initial_ap_ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "Robot local configuration console ready: http://%s:8081/",
                 CONFIG_SERVER_IP);
    } else {
        ESP_LOGE(TAG, "Initial local configuration hotspot failed: %s",
                 esp_err_to_name(initial_ap_ret));
    }
#else
    telemetry_uart_configure_wifi(false, NULL, NULL);
    telemetry_uart_configure_ap_stream(false, TELEMETRY_AP_STREAM_UNSUPPORTED,
                                       NULL, NULL);
#endif
    /* Telemetry owns the cloud control socket, so it must not depend on AP state. */
    start_telemetry_task();
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
    publish_initial_wifi_state();
#endif
#ifndef CONFIG_L610_SOFTAP_SHARE_ENABLE
    start_camera_task();
#endif

    // install usbh cdc driver
    usbh_cdc_driver_config_t config = {
        .task_stack_size = 1024 * 4,
        .task_priority = configMAX_PRIORITIES - 1,
        .task_coreid = 0,
        .skip_init_usb_host_driver = false,
    };
    ESP_ERROR_CHECK(usbh_cdc_driver_install(&config));

    install_rndis(L610_USB_VID, L610_RNDIS_PID, "USB RNDIS0");
#if CONFIG_EXAMPLE_ENABLE_AT_CMD
    /* Modem query parsing includes a large response buffer plus cell state.
     * Keep explicit headroom for the parser/sscanf call chain. */
    BaseType_t task_ok = xTaskCreate(l610_at_manager_task, "l610_at_manager", 8192, NULL, 8, NULL);
    ESP_ERROR_CHECK(task_ok == pdPASS ? ESP_OK : ESP_FAIL);
#endif

    ESP_LOGI(TAG, "Waiting for L610 enumeration, LTE registration, RNDIS and DHCP");

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            s_event_group,
            EVENT_AT_READY_BIT | EVENT_GOT_IP_BIT | EVENT_WIFI_POLICY_BIT,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
#if CONFIG_EXAMPLE_ENABLE_AT_CMD
        if (bits & EVENT_AT_READY_BIT) {
            ESP_LOGI(TAG, "L610 RNDIS control interface ready");
        }
#endif
        if (bits & EVENT_GOT_IP_BIT) {
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
            snprintf(s_modem_wifi_config.dns, sizeof(s_modem_wifi_config.dns),
                     "%s", s_rndis_dns);
            if (s_ap_stream_requested &&
                    (!app_wifi_softap_is_started() ||
                     !camera_stream_local_mjpeg_is_enabled())) {
                telemetry_uart_set_ap_stream_state(TELEMETRY_AP_STREAM_STARTING, ESP_OK);
                ESP_LOGI(TAG, "Retrying requested local configuration hotspot; free heap=%lu bytes",
                         (unsigned long)esp_get_free_heap_size());
                esp_err_t ap_ret = set_ap_stream_enabled(true, NULL);
                telemetry_uart_set_ap_stream_state(
                    ap_ret == ESP_OK ? TELEMETRY_AP_STREAM_ENABLED
                                     : TELEMETRY_AP_STREAM_ERROR,
                    ap_ret);
                if (ap_ret == ESP_OK) {
                    ESP_LOGI(TAG, "Local configuration hotspot ready: SSID=%s, gateway=%s, DNS=%s",
                             s_modem_wifi_config.ap_ssid, CONFIG_SERVER_IP,
                             s_modem_wifi_config.dns);
                } else {
                    ESP_LOGE(TAG, "Local configuration hotspot retry failed: %s",
                             esp_err_to_name(ap_ret));
                }
            }
            /* Preserve the verified 8 FPS profile ordering: AP first, camera second. */
            start_camera_task();
            esp_netif_t *cloud_netif = current_cloud_netif();
            if (cloud_netif != NULL) {
                const char *label = app_wifi_sta_uplink_is_active()
                    ? "Wi-Fi STA" : "L610 RNDIS";
                esp_err_t uplink_ret = app_wifi_refresh_uplink();
                if (uplink_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Could not publish current cloud uplink: %s",
                             esp_err_to_name(uplink_ret));
                } else {
                    ESP_LOGI(TAG, "Published current cloud uplink: %s", label);
                }
            } else {
                camera_stream_set_network_ready(false);
                telemetry_uart_set_network_ready(false);
            }
            publish_initial_wifi_state();
#else
            ESP_ERROR_CHECK(activate_cloud_uplink(s_rndis_netif, "L610 RNDIS"));
#endif
            ESP_LOGI(TAG, "L610 RNDIS video uplink enabled, DNS=%s", s_rndis_dns);
#if CONFIG_EXAMPLE_USB_RNDIS_DOWNLOAD_SPEED_TEST
            ESP_LOGW(TAG, "Download speed test is unavailable in the camera-only build");
#endif
        }
#ifdef CONFIG_L610_SOFTAP_SHARE_ENABLE
        if ((bits & EVENT_WIFI_POLICY_BIT) ||
                (!s_rndis_has_ip && !s_ap_stream_requested)) {
            esp_err_t policy_ret = app_wifi_apply_auto_failover(
                s_rndis_has_ip, s_ap_stream_requested);
            if (policy_ret != ESP_OK) {
                ESP_LOGW(TAG, "Wi-Fi failover policy could not be applied: %s",
                         esp_err_to_name(policy_ret));
            }
        }
#endif
    }
}

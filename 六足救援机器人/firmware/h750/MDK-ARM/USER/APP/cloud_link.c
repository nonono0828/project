#include "cloud_link.h"
#include "env_sensors.h"
#include "gas_alarm.h"
#include "gps_nmea.h"
#include "person_detect.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define CLOUD_MAGIC_0 0xA5U
#define CLOUD_MAGIC_1 0x5AU
#define CLOUD_VERSION 0x01U
#define CLOUD_TYPE_TELEMETRY 0x01U
#define CLOUD_TYPE_CONTROL 0x02U
#define CLOUD_FIXED_HEADER_SIZE 10U
#define CLOUD_FRAME_OVERHEAD 14U
#define CLOUD_RX_JSON_MAX 96U
#define CLOUD_RX_FRAME_MAX (CLOUD_RX_JSON_MAX + CLOUD_FRAME_OVERHEAD)
#define CLOUD_TX_JSON_MAX 384U
#define CLOUD_TX_FRAME_MAX (CLOUD_TX_JSON_MAX + CLOUD_FRAME_OVERHEAD)
#define CLOUD_FRAME_TIMEOUT_MS 300U
#define CLOUD_CONTROL_TIMEOUT_MS 500U
#define CLOUD_TELEMETRY_PERIOD_MS 1000U
#define CLOUD_SENSOR_STALE_MS 3000U
#define CLOUD_GPS_STALE_MS 5000U
#define CLOUD_SEQUENCE_MAGIC 0x434C4431UL

typedef enum {
    CLOUD_BUTTON_FORWARD = 0,
    CLOUD_BUTTON_BACKWARD,
    CLOUD_BUTTON_LEFT,
    CLOUD_BUTTON_RIGHT,
    CLOUD_BUTTON_COUNT
} CloudButton;

typedef struct {
    uint8_t down;
    uint32_t last_down_tick;
} CloudButtonState;

static CloudLinkStatus s_status;
static CloudButtonState s_buttons[CLOUD_BUTTON_COUNT];
static uint8_t s_rx_frame[CLOUD_RX_FRAME_MAX];
static uint16_t s_rx_used;
static uint16_t s_rx_expected;
static uint32_t s_rx_started_tick;
static uint8_t s_tx_frame[CLOUD_TX_FRAME_MAX];
static uint16_t s_tx_length;
static uint16_t s_tx_offset;
static uint32_t s_tx_frame_sequence;
static uint32_t s_next_tx_sequence;
static uint32_t s_last_telemetry_tick;
static uint32_t s_last_gas_change_count;
static uint32_t s_last_person_change_count;
static uint8_t s_last_gas_initialized;
static uint8_t s_last_person_initialized;
static uint8_t s_force_telemetry;
static uint8_t s_physical_rearm_mask;

static uint16_t read_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           data[3];
}

static void write_u16_be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= (uint16_t)data[index] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void parser_reset(void)
{
    s_rx_used = 0U;
    s_rx_expected = 0U;
    s_rx_started_tick = 0U;
}

static void release_control_locked(void)
{
    memset(s_buttons, 0, sizeof(s_buttons));
    s_status.control_active = 0U;
    s_status.motion_ry = 0;
    s_status.motion_lx = 0;
}

static void update_motion_locked(uint32_t now)
{
    uint8_t index;
    uint8_t forward;
    uint8_t backward;
    uint8_t left;
    uint8_t right;

    for (index = 0U; index < CLOUD_BUTTON_COUNT; index++) {
        if (s_buttons[index].down &&
            (now - s_buttons[index].last_down_tick) >=
                CLOUD_CONTROL_TIMEOUT_MS) {
            s_buttons[index].down = 0U;
            s_status.control_timeout_count++;
        }
    }

    forward = s_buttons[CLOUD_BUTTON_FORWARD].down;
    backward = s_buttons[CLOUD_BUTTON_BACKWARD].down;
    left = s_buttons[CLOUD_BUTTON_LEFT].down;
    right = s_buttons[CLOUD_BUTTON_RIGHT].down;

    if ((forward && backward) || (left && right)) {
        s_status.motion_ry = 0;
        s_status.motion_lx = 0;
    } else {
        s_status.motion_ry =
            forward == backward ? 0 : (forward ? 127 : -128);
        s_status.motion_lx =
            left == right ? 0 : (left ? -128 : 127);
    }
    s_status.control_active =
        (s_status.motion_ry != 0 || s_status.motion_lx != 0) ? 1U : 0U;
}

static const char *skip_whitespace(const char *cursor, const char *end)
{
    while (cursor < end &&
           (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\r' || *cursor == '\n')) {
        cursor++;
    }
    return cursor;
}

static uint8_t parse_json_string(const char **cursor, const char *end,
                                 char *output, uint8_t capacity)
{
    const char *position = *cursor;
    uint8_t length = 0U;

    if (position >= end || *position != '"' ||
        output == NULL || capacity == 0U) {
        return 0U;
    }
    position++;
    while (position < end && *position != '"') {
        uint8_t value = (uint8_t)*position;

        if (value < 0x20U || *position == '\\' ||
            length >= (uint8_t)(capacity - 1U)) {
            return 0U;
        }
        output[length++] = *position++;
    }
    if (position >= end || *position != '"') {
        return 0U;
    }
    output[length] = '\0';
    *cursor = position + 1;
    return 1U;
}

static uint8_t parse_json_u32(const char **cursor, const char *end,
                              uint32_t *output)
{
    const char *position = *cursor;
    uint32_t value = 0U;
    uint8_t digit_count = 0U;

    if (output == NULL || position >= end ||
        *position < '0' || *position > '9') {
        return 0U;
    }
    while (position < end && *position >= '0' && *position <= '9') {
        uint32_t digit = (uint32_t)(*position - '0');

        if (value > (0xFFFFFFFFUL - digit) / 10U) {
            return 0U;
        }
        value = value * 10U + digit;
        digit_count++;
        position++;
    }
    if (digit_count == 0U) {
        return 0U;
    }
    *output = value;
    *cursor = position;
    return 1U;
}

static uint8_t control_sequence_is_newer(uint32_t candidate,
                                         uint32_t previous)
{
    return (int32_t)(candidate - previous) > 0 ? 1U : 0U;
}

static uint8_t map_button(const char *name, CloudButton *button)
{
    if (strcmp(name, "PAD_UP") == 0) {
        *button = CLOUD_BUTTON_FORWARD;
    } else if (strcmp(name, "PAD_DOWN") == 0) {
        *button = CLOUD_BUTTON_BACKWARD;
    } else if (strcmp(name, "PAD_LEFT") == 0) {
        *button = CLOUD_BUTTON_LEFT;
    } else if (strcmp(name, "PAD_RIGHT") == 0) {
        *button = CLOUD_BUTTON_RIGHT;
    } else {
        return 0U;
    }
    return 1U;
}

static uint8_t parse_control_json(const uint8_t *json, uint16_t length,
                                  CloudButton *button, uint8_t *is_down,
                                  uint32_t *control_sequence)
{
    const char *cursor = (const char *)json;
    const char *end = cursor + length;
    char key[16];
    char value[12];
    char button_name[12] = {0};
    char state_name[6] = {0};
    uint8_t button_seen = 0U;
    uint8_t state_seen = 0U;
    uint8_t sequence_seen = 0U;

    cursor = skip_whitespace(cursor, end);
    if (cursor >= end || *cursor++ != '{') {
        return 0U;
    }

    for (;;) {
        cursor = skip_whitespace(cursor, end);
        if (cursor >= end || *cursor == '}') {
            return 0U;
        }
        if (!parse_json_string(&cursor, end, key, sizeof(key))) {
            return 0U;
        }
        cursor = skip_whitespace(cursor, end);
        if (cursor >= end || *cursor++ != ':') {
            return 0U;
        }
        cursor = skip_whitespace(cursor, end);
        if (strcmp(key, "button") == 0 && !button_seen &&
            parse_json_string(&cursor, end, value, sizeof(value)) &&
            strlen(value) < sizeof(button_name)) {
            strcpy(button_name, value);
            button_seen = 1U;
        } else if (strcmp(key, "state") == 0 && !state_seen &&
                   parse_json_string(&cursor, end, value, sizeof(value)) &&
                   strlen(value) < sizeof(state_name)) {
            strcpy(state_name, value);
            state_seen = 1U;
        } else if (strcmp(key, "control_seq") == 0 && !sequence_seen &&
                   parse_json_u32(&cursor, end, control_sequence)) {
            sequence_seen = 1U;
        } else {
            return 0U;
        }

        cursor = skip_whitespace(cursor, end);
        if (cursor >= end) {
            return 0U;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == '}') {
            cursor++;
            break;
        }
        return 0U;
    }

    cursor = skip_whitespace(cursor, end);
    if (cursor != end || !button_seen || !state_seen || !sequence_seen ||
        !map_button(button_name, button)) {
        return 0U;
    }
    if (strcmp(state_name, "down") == 0) {
        *is_down = 1U;
    } else if (strcmp(state_name, "up") == 0) {
        *is_down = 0U;
    } else {
        return 0U;
    }
    return 1U;
}

static void apply_control(CloudButton button, uint8_t is_down,
                          uint32_t frame_sequence,
                          uint32_t control_sequence)
{
    uint32_t now = HAL_GetTick();
    uint8_t button_mask = (uint8_t)(1U << (uint8_t)button);

    taskENTER_CRITICAL();
    s_status.last_rx_sequence = frame_sequence;
    if (s_status.control_sequence_valid &&
        !control_sequence_is_newer(control_sequence,
                                   s_status.last_control_sequence)) {
        s_status.command_rejected++;
        s_status.control_sequence_rejected++;
        taskEXIT_CRITICAL();
        return;
    }

    s_status.control_sequence_valid = 1U;
    s_status.last_control_sequence = control_sequence;
    s_status.last_command_tick = now;
    if (s_status.physical_override_active ||
        s_physical_rearm_mask != 0U) {
        if (is_down) {
            s_physical_rearm_mask |= button_mask;
        } else {
            s_physical_rearm_mask &= (uint8_t)~button_mask;
        }
        s_status.physical_rearm_mask = s_physical_rearm_mask;
        s_status.command_rejected++;
        s_status.physical_blocked_count++;
        release_control_locked();
        taskEXIT_CRITICAL();
        return;
    }

    s_buttons[button].down = is_down;
    if (is_down) {
        s_buttons[button].last_down_tick = now;
    }
    s_status.command_frames++;
    update_motion_locked(now);
    taskEXIT_CRITICAL();
}

static void validate_rx_frame(void)
{
    uint16_t json_length = read_u16_be(&s_rx_frame[4]);
    uint16_t crc_offset =
        (uint16_t)(CLOUD_FIXED_HEADER_SIZE + json_length);
    uint16_t tail_offset = (uint16_t)(crc_offset + 2U);
    uint16_t expected_crc;
    uint16_t actual_crc;
    CloudButton button;
    uint8_t is_down;
    uint32_t control_sequence;

    if (s_rx_frame[2] != CLOUD_VERSION ||
        s_rx_frame[3] != CLOUD_TYPE_CONTROL) {
        s_status.type_error_count++;
        parser_reset();
        return;
    }
    if (s_rx_frame[tail_offset] != 0x0DU ||
        s_rx_frame[tail_offset + 1U] != 0x0AU) {
        s_status.tail_error_count++;
        parser_reset();
        return;
    }

    expected_crc = read_u16_be(&s_rx_frame[crc_offset]);
    actual_crc = crc16_ccitt_false(
        &s_rx_frame[2], (uint16_t)(8U + json_length));
    if (expected_crc != actual_crc) {
        s_status.crc_error_count++;
        parser_reset();
        return;
    }

    s_status.rx_frames++;
    if (parse_control_json(&s_rx_frame[CLOUD_FIXED_HEADER_SIZE],
                           json_length, &button, &is_down,
                           &control_sequence)) {
        apply_control(button, is_down, read_u32_be(&s_rx_frame[6]),
                      control_sequence);
    } else {
        s_status.json_error_count++;
        s_status.command_rejected++;
    }
    parser_reset();
}

static uint32_t magnitude_i32(int32_t value)
{
    if (value < 0) {
        return (uint32_t)(-(value + 1)) + 1U;
    }
    return (uint32_t)value;
}

static uint8_t format_u32(char *output, uint8_t capacity,
                          uint32_t value, uint8_t minimum_digits)
{
    char reversed[10];
    uint8_t digits = 0U;
    uint8_t padding;
    uint8_t index;

    do {
        reversed[digits++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && digits < sizeof(reversed));

    padding = minimum_digits > digits ?
        (uint8_t)(minimum_digits - digits) : 0U;
    if ((uint16_t)padding + digits + 1U > capacity) {
        return 0U;
    }
    for (index = 0U; index < padding; index++) {
        output[index] = '0';
    }
    for (index = 0U; index < digits; index++) {
        output[padding + index] = reversed[digits - 1U - index];
    }
    output[padding + digits] = '\0';
    return 1U;
}

static int32_t round_scaled(double value, double scale)
{
    double scaled = value * scale;

    if (scaled >= 0.0) {
        scaled += 0.5;
    } else {
        scaled -= 0.5;
    }
    return (int32_t)scaled;
}

static uint8_t format_fixed(char *output, uint8_t capacity,
                            int32_t scaled, uint32_t divisor,
                            uint8_t decimal_digits)
{
    uint32_t magnitude = magnitude_i32(scaled);
    char integer[11];
    char fraction[7];
    uint8_t integer_length;
    uint8_t fraction_length;
    uint8_t offset = 0U;

    if (!format_u32(integer, sizeof(integer),
                    magnitude / divisor, 1U) ||
        !format_u32(fraction, sizeof(fraction),
                    magnitude % divisor, decimal_digits)) {
        return 0U;
    }
    integer_length = (uint8_t)strlen(integer);
    fraction_length = (uint8_t)strlen(fraction);
    if ((uint16_t)(scaled < 0 ? 1U : 0U) + integer_length +
        fraction_length + 2U > capacity) {
        return 0U;
    }
    if (scaled < 0) {
        output[offset++] = '-';
    }
    memcpy(&output[offset], integer, integer_length);
    offset = (uint8_t)(offset + integer_length);
    output[offset++] = '.';
    memcpy(&output[offset], fraction, fraction_length);
    offset = (uint8_t)(offset + fraction_length);
    output[offset] = '\0';
    return 1U;
}

static uint8_t format_fixed_1(char *output, uint8_t capacity,
                              double value)
{
    return format_fixed(output, capacity,
                        round_scaled(value, 10.0), 10U, 1U);
}

static uint8_t format_fixed_6(char *output, uint8_t capacity,
                              double value)
{
    return format_fixed(output, capacity,
                        round_scaled(value, 1000000.0),
                        1000000UL, 6U);
}

static uint8_t append_text(uint8_t *output, uint16_t capacity,
                           uint16_t *used, const char *text)
{
    uint16_t length = (uint16_t)strlen(text);

    if ((uint32_t)*used + length > capacity) {
        return 0U;
    }
    memcpy(&output[*used], text, length);
    *used = (uint16_t)(*used + length);
    return 1U;
}

static uint32_t next_sequence(void)
{
    uint32_t sequence = s_next_tx_sequence++;

    RTC->BKP1R = s_next_tx_sequence;
    return sequence;
}

static uint8_t build_telemetry_frame(uint32_t now)
{
    const EnvSensorStatus *env = EnvSensors_GetStatus();
    const GPS_NMEA_Status *gps = GPS_NMEA_GetStatus();
    const GasAlarmStatus *gas = GasAlarm_GetStatus();
    const PersonDetectStatus *person = PersonDetect_GetStatus();
    char temperature[16] = "null";
    char humidity[16] = "null";
    char altitude[16] = "null";
    char pressure[16] = "null";
    char gas_value[8] = "null";
    char person_value[8] = "null";
    char latitude[20] = "null";
    char longitude[20] = "null";
    char satellites[8] = "null";
    char speed[16] = "null";
    char heading[16] = "null";
    uint8_t *json = &s_tx_frame[CLOUD_FIXED_HEADER_SIZE];
    uint16_t json_length = 0U;
    uint16_t crc;

    if (env->aht20_ok && env->last_aht_update_tick != 0U &&
        (now - env->last_aht_update_tick) <= CLOUD_SENSOR_STALE_MS &&
        env->aht_temperature_c >= -40.0f &&
        env->aht_temperature_c <= 85.0f &&
        env->humidity_rh >= 0.0f && env->humidity_rh <= 100.0f) {
        if (!format_fixed_1(temperature, sizeof(temperature),
                            env->aht_temperature_c) ||
            !format_fixed_1(humidity, sizeof(humidity),
                            env->humidity_rh)) {
            return 0U;
        }
    }
    if (env->bmp280_ok && env->last_bmp_update_tick != 0U &&
        (now - env->last_bmp_update_tick) <= CLOUD_SENSOR_STALE_MS &&
        env->pressure_hpa >= 300.0f &&
        env->pressure_hpa <= 1100.0f &&
        env->altitude_m >= -500.0f &&
        env->altitude_m <= 10000.0f) {
        if (!format_fixed_1(altitude, sizeof(altitude),
                            env->altitude_m) ||
            !format_fixed_1(pressure, sizeof(pressure),
                            env->pressure_hpa)) {
            return 0U;
        }
    }
    if (gas->initialized) {
        if (!format_u32(gas_value, sizeof(gas_value),
                        gas->alarm, 1U)) {
            return 0U;
        }
    }
    if (person->initialized) {
        if (!format_u32(person_value, sizeof(person_value),
                        person->detected, 1U)) {
            return 0U;
        }
    }

    if ((gps->gga_count != 0U || gps->gsv_count != 0U) &&
        gps->last_satellites_tick != 0U &&
        (now - gps->last_satellites_tick) <= CLOUD_GPS_STALE_MS &&
        gps->satellites <= 99U) {
        if (!format_u32(satellites, sizeof(satellites),
                        gps->satellites, 1U)) {
            return 0U;
        }
    }
    if (gps->fix_valid && gps->last_position_tick != 0U &&
        (now - gps->last_position_tick) <= CLOUD_GPS_STALE_MS &&
        gps->latitude_deg >= -90.0 && gps->latitude_deg <= 90.0 &&
        gps->longitude_deg >= -180.0 &&
        gps->longitude_deg <= 180.0) {
        if (!format_fixed_6(latitude, sizeof(latitude),
                            gps->latitude_deg) ||
            !format_fixed_6(longitude, sizeof(longitude),
                            gps->longitude_deg)) {
            return 0U;
        }
        if (gps->speed_valid && gps->last_speed_tick != 0U &&
            (now - gps->last_speed_tick) <= CLOUD_GPS_STALE_MS &&
            gps->speed_knots >= 0.0f &&
            (gps->speed_knots * 0.514444f) <= 100.0f) {
            if (!format_fixed_1(
                    speed, sizeof(speed),
                    (double)(gps->speed_knots * 0.514444f))) {
                return 0U;
            }
        }
        if (gps->heading_valid && gps->last_heading_tick != 0U &&
            (now - gps->last_heading_tick) <= CLOUD_GPS_STALE_MS &&
            gps->heading_deg >= 0.0f &&
            gps->heading_deg < 360.0f) {
            double heading_value = gps->heading_deg >= 359.95f ?
                0.0 : (double)gps->heading_deg;

            if (!format_fixed_1(heading, sizeof(heading),
                                heading_value)) {
                return 0U;
            }
        }
    }

    if (!append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     "{\"sensor\":{\"temperature\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     temperature) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"humidity\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     humidity) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"altitude\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     altitude) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"pressure\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     pressure) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"gas\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     gas_value) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"person_detected\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     person_value) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     "},\"gps\":{\"lat\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     latitude) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"lng\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     longitude) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"satellites\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     satellites) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"speed\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     speed) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     ",\"heading\":") ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     heading) ||
        !append_text(json, CLOUD_TX_JSON_MAX, &json_length,
                     "}}")) {
        return 0U;
    }

    s_tx_frame_sequence = next_sequence();
    s_tx_frame[0] = CLOUD_MAGIC_0;
    s_tx_frame[1] = CLOUD_MAGIC_1;
    s_tx_frame[2] = CLOUD_VERSION;
    s_tx_frame[3] = CLOUD_TYPE_TELEMETRY;
    write_u16_be(&s_tx_frame[4], json_length);
    write_u32_be(&s_tx_frame[6], s_tx_frame_sequence);
    crc = crc16_ccitt_false(
        &s_tx_frame[2], (uint16_t)(8U + json_length));
    write_u16_be(
        &s_tx_frame[CLOUD_FIXED_HEADER_SIZE + json_length], crc);
    s_tx_frame[CLOUD_FIXED_HEADER_SIZE + json_length + 2U] = 0x0DU;
    s_tx_frame[CLOUD_FIXED_HEADER_SIZE + json_length + 3U] = 0x0AU;
    s_tx_length = (uint16_t)(json_length + CLOUD_FRAME_OVERHEAD);
    s_tx_offset = 0U;
    s_status.tx_pending = 1U;
    return 1U;
}

void CloudLink_Init(void)
{
    static const uint8_t crc_test[] = "123456789";

    memset(&s_status, 0, sizeof(s_status));
    memset(s_buttons, 0, sizeof(s_buttons));
    parser_reset();
    s_tx_length = 0U;
    s_tx_offset = 0U;
    s_tx_frame_sequence = 0U;
    s_last_telemetry_tick = 0U;
    s_last_gas_change_count = 0U;
    s_last_person_change_count = 0U;
    s_last_gas_initialized = 0U;
    s_last_person_initialized = 0U;
    s_force_telemetry = 1U;
    s_physical_rearm_mask = 0U;
    s_status.crc_self_test_ok =
        crc16_ccitt_false(crc_test, sizeof(crc_test) - 1U) ==
            0x29B1U ? 1U : 0U;

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();
    if (RTC->BKP0R == CLOUD_SEQUENCE_MAGIC) {
        s_next_tx_sequence = RTC->BKP1R;
    } else {
        RTC->BKP0R = CLOUD_SEQUENCE_MAGIC;
        RTC->BKP1R = 0U;
        s_next_tx_sequence = 0U;
    }
}

void CloudLink_SetTransportOnline(uint8_t online)
{
    online = online ? 1U : 0U;
    if (online == s_status.transport_online) {
        return;
    }

    s_status.transport_online = online;
    parser_reset();
    if (online) {
        s_force_telemetry = 1U;
    } else {
        taskENTER_CRITICAL();
        s_physical_rearm_mask = 0U;
        s_status.physical_rearm_mask = 0U;
        release_control_locked();
        taskEXIT_CRITICAL();
        s_tx_length = 0U;
        s_tx_offset = 0U;
        s_status.tx_pending = 0U;
    }
}

void CloudLink_Feed(const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if (data == NULL) {
        return;
    }
    for (index = 0U; index < length; index++) {
        uint8_t byte = data[index];

        s_status.rx_bytes++;
        if (s_rx_used >= 2U &&
            (HAL_GetTick() - s_rx_started_tick) >=
                CLOUD_FRAME_TIMEOUT_MS) {
            s_status.frame_timeout_count++;
            parser_reset();
        }
        if (s_rx_used == 0U) {
            if (byte == CLOUD_MAGIC_0) {
                s_rx_frame[0] = byte;
                s_rx_used = 1U;
            }
            continue;
        }
        if (s_rx_used == 1U) {
            if (byte == CLOUD_MAGIC_1) {
                s_rx_frame[1] = byte;
                s_rx_used = 2U;
                s_rx_started_tick = HAL_GetTick();
            } else if (byte != CLOUD_MAGIC_0) {
                s_rx_used = 0U;
            }
            continue;
        }
        if (s_rx_used >= sizeof(s_rx_frame)) {
            s_status.length_error_count++;
            parser_reset();
            continue;
        }

        s_rx_frame[s_rx_used++] = byte;
        if (s_rx_used == 6U) {
            uint16_t json_length = read_u16_be(&s_rx_frame[4]);

            if (json_length == 0U ||
                json_length > CLOUD_RX_JSON_MAX) {
                s_status.length_error_count++;
                parser_reset();
                continue;
            }
            s_rx_expected =
                (uint16_t)(json_length + CLOUD_FRAME_OVERHEAD);
        }
        if (s_rx_expected != 0U && s_rx_used == s_rx_expected) {
            validate_rx_frame();
        }
    }
}

void CloudLink_Service(void)
{
    const GasAlarmStatus *gas = GasAlarm_GetStatus();
    const PersonDetectStatus *person = PersonDetect_GetStatus();
    uint32_t now = HAL_GetTick();

    taskENTER_CRITICAL();
    update_motion_locked(now);
    taskEXIT_CRITICAL();

    if (s_rx_used >= 2U &&
        (now - s_rx_started_tick) >= CLOUD_FRAME_TIMEOUT_MS) {
        s_status.frame_timeout_count++;
        parser_reset();
    }

    if (gas->initialized != s_last_gas_initialized ||
        gas->change_count != s_last_gas_change_count) {
        s_last_gas_initialized = gas->initialized;
        s_last_gas_change_count = gas->change_count;
        s_force_telemetry = 1U;
    }
    if (person->initialized != s_last_person_initialized ||
        person->change_count != s_last_person_change_count) {
        s_last_person_initialized = person->initialized;
        s_last_person_change_count = person->change_count;
        s_force_telemetry = 1U;
    }

    if (!s_status.transport_online || s_status.tx_pending) {
        return;
    }
    if (s_force_telemetry ||
        (now - s_last_telemetry_tick) >=
            CLOUD_TELEMETRY_PERIOD_MS) {
        s_last_telemetry_tick = now;
        if (build_telemetry_frame(now)) {
            s_force_telemetry = 0U;
        } else {
            s_status.tx_error_count++;
            s_force_telemetry = 0U;
        }
    }
}

uint16_t CloudLink_TxPeek(const uint8_t **data, uint16_t max_length)
{
    uint16_t remaining;

    if (data == NULL || max_length == 0U ||
        !s_status.tx_pending || s_tx_offset >= s_tx_length) {
        return 0U;
    }
    remaining = (uint16_t)(s_tx_length - s_tx_offset);
    if (remaining > max_length) {
        remaining = max_length;
    }
    *data = &s_tx_frame[s_tx_offset];
    return remaining;
}

void CloudLink_TxCommit(uint16_t length)
{
    uint16_t remaining;

    if (!s_status.tx_pending || s_tx_offset >= s_tx_length) {
        return;
    }
    remaining = (uint16_t)(s_tx_length - s_tx_offset);
    if (length > remaining) {
        length = remaining;
    }
    s_tx_offset = (uint16_t)(s_tx_offset + length);
    s_status.tx_bytes += length;
    if (s_tx_offset >= s_tx_length) {
        s_status.tx_frames++;
        s_status.last_tx_sequence = s_tx_frame_sequence;
        s_status.last_tx_tick = HAL_GetTick();
        s_status.tx_pending = 0U;
        s_tx_length = 0U;
        s_tx_offset = 0U;
    }
}

void CloudLink_TxFailed(void)
{
    s_status.tx_error_count++;
}

uint8_t CloudLink_GetMotion(int8_t *right_y, int8_t *left_x)
{
    uint8_t active;

    if (right_y == NULL || left_x == NULL) {
        return 0U;
    }
    taskENTER_CRITICAL();
    update_motion_locked(HAL_GetTick());
    *right_y = s_status.motion_ry;
    *left_x = s_status.motion_lx;
    active = s_status.control_active;
    taskEXIT_CRITICAL();
    return active;
}

void CloudLink_SetPhysicalOverride(uint8_t active)
{
    uint8_t index;

    active = active ? 1U : 0U;
    taskENTER_CRITICAL();
    if (active) {
        if (!s_status.physical_override_active) {
            s_status.physical_override_count++;
        }
        for (index = 0U; index < CLOUD_BUTTON_COUNT; index++) {
            if (s_buttons[index].down) {
                s_physical_rearm_mask |=
                    (uint8_t)(1U << index);
            }
        }
        s_status.physical_override_active = 1U;
        release_control_locked();
    } else {
        s_status.physical_override_active = 0U;
    }
    s_status.physical_rearm_mask = s_physical_rearm_mask;
    taskEXIT_CRITICAL();
}

const CloudLinkStatus *CloudLink_GetStatus(void)
{
    return &s_status;
}

#include "d6_radar_port.h"
#include <string.h>

#define D6_ANGLE_SCALE 64.0f
#define D6_RX_BUFFER_SIZE 256U
#define D6_ACK_TIMEOUT_MS 500U
#define D6_HEADER_1 0xAAU
#define D6_HEADER_2 0x55U
#define D6_ACK_HEADER_1 0xA5U
#define D6_ACK_HEADER_2 0x5AU
#define D6_MAX_POINTS 25U

typedef enum {
    D6_MODE_IDLE = 0,
    D6_MODE_WAIT_ACK,
    D6_MODE_DATA
} D6Mode;

static const uint8_t s_start_command[4] = {0xAAU, 0x55U, 0xF0U, 0x0FU};
static const uint8_t s_stop_command[4] = {0xAAU, 0x55U, 0xF5U, 0x0AU};

static D6RadarWriteFn s_write_fn;
static uint16_t s_distance[360];
static uint32_t s_distance_tick[360];
static D6RadarStatus s_status;
static D6Mode s_mode;

static uint8_t s_ack_buffer[20];
static uint8_t s_ack_index;
static uint8_t s_ack_state;
static uint8_t s_ack_expected;
static uint8_t s_ack_type;
static uint32_t s_ack_deadline;

static uint8_t s_data_buffer[D6_RX_BUFFER_SIZE];
static uint16_t s_data_index;
static uint8_t s_data_state;
static uint16_t s_data_expected;

static uint16_t calculate_checksum(const uint8_t *data, uint8_t point_count)
{
    uint16_t checksum = 0x55AAU;
    uint16_t first_sample_angle =
        (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
    uint16_t last_sample_angle =
        (uint16_t)(data[6] | ((uint16_t)data[7] << 8));
    uint16_t type_and_count =
        (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
    uint8_t index;

    checksum ^= first_sample_angle;
    checksum ^= type_and_count;
    checksum ^= last_sample_angle;
    for (index = 0U; index < point_count; index++) {
        const uint8_t *point = &data[10U + index * 3U];
        checksum ^= point[0];
        checksum ^= (uint16_t)(point[1] | ((uint16_t)point[2] << 8));
    }
    return checksum;
}

static uint8_t parse_frame(const uint8_t *data, uint16_t length)
{
    uint8_t point_count;
    uint8_t packet_type;
    uint16_t first_sample_angle;
    uint16_t last_sample_angle;
    uint16_t received_checksum;
    float start_angle;
    float end_angle;
    uint32_t frame_tick;
    uint8_t index;

    if (length < 10U) {
        s_status.length_error_count++;
        return 0U;
    }
    point_count = data[3];
    if (point_count > D6_MAX_POINTS ||
        length != (uint16_t)(10U + point_count * 3U)) {
        s_status.length_error_count++;
        return 0U;
    }
    received_checksum = (uint16_t)(data[8] |
                                   ((uint16_t)data[9] << 8));
    if (calculate_checksum(data, point_count) != received_checksum) {
        s_status.checksum_error_count++;
        return 0U;
    }

    packet_type = data[2] & 0x01U;
    if (packet_type != 0U) {
        s_status.sync_frame_count++;
        return 1U;
    }

    first_sample_angle = (uint16_t)(data[4] |
                                    ((uint16_t)data[5] << 8));
    last_sample_angle = (uint16_t)(data[6] |
                                   ((uint16_t)data[7] << 8));
    start_angle = (float)(first_sample_angle >> 1) / D6_ANGLE_SCALE;
    end_angle = (float)(last_sample_angle >> 1) / D6_ANGLE_SCALE;
    if (end_angle < start_angle) {
        end_angle += 360.0f;
    }
    frame_tick = HAL_GetTick();

    for (index = 0U; index < point_count; index++) {
        const uint8_t *point = &data[10U + index * 3U];
        uint16_t distance =
            (uint16_t)(((uint16_t)point[2] << 6) | (point[1] >> 2));
        float angle = start_angle;
        uint16_t angle_index;

        if (point_count > 1U) {
            angle += (end_angle - start_angle) *
                     (float)index / (float)(point_count - 1U);
        }
        while (angle >= 360.0f) {
            angle -= 360.0f;
        }
        angle_index = (uint16_t)(angle + 0.5f);
        if (angle_index >= 360U) {
            angle_index = 0U;
        }
        s_distance[angle_index] = distance;
        s_distance_tick[angle_index] = frame_tick;
    }
    s_status.data_frame_count++;
    s_status.last_frame_tick = frame_tick;
    s_status.state = D6_RADAR_STATE_SCANNING;
    return 1U;
}

static void reset_ack_parser(void)
{
    s_ack_state = 0U;
    s_ack_index = 0U;
}

static void reset_data_parser(void)
{
    s_data_state = 0U;
    s_data_index = 0U;
    s_data_expected = 0U;
}

static void process_ack(uint8_t byte)
{
    switch (s_ack_state) {
    case 0:
        if (byte == D6_ACK_HEADER_1) {
            s_ack_buffer[0] = byte;
            s_ack_index = 1U;
            s_ack_state = 1U;
        }
        break;
    case 1:
        if (byte == D6_ACK_HEADER_2) {
            s_ack_buffer[1] = byte;
            s_ack_index = 2U;
            s_ack_state = 2U;
        } else {
            reset_ack_parser();
        }
        break;
    case 2:
        if (s_ack_index < sizeof(s_ack_buffer)) {
            s_ack_buffer[s_ack_index++] = byte;
        }
        if (s_ack_index >= s_ack_expected) {
            uint8_t command_type = s_ack_buffer[2];
            uint8_t valid = s_ack_type == 0U ?
                            (command_type == 0x50U) :
                            (command_type == 0x55U);
            if (valid) {
                if (s_ack_type == 0U) {
                    s_status.state = D6_RADAR_STATE_SCANNING;
                    s_mode = D6_MODE_DATA;
                } else {
                    s_status.state = D6_RADAR_STATE_IDLE;
                    s_mode = D6_MODE_IDLE;
                }
            } else {
                s_status.command_error_count++;
                s_status.state = D6_RADAR_STATE_ERROR;
                s_mode = D6_MODE_IDLE;
            }
            reset_ack_parser();
        }
        break;
    default:
        reset_ack_parser();
        break;
    }
}

static void process_data(uint8_t byte)
{
    switch (s_data_state) {
    case 0:
        if (byte == D6_HEADER_1) {
            s_data_buffer[0] = byte;
            s_data_index = 1U;
            s_data_state = 1U;
        }
        break;
    case 1:
        if (byte == D6_HEADER_2) {
            s_data_buffer[1] = byte;
            s_data_index = 2U;
            s_data_state = 2U;
        } else if (byte == D6_HEADER_1) {
            s_data_buffer[0] = byte;
            s_data_index = 1U;
        } else {
            reset_data_parser();
        }
        break;
    case 2:
        if (s_data_index >= D6_RX_BUFFER_SIZE) {
            s_status.length_error_count++;
            reset_data_parser();
            break;
        }
        s_data_buffer[s_data_index++] = byte;
        if (s_data_index == 4U) {
            s_data_expected =
                (uint16_t)(10U + s_data_buffer[3] * 3U);
            if (s_data_buffer[3] > D6_MAX_POINTS ||
                s_data_expected > D6_RX_BUFFER_SIZE) {
                s_status.length_error_count++;
                reset_data_parser();
                break;
            }
        }
        if (s_data_expected > 0U &&
            s_data_index >= s_data_expected) {
            if (parse_frame(s_data_buffer, s_data_index)) {
                s_status.frame_count++;
            }
            reset_data_parser();
        }
        break;
    default:
        reset_data_parser();
        break;
    }
}

void D6_Radar_Port_Init(D6RadarWriteFn write_fn)
{
    s_write_fn = write_fn;
    memset(s_distance, 0, sizeof(s_distance));
    memset(s_distance_tick, 0, sizeof(s_distance_tick));
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = D6_RADAR_STATE_IDLE;
    s_mode = D6_MODE_IDLE;
    reset_ack_parser();
    reset_data_parser();
}

HAL_StatusTypeDef D6_Radar_Port_Start(void)
{
    HAL_StatusTypeDef result;

    if (s_write_fn == NULL) {
        return HAL_ERROR;
    }
    reset_ack_parser();
    reset_data_parser();
    s_status.state = D6_RADAR_STATE_STARTING;
    s_mode = D6_MODE_WAIT_ACK;
    s_ack_type = 0U;
    s_ack_expected = 12U;
    s_ack_deadline = HAL_GetTick() + D6_ACK_TIMEOUT_MS;
    result = s_write_fn(s_start_command, sizeof(s_start_command));
    if (result != HAL_OK) {
        s_status.command_error_count++;
        s_status.state = D6_RADAR_STATE_ERROR;
        s_mode = D6_MODE_IDLE;
    }
    return result;
}

HAL_StatusTypeDef D6_Radar_Port_Stop(void)
{
    HAL_StatusTypeDef result;

    if (s_write_fn == NULL) {
        return HAL_ERROR;
    }
    result = s_write_fn(s_stop_command, sizeof(s_stop_command));
    if (result != HAL_OK) {
        s_status.command_error_count++;
    }
    s_status.state = D6_RADAR_STATE_IDLE;
    s_mode = D6_MODE_IDLE;
    reset_ack_parser();
    reset_data_parser();
    return result;
}

void D6_Radar_Port_Service(void)
{
    if (s_mode == D6_MODE_WAIT_ACK &&
        (int32_t)(HAL_GetTick() - s_ack_deadline) >= 0) {
        s_status.ack_timeout_count++;
        s_status.state = D6_RADAR_STATE_ERROR;
        s_mode = D6_MODE_DATA;
        reset_ack_parser();
    }
}

void D6_Radar_Port_FeedByte(uint8_t byte)
{
    s_status.byte_count++;
    if (s_mode == D6_MODE_WAIT_ACK) {
        if (byte == D6_HEADER_1) {
            s_status.state = D6_RADAR_STATE_SCANNING;
            s_mode = D6_MODE_DATA;
            process_data(byte);
        } else {
            process_ack(byte);
        }
    } else if (s_mode == D6_MODE_DATA) {
        process_data(byte);
    } else if (byte == D6_HEADER_1) {
        s_mode = D6_MODE_DATA;
        process_data(byte);
    } else if (byte == D6_ACK_HEADER_1) {
        process_ack(byte);
    }
}

void D6_Radar_Port_Feed(const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if (data == NULL) {
        return;
    }
    for (index = 0U; index < length; index++) {
        D6_Radar_Port_FeedByte(data[index]);
    }
}

uint16_t D6_Radar_Port_GetDistance(uint16_t angle)
{
    return angle < 360U ? s_distance[angle] : 0U;
}

uint16_t D6_Radar_Port_GetFreshDistance(uint16_t angle,
                                        uint32_t now,
                                        uint32_t max_age_ms)
{
    uint32_t sample_tick;

    if (angle >= 360U) {
        return 0U;
    }
    sample_tick = s_distance_tick[angle];
    if (sample_tick == 0U || (now - sample_tick) > max_age_ms) {
        return 0U;
    }
    return s_distance[angle];
}

uint16_t D6_Radar_Port_GetNearest(uint16_t *angle)
{
    uint16_t nearest = 0U;
    uint16_t nearest_angle = 0U;
    uint16_t index;

    for (index = 0U; index < 360U; index++) {
        uint16_t distance = s_distance[index];
        if (distance != 0U && (nearest == 0U || distance < nearest)) {
            nearest = distance;
            nearest_angle = index;
        }
    }
    if (angle != NULL) {
        *angle = nearest_angle;
    }
    return nearest;
}

const D6RadarStatus *D6_Radar_Port_GetStatus(void)
{
    return &s_status;
}

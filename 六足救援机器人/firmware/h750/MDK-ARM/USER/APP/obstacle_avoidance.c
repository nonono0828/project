#include "obstacle_avoidance.h"
#include "d6_radar_port.h"
#include "main.h"
#include <string.h>

#define OBSTACLE_SECTOR_HALF_WIDTH_DEG 45

typedef struct {
    uint8_t slowed;
    uint8_t blocked;
    uint8_t emergency;
} AxisLimitResult;

static ObstacleAvoidanceStatus s_status;

static uint16_t relative_to_raw_angle(int16_t relative_angle)
{
    int32_t raw_angle;

#if OBSTACLE_RADAR_ANGLE_CLOCKWISE
    raw_angle = (int32_t)OBSTACLE_RADAR_FRONT_ANGLE_DEG +
                relative_angle;
#else
    raw_angle = (int32_t)OBSTACLE_RADAR_FRONT_ANGLE_DEG -
                relative_angle;
#endif
    while (raw_angle < 0) {
        raw_angle += 360;
    }
    while (raw_angle >= 360) {
        raw_angle -= 360;
    }
    return (uint16_t)raw_angle;
}

static uint16_t sector_second_nearest(int16_t center_angle,
                                      uint32_t now)
{
    uint16_t nearest = 0xFFFFU;
    uint16_t second = 0xFFFFU;
    uint16_t valid_count = 0U;
    int16_t offset;

    for (offset = -OBSTACLE_SECTOR_HALF_WIDTH_DEG;
         offset <= OBSTACLE_SECTOR_HALF_WIDTH_DEG;
         offset++) {
        uint16_t angle =
            relative_to_raw_angle((int16_t)(center_angle + offset));
        uint16_t distance = D6_Radar_Port_GetFreshDistance(
            angle, now, OBSTACLE_POINT_STALE_MS);

        if (distance < OBSTACLE_MIN_VALID_MM ||
            distance > OBSTACLE_MAX_VALID_MM) {
            continue;
        }
        valid_count++;
        if (distance < nearest) {
            second = nearest;
            nearest = distance;
        } else if (distance < second) {
            second = distance;
        }
    }

    if (valid_count < 2U || second == 0xFFFFU) {
        return 0U;
    }
    return second;
}

static AxisLimitResult limit_axis(int8_t *command, uint16_t distance)
{
    AxisLimitResult result = {0U, 0U, 0U};
    int16_t scaled;

    if (command == NULL || *command == 0 || distance == 0U ||
        distance >= OBSTACLE_SLOW_MM) {
        return result;
    }
    if (distance <= OBSTACLE_STOP_MM) {
        *command = 0;
        result.blocked = 1U;
        if (distance <= OBSTACLE_EMERGENCY_MM) {
            result.emergency = 1U;
        }
        return result;
    }

    scaled = (int16_t)*command *
             (int16_t)(distance - OBSTACLE_STOP_MM) /
             (int16_t)(OBSTACLE_SLOW_MM - OBSTACLE_STOP_MM);
    if (scaled > 127) {
        scaled = 127;
    } else if (scaled < -128) {
        scaled = -128;
    }
    *command = (int8_t)scaled;
    result.slowed = 1U;
    return result;
}

static void merge_result(AxisLimitResult result,
                         uint8_t *slowed,
                         uint8_t *blocked,
                         uint8_t *emergency)
{
    *slowed |= result.slowed;
    *blocked |= result.blocked;
    *emergency |= result.emergency;
}

void ObstacleAvoidance_Init(uint8_t enabled)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.initialized = 1U;
    s_status.enabled = enabled ? 1U : 0U;
    s_status.state = s_status.enabled ?
                     OBSTACLE_STATE_RADAR_STALE :
                     OBSTACLE_STATE_DISABLED;
}

void ObstacleAvoidance_SetEnabled(uint8_t enabled)
{
    if (!s_status.initialized) {
        ObstacleAvoidance_Init(enabled);
        return;
    }
    s_status.enabled = enabled ? 1U : 0U;
    s_status.state = s_status.enabled ?
                     OBSTACLE_STATE_RADAR_STALE :
                     OBSTACLE_STATE_DISABLED;
}

void ObstacleAvoidance_Toggle(void)
{
    ObstacleAvoidance_SetEnabled(s_status.enabled ? 0U : 1U);
}

void ObstacleAvoidance_Apply(int8_t *right_x,
                             int8_t *right_y,
                             int8_t *left_x)
{
    const D6RadarStatus *radar;
    AxisLimitResult result;
    uint32_t now;
    uint8_t slowed = 0U;
    uint8_t blocked = 0U;
    uint8_t emergency = 0U;

    if (right_x == NULL || right_y == NULL || left_x == NULL) {
        return;
    }
    if (!s_status.initialized) {
        ObstacleAvoidance_Init(0U);
    }

    now = HAL_GetTick();
    s_status.update_count++;
    s_status.last_update_tick = now;
    s_status.input_rx = *right_x;
    s_status.input_ry = *right_y;
    s_status.input_lx = *left_x;
    s_status.output_rx = *right_x;
    s_status.output_ry = *right_y;
    s_status.output_lx = *left_x;

    if (!s_status.enabled) {
        s_status.radar_ready = 0U;
        s_status.state = OBSTACLE_STATE_DISABLED;
        return;
    }

    radar = D6_Radar_Port_GetStatus();
    if (radar->state != D6_RADAR_STATE_SCANNING ||
        radar->data_frame_count == 0U ||
        (now - radar->last_frame_tick) > OBSTACLE_RADAR_STALE_MS) {
        if (s_status.radar_ready) {
            s_status.radar_stale_count++;
        }
        s_status.radar_ready = 0U;
        s_status.state = OBSTACLE_STATE_RADAR_STALE;
        return;
    }

    s_status.radar_ready = 1U;
    s_status.front_mm = sector_second_nearest(0, now);
    s_status.right_mm = sector_second_nearest(90, now);
    s_status.rear_mm = sector_second_nearest(180, now);
    s_status.left_mm = sector_second_nearest(-90, now);

    if (*right_y > 0) {
        result = limit_axis(right_y, s_status.front_mm);
        merge_result(result, &slowed, &blocked, &emergency);
    } else if (*right_y < 0) {
        result = limit_axis(right_y, s_status.rear_mm);
        merge_result(result, &slowed, &blocked, &emergency);
    }

    if (*right_x > 0) {
        result = limit_axis(right_x, s_status.right_mm);
        merge_result(result, &slowed, &blocked, &emergency);
    } else if (*right_x < 0) {
        result = limit_axis(right_x, s_status.left_mm);
        merge_result(result, &slowed, &blocked, &emergency);
    }

    if (*left_x > 0) {
        result = limit_axis(left_x, s_status.right_mm);
        merge_result(result, &slowed, &blocked, &emergency);
    } else if (*left_x < 0) {
        result = limit_axis(left_x, s_status.left_mm);
        merge_result(result, &slowed, &blocked, &emergency);
    }

    s_status.output_rx = *right_x;
    s_status.output_ry = *right_y;
    s_status.output_lx = *left_x;
    if (s_status.output_rx != s_status.input_rx ||
        s_status.output_ry != s_status.input_ry ||
        s_status.output_lx != s_status.input_lx) {
        s_status.modified_count++;
        s_status.last_limit_tick = now;
    }
    if (emergency) {
        s_status.state = OBSTACLE_STATE_EMERGENCY;
        s_status.emergency_count++;
    } else if (blocked) {
        s_status.state = OBSTACLE_STATE_BLOCKED;
        s_status.blocked_count++;
    } else if (slowed) {
        s_status.state = OBSTACLE_STATE_SLOWING;
    } else {
        s_status.state = OBSTACLE_STATE_CLEAR;
    }
}

const ObstacleAvoidanceStatus *ObstacleAvoidance_GetStatus(void)
{
    return &s_status;
}

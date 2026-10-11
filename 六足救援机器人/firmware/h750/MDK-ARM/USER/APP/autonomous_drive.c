#include "autonomous_drive.h"
#include "obstacle_avoidance.h"
#include "main.h"
#include <string.h>

static AutonomousDriveStatus s_status;

static uint8_t distance_blocked(uint16_t distance, uint16_t threshold)
{
    return distance != 0U && distance <= threshold;
}

static uint8_t distance_clear(uint16_t distance, uint16_t threshold)
{
    return distance == 0U || distance > threshold;
}

static uint16_t clearance_value(uint16_t distance)
{
    return distance == 0U ? OBSTACLE_MAX_VALID_MM : distance;
}

static void enter_state(AutonomousDriveState state, uint32_t now)
{
    if (s_status.state == state) {
        return;
    }
    s_status.state = state;
    s_status.state_enter_tick = now;
    s_status.transition_count++;
    if (state == AUTO_DRIVE_TURN_LEFT ||
        state == AUTO_DRIVE_TURN_RIGHT) {
        s_status.turn_count++;
        s_status.last_turn_left =
            state == AUTO_DRIVE_TURN_LEFT ? 1U : 0U;
    } else if (state == AUTO_DRIVE_REVERSE) {
        s_status.reverse_count++;
    } else if (state == AUTO_DRIVE_TRAPPED) {
        s_status.trapped_count++;
    }
}

static void choose_escape_direction(
    const ObstacleAvoidanceStatus *avoidance, uint32_t now)
{
    uint8_t left_blocked =
        distance_blocked(avoidance->left_mm, AUTO_SIDE_BLOCK_MM);
    uint8_t right_blocked =
        distance_blocked(avoidance->right_mm, AUTO_SIDE_BLOCK_MM);
    uint16_t left_clearance = clearance_value(avoidance->left_mm);
    uint16_t right_clearance = clearance_value(avoidance->right_mm);

    if (left_blocked && right_blocked) {
        if (distance_clear(avoidance->rear_mm, AUTO_REAR_BLOCK_MM)) {
            enter_state(AUTO_DRIVE_REVERSE, now);
        } else {
            enter_state(AUTO_DRIVE_TRAPPED, now);
        }
        return;
    }
    if (left_blocked) {
        enter_state(AUTO_DRIVE_TURN_RIGHT, now);
        return;
    }
    if (right_blocked) {
        enter_state(AUTO_DRIVE_TURN_LEFT, now);
        return;
    }

    if (left_clearance > right_clearance + 100U) {
        enter_state(AUTO_DRIVE_TURN_LEFT, now);
    } else if (right_clearance > left_clearance + 100U) {
        enter_state(AUTO_DRIVE_TURN_RIGHT, now);
    } else {
        enter_state(
            s_status.last_turn_left ?
            AUTO_DRIVE_TURN_RIGHT : AUTO_DRIVE_TURN_LEFT,
            now);
    }
}

void AutonomousDrive_Init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.initialized = 1U;
    s_status.state = AUTO_DRIVE_DISABLED;
}

void AutonomousDrive_SetEnabled(uint8_t enabled)
{
    uint32_t now = HAL_GetTick();

    if (!s_status.initialized) {
        AutonomousDrive_Init();
    }
    enabled = enabled ? 1U : 0U;
    if (enabled == s_status.enabled) {
        return;
    }
    s_status.enabled = enabled;
    s_status.command_rx = 0;
    s_status.command_ry = 0;
    s_status.command_lx = 0;
    if (enabled) {
        s_status.enable_count++;
        ObstacleAvoidance_SetEnabled(1U);
        enter_state(AUTO_DRIVE_WAIT_RADAR, now);
    } else {
        enter_state(AUTO_DRIVE_DISABLED, now);
    }
}

void AutonomousDrive_Toggle(void)
{
    AutonomousDrive_SetEnabled(s_status.enabled ? 0U : 1U);
}

void AutonomousDrive_Update(uint8_t manual_motion_active,
                            int8_t *right_x,
                            int8_t *right_y,
                            int8_t *left_x)
{
    const ObstacleAvoidanceStatus *avoidance;
    uint32_t now;
    uint32_t elapsed;

    if (right_x == NULL || right_y == NULL || left_x == NULL) {
        return;
    }
    if (!s_status.initialized) {
        AutonomousDrive_Init();
    }
    now = HAL_GetTick();
    s_status.last_update_tick = now;

    if (manual_motion_active && s_status.enabled) {
        s_status.manual_override_count++;
        AutonomousDrive_SetEnabled(0U);
        return;
    }
    if (!s_status.enabled) {
        return;
    }

    avoidance = ObstacleAvoidance_GetStatus();
    s_status.radar_ready = avoidance->radar_ready;
    if (!avoidance->radar_ready) {
        enter_state(AUTO_DRIVE_WAIT_RADAR, now);
    } else {
        elapsed = now - s_status.state_enter_tick;
        switch (s_status.state) {
        case AUTO_DRIVE_WAIT_RADAR:
            enter_state(AUTO_DRIVE_FORWARD, now);
            break;

        case AUTO_DRIVE_FORWARD:
            if (distance_blocked(
                    avoidance->front_mm, AUTO_EMERGENCY_MM) &&
                distance_clear(
                    avoidance->rear_mm, AUTO_REAR_BLOCK_MM)) {
                enter_state(AUTO_DRIVE_REVERSE, now);
            } else if (distance_blocked(
                           avoidance->front_mm,
                           AUTO_FRONT_TRIGGER_MM)) {
                choose_escape_direction(avoidance, now);
            }
            break;

        case AUTO_DRIVE_TURN_LEFT:
            if (distance_blocked(
                    avoidance->left_mm, AUTO_SIDE_BLOCK_MM)) {
                if (distance_clear(
                        avoidance->right_mm,
                        AUTO_SIDE_BLOCK_MM)) {
                    enter_state(AUTO_DRIVE_TURN_RIGHT, now);
                } else if (distance_clear(
                               avoidance->rear_mm,
                               AUTO_REAR_BLOCK_MM)) {
                    enter_state(AUTO_DRIVE_REVERSE, now);
                } else {
                    enter_state(AUTO_DRIVE_TRAPPED, now);
                }
            } else if (elapsed >= AUTO_TURN_MIN_MS &&
                       distance_clear(
                           avoidance->front_mm,
                           AUTO_FRONT_CLEAR_MM)) {
                enter_state(AUTO_DRIVE_FORWARD, now);
            } else if (elapsed >= AUTO_TURN_TIMEOUT_MS) {
                enter_state(AUTO_DRIVE_REVERSE, now);
            }
            break;

        case AUTO_DRIVE_TURN_RIGHT:
            if (distance_blocked(
                    avoidance->right_mm, AUTO_SIDE_BLOCK_MM)) {
                if (distance_clear(
                        avoidance->left_mm,
                        AUTO_SIDE_BLOCK_MM)) {
                    enter_state(AUTO_DRIVE_TURN_LEFT, now);
                } else if (distance_clear(
                               avoidance->rear_mm,
                               AUTO_REAR_BLOCK_MM)) {
                    enter_state(AUTO_DRIVE_REVERSE, now);
                } else {
                    enter_state(AUTO_DRIVE_TRAPPED, now);
                }
            } else if (elapsed >= AUTO_TURN_MIN_MS &&
                       distance_clear(
                           avoidance->front_mm,
                           AUTO_FRONT_CLEAR_MM)) {
                enter_state(AUTO_DRIVE_FORWARD, now);
            } else if (elapsed >= AUTO_TURN_TIMEOUT_MS) {
                enter_state(AUTO_DRIVE_REVERSE, now);
            }
            break;

        case AUTO_DRIVE_REVERSE:
            if (distance_blocked(
                    avoidance->rear_mm, AUTO_REAR_BLOCK_MM)) {
                choose_escape_direction(avoidance, now);
            } else if (elapsed >= AUTO_REVERSE_MS) {
                choose_escape_direction(avoidance, now);
            }
            break;

        case AUTO_DRIVE_TRAPPED:
            if (distance_clear(
                    avoidance->rear_mm, AUTO_REAR_BLOCK_MM)) {
                enter_state(AUTO_DRIVE_REVERSE, now);
            } else if (distance_clear(
                           avoidance->front_mm,
                           AUTO_FRONT_CLEAR_MM)) {
                enter_state(AUTO_DRIVE_FORWARD, now);
            } else if (distance_clear(
                           avoidance->left_mm,
                           AUTO_SIDE_BLOCK_MM) ||
                       distance_clear(
                           avoidance->right_mm,
                           AUTO_SIDE_BLOCK_MM)) {
                choose_escape_direction(avoidance, now);
            }
            break;

        case AUTO_DRIVE_DISABLED:
        default:
            enter_state(AUTO_DRIVE_WAIT_RADAR, now);
            break;
        }
    }

    s_status.command_rx = 0;
    s_status.command_ry = 0;
    s_status.command_lx = 0;
    switch (s_status.state) {
    case AUTO_DRIVE_FORWARD:
        s_status.command_ry = AUTO_FORWARD_COMMAND;
        break;
    case AUTO_DRIVE_TURN_LEFT:
        s_status.command_lx = -AUTO_TURN_COMMAND;
        break;
    case AUTO_DRIVE_TURN_RIGHT:
        s_status.command_lx = AUTO_TURN_COMMAND;
        break;
    case AUTO_DRIVE_REVERSE:
        s_status.command_ry = AUTO_REVERSE_COMMAND;
        break;
    default:
        break;
    }
    *right_x = s_status.command_rx;
    *right_y = s_status.command_ry;
    *left_x = s_status.command_lx;
}

const AutonomousDriveStatus *AutonomousDrive_GetStatus(void)
{
    return &s_status;
}

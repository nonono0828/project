#ifndef OBSTACLE_AVOIDANCE_H
#define OBSTACLE_AVOIDANCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * D6 raw angle aligned with the robot front. The vendor display increases
 * angles clockwise, so +90 degrees is the robot's right side.
 */
#define OBSTACLE_RADAR_FRONT_ANGLE_DEG 0U
#define OBSTACLE_RADAR_ANGLE_CLOCKWISE 1U

#define OBSTACLE_MIN_VALID_MM 120U
#define OBSTACLE_MAX_VALID_MM 12000U
#define OBSTACLE_EMERGENCY_MM 250U
#define OBSTACLE_STOP_MM 500U
#define OBSTACLE_SLOW_MM 800U
#define OBSTACLE_RADAR_STALE_MS 300U
#define OBSTACLE_POINT_STALE_MS 500U

typedef enum {
    OBSTACLE_STATE_DISABLED = 0,
    OBSTACLE_STATE_CLEAR,
    OBSTACLE_STATE_SLOWING,
    OBSTACLE_STATE_BLOCKED,
    OBSTACLE_STATE_EMERGENCY,
    OBSTACLE_STATE_RADAR_STALE
} ObstacleAvoidanceState;

typedef struct {
    uint8_t initialized;
    uint8_t enabled;
    uint8_t radar_ready;
    ObstacleAvoidanceState state;
    uint16_t front_mm;
    uint16_t right_mm;
    uint16_t rear_mm;
    uint16_t left_mm;
    int8_t input_rx;
    int8_t input_ry;
    int8_t input_lx;
    int8_t output_rx;
    int8_t output_ry;
    int8_t output_lx;
    uint32_t update_count;
    uint32_t modified_count;
    uint32_t blocked_count;
    uint32_t emergency_count;
    uint32_t radar_stale_count;
    uint32_t last_update_tick;
    uint32_t last_limit_tick;
} ObstacleAvoidanceStatus;

void ObstacleAvoidance_Init(uint8_t enabled);
void ObstacleAvoidance_SetEnabled(uint8_t enabled);
void ObstacleAvoidance_Toggle(void);
void ObstacleAvoidance_Apply(int8_t *right_x,
                             int8_t *right_y,
                             int8_t *left_x);
const ObstacleAvoidanceStatus *ObstacleAvoidance_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

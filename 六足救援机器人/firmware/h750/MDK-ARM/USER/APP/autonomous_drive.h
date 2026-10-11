#ifndef AUTONOMOUS_DRIVE_H
#define AUTONOMOUS_DRIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUTO_FORWARD_COMMAND 90
#define AUTO_TURN_COMMAND 85
#define AUTO_REVERSE_COMMAND (-70)

#define AUTO_FRONT_TRIGGER_MM 650U
#define AUTO_FRONT_CLEAR_MM 900U
#define AUTO_SIDE_BLOCK_MM 500U
#define AUTO_REAR_BLOCK_MM 500U
#define AUTO_EMERGENCY_MM 250U

#define AUTO_TURN_MIN_MS 350U
#define AUTO_TURN_TIMEOUT_MS 2500U
#define AUTO_REVERSE_MS 800U

typedef enum {
    AUTO_DRIVE_DISABLED = 0,
    AUTO_DRIVE_WAIT_RADAR,
    AUTO_DRIVE_FORWARD,
    AUTO_DRIVE_TURN_LEFT,
    AUTO_DRIVE_TURN_RIGHT,
    AUTO_DRIVE_REVERSE,
    AUTO_DRIVE_TRAPPED
} AutonomousDriveState;

typedef struct {
    uint8_t initialized;
    uint8_t enabled;
    uint8_t radar_ready;
    uint8_t last_turn_left;
    AutonomousDriveState state;
    int8_t command_rx;
    int8_t command_ry;
    int8_t command_lx;
    uint32_t state_enter_tick;
    uint32_t enable_count;
    uint32_t manual_override_count;
    uint32_t transition_count;
    uint32_t turn_count;
    uint32_t reverse_count;
    uint32_t trapped_count;
    uint32_t last_update_tick;
} AutonomousDriveStatus;

void AutonomousDrive_Init(void);
void AutonomousDrive_SetEnabled(uint8_t enabled);
void AutonomousDrive_Toggle(void);
void AutonomousDrive_Update(uint8_t manual_motion_active,
                            int8_t *right_x,
                            int8_t *right_y,
                            int8_t *left_x);
const AutonomousDriveStatus *AutonomousDrive_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef D6_RADAR_PORT_H
#define D6_RADAR_PORT_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef HAL_StatusTypeDef (*D6RadarWriteFn)(const uint8_t *data, uint16_t length);

typedef enum {
    D6_RADAR_STATE_UNINIT = 0,
    D6_RADAR_STATE_IDLE,
    D6_RADAR_STATE_STARTING,
    D6_RADAR_STATE_SCANNING,
    D6_RADAR_STATE_ERROR
} D6RadarState;

typedef struct {
    D6RadarState state;
    uint32_t byte_count;
    uint32_t frame_count;
    uint32_t data_frame_count;
    uint32_t sync_frame_count;
    uint32_t checksum_error_count;
    uint32_t length_error_count;
    uint32_t ack_timeout_count;
    uint32_t command_error_count;
    uint32_t last_frame_tick;
} D6RadarStatus;

void D6_Radar_Port_Init(D6RadarWriteFn write_fn);
HAL_StatusTypeDef D6_Radar_Port_Start(void);
HAL_StatusTypeDef D6_Radar_Port_Stop(void);
void D6_Radar_Port_Service(void);
void D6_Radar_Port_FeedByte(uint8_t byte);
void D6_Radar_Port_Feed(const uint8_t *data, uint16_t length);
uint16_t D6_Radar_Port_GetDistance(uint16_t angle);
uint16_t D6_Radar_Port_GetFreshDistance(uint16_t angle,
                                        uint32_t now,
                                        uint32_t max_age_ms);
uint16_t D6_Radar_Port_GetNearest(uint16_t *angle);
const D6RadarStatus *D6_Radar_Port_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

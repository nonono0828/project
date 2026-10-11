#ifndef GAIT_STATUS_H
#define GAIT_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t Hexapod_GetTerrainMode(void);
uint8_t Hexapod_GetSpeedMode(void);
uint8_t Hexapod_GetSpeedPercent(void);

#ifdef __cplusplus
}
#endif

#endif

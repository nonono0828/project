#ifndef GAS_ALARM_H
#define GAS_ALARM_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The connected MQ-2 module pulls DO low when its threshold is exceeded. */
#ifndef GAS_ALARM_ACTIVE_LEVEL
#define GAS_ALARM_ACTIVE_LEVEL GPIO_PIN_RESET
#endif

typedef struct {
    uint8_t initialized;
    uint8_t alarm;
    GPIO_PinState raw_level;
    uint32_t change_count;
    uint32_t last_change_tick;
} GasAlarmStatus;

void GasAlarm_Init(void);
void GasAlarm_Poll(void);
const GasAlarmStatus *GasAlarm_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

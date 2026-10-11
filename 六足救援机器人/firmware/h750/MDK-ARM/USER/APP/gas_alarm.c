#include "gas_alarm.h"
#include <string.h>

#define GAS_DEBOUNCE_MS 30U

static GasAlarmStatus s_gas;
static GPIO_PinState s_last_raw;
static uint32_t s_candidate_tick;

void GasAlarm_Init(void)
{
    memset(&s_gas, 0, sizeof(s_gas));
    s_last_raw = HAL_GPIO_ReadPin(GAS_DO_GPIO_Port, GAS_DO_Pin);
    s_gas.raw_level = s_last_raw;
    s_gas.alarm =
        s_last_raw == GAS_ALARM_ACTIVE_LEVEL ? 1U : 0U;
    s_gas.initialized = 1U;
    s_candidate_tick = HAL_GetTick();
}

void GasAlarm_Poll(void)
{
    GPIO_PinState raw =
        HAL_GPIO_ReadPin(GAS_DO_GPIO_Port, GAS_DO_Pin);
    uint32_t now = HAL_GetTick();

    s_gas.raw_level = raw;
    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_candidate_tick = now;
        return;
    }
    if ((now - s_candidate_tick) >= GAS_DEBOUNCE_MS) {
        uint8_t alarm =
            raw == GAS_ALARM_ACTIVE_LEVEL ? 1U : 0U;

        if (alarm != s_gas.alarm) {
            s_gas.alarm = alarm;
            s_gas.change_count++;
            s_gas.last_change_tick = now;
        }
    }
}

const GasAlarmStatus *GasAlarm_GetStatus(void)
{
    return &s_gas;
}

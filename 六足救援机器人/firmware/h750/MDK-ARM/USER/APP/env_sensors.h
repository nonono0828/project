#ifndef ENV_SENSORS_H
#define ENV_SENSORS_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AHT20_STATUS_NONE = 0,
    AHT20_STATUS_DIRECT_READ,
    AHT20_STATUS_COMMAND_71
} AHT20StatusMethod;

typedef struct {
    uint8_t aht20_present;
    uint8_t aht20_ok;
    uint8_t aht20_status_method;
    uint8_t bmp280_present;
    uint8_t bmp280_ok;
    uint8_t bmp280_addr;
    float humidity_rh;
    float aht_temperature_c;
    float bmp_temperature_c;
    float pressure_hpa;
    float altitude_m;
    uint32_t sample_count;
    uint32_t aht_sample_count;
    uint32_t bmp_sample_count;
    uint32_t error_count;
    uint32_t aht_crc_error_count;
    uint32_t init_attempt_count;
    uint32_t last_update_tick;
    uint32_t last_aht_update_tick;
    uint32_t last_bmp_update_tick;
} EnvSensorStatus;

void EnvSensors_Init(void);
void EnvSensors_Poll(void);
const EnvSensorStatus *EnvSensors_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

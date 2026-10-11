#include "env_sensors.h"
#include "i2c_bus.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <string.h>

#define AHT20_ADDR 0x38U
#define AHT20_CMD_STATUS 0x71U
#define AHT20_CMD_INIT 0xBEU
#define AHT20_CMD_SOFT_RESET 0xBAU
#define AHT20_CMD_MEASURE 0xACU
#define AHT20_STATUS_BUSY 0x80U
#define AHT20_STATUS_CALIBRATED 0x08U

#define BMP280_ADDR_PRIMARY 0x77U
#define BMP280_ADDR_ALT 0x76U
#define BMP280_CHIP_ID_REG 0xD0U
#define BMP280_CHIP_ID 0x58U
#define BMP280_RESET_REG 0xE0U
#define BMP280_RESET_VALUE 0xB6U
#define BMP280_CALIB_REG 0x88U
#define BMP280_CTRL_MEAS_REG 0xF4U
#define BMP280_CONFIG_REG 0xF5U
#define BMP280_PRESS_MSB_REG 0xF7U

#define BMP280_OSRS_T_X16 0x05U
#define BMP280_OSRS_P_X8 0x04U
#define BMP280_MODE_NORMAL 0x03U

#define ENV_REINIT_INTERVAL_MS 5000U
#define ENV_MAX_CONSECUTIVE_ERRORS 3U

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    int32_t t_fine;
} BMP280Calib;

static EnvSensorStatus s_env;
static BMP280Calib s_bmp_cal;
static uint8_t s_aht_consecutive_errors;
static uint8_t s_bmp_consecutive_errors;
static uint32_t s_last_init_attempt_tick;

static uint8_t aht20_crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0xFFU;
    uint8_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1) ^ 0x31U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint8_t aht20_read_status(uint8_t *status)
{
    uint8_t command = AHT20_CMD_STATUS;

    if (status == NULL) {
        return 0U;
    }
    if (I2C_Bus_Read(AHT20_ADDR, status, 1U, 20U) == HAL_OK) {
        s_env.aht20_status_method = AHT20_STATUS_DIRECT_READ;
        return 1U;
    }
    if (I2C_Bus_Write(AHT20_ADDR, &command, 1U, 20U) == HAL_OK &&
        I2C_Bus_Read(AHT20_ADDR, status, 1U, 20U) == HAL_OK) {
        s_env.aht20_status_method = AHT20_STATUS_COMMAND_71;
        return 1U;
    }
    return 0U;
}

static uint8_t aht20_initialize_once(void)
{
    static const uint8_t init_cmd[3] = {AHT20_CMD_INIT, 0x08U, 0x00U};
    uint8_t status = 0U;

    if (!aht20_read_status(&status)) {
        return 0U;
    }
    if ((status & AHT20_STATUS_CALIBRATED) != 0U) {
        return 1U;
    }
    if (I2C_Bus_Write(AHT20_ADDR, init_cmd, sizeof(init_cmd), 20U) != HAL_OK) {
        return 0U;
    }
    vTaskDelay(pdMS_TO_TICKS(20U));
    return aht20_read_status(&status) &&
           ((status & AHT20_STATUS_CALIBRATED) != 0U);
}

static uint8_t aht20_init(void)
{
    static const uint8_t init_cmd[3] = {AHT20_CMD_INIT, 0x08U, 0x00U};
    uint8_t reset = AHT20_CMD_SOFT_RESET;

    s_env.aht20_present =
        I2C_Bus_IsReady(AHT20_ADDR, 30U) == HAL_OK ? 1U : 0U;
    if (!s_env.aht20_present) {
        return 0U;
    }
    if (aht20_initialize_once()) {
        return 1U;
    }

    if (I2C_Bus_Write(AHT20_ADDR, &reset, 1U, 20U) != HAL_OK) {
        return 0U;
    }
    vTaskDelay(pdMS_TO_TICKS(200U));
    if (I2C_Bus_Write(AHT20_ADDR, init_cmd, sizeof(init_cmd), 20U) != HAL_OK) {
        return 0U;
    }
    vTaskDelay(pdMS_TO_TICKS(20U));
    return aht20_initialize_once();
}

static uint8_t aht20_read(float *temperature_c, float *humidity_rh)
{
    static const uint8_t measure_cmd[3] = {AHT20_CMD_MEASURE, 0x33U, 0x00U};
    uint8_t data[7];
    uint8_t status = 0U;
    uint8_t busy_wait;
    uint32_t humidity_raw;
    uint32_t temperature_raw;
    float temperature;
    float humidity;

    if (I2C_Bus_Write(AHT20_ADDR, measure_cmd, sizeof(measure_cmd), 20U) != HAL_OK) {
        return 0U;
    }
    vTaskDelay(pdMS_TO_TICKS(80U));
    for (busy_wait = 0U; busy_wait < 20U; busy_wait++) {
        if (!aht20_read_status(&status)) {
            return 0U;
        }
        if ((status & AHT20_STATUS_BUSY) == 0U) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    if ((status & AHT20_STATUS_BUSY) != 0U ||
        I2C_Bus_Read(AHT20_ADDR, data, sizeof(data), 20U) != HAL_OK) {
        return 0U;
    }
    if ((data[0] & AHT20_STATUS_BUSY) != 0U) {
        return 0U;
    }
    if (aht20_crc8(data, 6U) != data[6]) {
        s_env.aht_crc_error_count++;
        return 0U;
    }

    humidity_raw = ((uint32_t)data[1] << 12) |
                   ((uint32_t)data[2] << 4) |
                   ((uint32_t)data[3] >> 4);
    temperature_raw = (((uint32_t)data[3] & 0x0FU) << 16) |
                      ((uint32_t)data[4] << 8) |
                      data[5];
    humidity = ((float)humidity_raw * 100.0f) / 1048576.0f;
    temperature = ((float)temperature_raw * 200.0f) / 1048576.0f - 50.0f;
    if (humidity < 0.0f || humidity > 100.0f ||
        temperature < -40.0f || temperature > 85.0f) {
        return 0U;
    }
    *humidity_rh = humidity;
    *temperature_c = temperature;
    return 1U;
}

static uint16_t u16_le(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static int16_t s16_le(const uint8_t *data)
{
    return (int16_t)u16_le(data);
}

static uint8_t bmp280_probe(uint8_t address)
{
    uint8_t id = 0U;

    if (I2C_Bus_MemRead8(address, BMP280_CHIP_ID_REG, &id, 1U, 20U) != HAL_OK) {
        return 0U;
    }
    return id == BMP280_CHIP_ID;
}

static uint8_t bmp280_init(void)
{
    uint8_t calibration[24];
    uint8_t reset = BMP280_RESET_VALUE;
    uint8_t ctrl;
    uint8_t config;

    if (bmp280_probe(BMP280_ADDR_PRIMARY)) {
        s_env.bmp280_addr = BMP280_ADDR_PRIMARY;
    } else if (bmp280_probe(BMP280_ADDR_ALT)) {
        s_env.bmp280_addr = BMP280_ADDR_ALT;
    } else {
        s_env.bmp280_addr = 0U;
        s_env.bmp280_present = 0U;
        return 0U;
    }
    s_env.bmp280_present = 1U;

    if (I2C_Bus_MemWrite8(s_env.bmp280_addr, BMP280_RESET_REG, &reset, 1U, 20U) != HAL_OK) {
        return 0U;
    }
    vTaskDelay(pdMS_TO_TICKS(5U));
    if (I2C_Bus_MemRead8(s_env.bmp280_addr, BMP280_CALIB_REG,
                         calibration, sizeof(calibration), 50U) != HAL_OK) {
        return 0U;
    }

    s_bmp_cal.dig_T1 = u16_le(&calibration[0]);
    s_bmp_cal.dig_T2 = s16_le(&calibration[2]);
    s_bmp_cal.dig_T3 = s16_le(&calibration[4]);
    s_bmp_cal.dig_P1 = u16_le(&calibration[6]);
    s_bmp_cal.dig_P2 = s16_le(&calibration[8]);
    s_bmp_cal.dig_P3 = s16_le(&calibration[10]);
    s_bmp_cal.dig_P4 = s16_le(&calibration[12]);
    s_bmp_cal.dig_P5 = s16_le(&calibration[14]);
    s_bmp_cal.dig_P6 = s16_le(&calibration[16]);
    s_bmp_cal.dig_P7 = s16_le(&calibration[18]);
    s_bmp_cal.dig_P8 = s16_le(&calibration[20]);
    s_bmp_cal.dig_P9 = s16_le(&calibration[22]);
    if (s_bmp_cal.dig_P1 == 0U) {
        return 0U;
    }

    config = (uint8_t)(5U << 5);
    ctrl = (uint8_t)((BMP280_OSRS_T_X16 << 5) |
                     (BMP280_OSRS_P_X8 << 2) |
                     BMP280_MODE_NORMAL);
    if (I2C_Bus_MemWrite8(s_env.bmp280_addr, BMP280_CONFIG_REG,
                          &config, 1U, 20U) != HAL_OK ||
        I2C_Bus_MemWrite8(s_env.bmp280_addr, BMP280_CTRL_MEAS_REG,
                          &ctrl, 1U, 20U) != HAL_OK) {
        return 0U;
    }
    return 1U;
}

static int32_t bmp280_compensate_temperature(int32_t adc_temperature)
{
    int32_t var1;
    int32_t var2;

    var1 = ((((adc_temperature >> 3) -
             ((int32_t)s_bmp_cal.dig_T1 << 1))) *
            (int32_t)s_bmp_cal.dig_T2) >> 11;
    var2 = (((((adc_temperature >> 4) - (int32_t)s_bmp_cal.dig_T1) *
              ((adc_temperature >> 4) - (int32_t)s_bmp_cal.dig_T1)) >> 12) *
            (int32_t)s_bmp_cal.dig_T3) >> 14;
    s_bmp_cal.t_fine = var1 + var2;
    return (s_bmp_cal.t_fine * 5 + 128) >> 8;
}

static uint32_t bmp280_compensate_pressure(int32_t adc_pressure)
{
    int64_t var1;
    int64_t var2;
    int64_t pressure;

    var1 = (int64_t)s_bmp_cal.t_fine - 128000;
    var2 = var1 * var1 * (int64_t)s_bmp_cal.dig_P6;
    var2 += (var1 * (int64_t)s_bmp_cal.dig_P5) << 17;
    var2 += (int64_t)s_bmp_cal.dig_P4 << 35;
    var1 = ((var1 * var1 * (int64_t)s_bmp_cal.dig_P3) >> 8) +
           ((var1 * (int64_t)s_bmp_cal.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) *
            (int64_t)s_bmp_cal.dig_P1) >> 33;
    if (var1 == 0) {
        return 0U;
    }

    pressure = 1048576 - adc_pressure;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)s_bmp_cal.dig_P9 *
            (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = ((int64_t)s_bmp_cal.dig_P8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) +
               ((int64_t)s_bmp_cal.dig_P7 << 4);
    return (uint32_t)pressure;
}

static uint8_t bmp280_read(float *temperature_c, float *pressure_hpa,
                           float *altitude_m)
{
    uint8_t data[6];
    int32_t raw_pressure;
    int32_t raw_temperature;
    uint32_t pressure_q24_8;
    float temperature;
    float pressure;

    if (s_env.bmp280_addr == 0U ||
        I2C_Bus_MemRead8(s_env.bmp280_addr, BMP280_PRESS_MSB_REG,
                         data, sizeof(data), 30U) != HAL_OK) {
        return 0U;
    }
    raw_pressure = (int32_t)(((uint32_t)data[0] << 12) |
                            ((uint32_t)data[1] << 4) |
                            ((uint32_t)data[2] >> 4));
    raw_temperature = (int32_t)(((uint32_t)data[3] << 12) |
                               ((uint32_t)data[4] << 4) |
                               ((uint32_t)data[5] >> 4));
    temperature = (float)bmp280_compensate_temperature(raw_temperature) / 100.0f;
    pressure_q24_8 = bmp280_compensate_pressure(raw_pressure);
    pressure = (float)pressure_q24_8 / 25600.0f;
    if (temperature < -40.0f || temperature > 85.0f ||
        pressure < 300.0f || pressure > 1100.0f) {
        return 0U;
    }

    *temperature_c = temperature;
    *pressure_hpa = pressure;
    *altitude_m = (powf(1013.25f / pressure, 0.1902630958f) - 1.0f) *
                  298.15f / 0.0065f;
    return 1U;
}

static void env_try_initialize(void)
{
    s_env.init_attempt_count++;
    s_last_init_attempt_tick = HAL_GetTick();
    if (!s_env.aht20_ok) {
        s_env.aht20_ok = aht20_init();
        s_aht_consecutive_errors = 0U;
    }
    if (!s_env.bmp280_ok) {
        s_env.bmp280_ok = bmp280_init();
        s_bmp_consecutive_errors = 0U;
    }
}

void EnvSensors_Init(void)
{
    memset(&s_env, 0, sizeof(s_env));
    memset(&s_bmp_cal, 0, sizeof(s_bmp_cal));
    s_aht_consecutive_errors = 0U;
    s_bmp_consecutive_errors = 0U;
    s_last_init_attempt_tick = 0U;
    env_try_initialize();
}

void EnvSensors_Poll(void)
{
    uint8_t sample_updated = 0U;
    float temperature;
    float humidity;
    float pressure;
    float altitude;
    uint32_t now = HAL_GetTick();

    if ((!s_env.aht20_ok || !s_env.bmp280_ok) &&
        (now - s_last_init_attempt_tick) >= ENV_REINIT_INTERVAL_MS) {
        env_try_initialize();
    }

    if (s_env.aht20_ok) {
        if (aht20_read(&temperature, &humidity)) {
            s_env.aht_temperature_c = temperature;
            s_env.humidity_rh = humidity;
            s_env.aht_sample_count++;
            s_env.last_aht_update_tick = now;
            s_aht_consecutive_errors = 0U;
            sample_updated = 1U;
        } else {
            s_env.error_count++;
            if (++s_aht_consecutive_errors >= ENV_MAX_CONSECUTIVE_ERRORS) {
                s_env.aht20_ok = 0U;
            }
        }
    }

    if (s_env.bmp280_ok) {
        if (bmp280_read(&temperature, &pressure, &altitude)) {
            s_env.bmp_temperature_c = temperature;
            s_env.pressure_hpa = pressure;
            s_env.altitude_m = altitude;
            s_env.bmp_sample_count++;
            s_env.last_bmp_update_tick = now;
            s_bmp_consecutive_errors = 0U;
            sample_updated = 1U;
        } else {
            s_env.error_count++;
            if (++s_bmp_consecutive_errors >= ENV_MAX_CONSECUTIVE_ERRORS) {
                s_env.bmp280_ok = 0U;
            }
        }
    }

    if (sample_updated) {
        s_env.sample_count++;
        s_env.last_update_tick = now;
    }
}

const EnvSensorStatus *EnvSensors_GetStatus(void)
{
    return &s_env;
}

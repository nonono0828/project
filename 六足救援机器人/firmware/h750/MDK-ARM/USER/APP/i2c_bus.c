#include "i2c_bus.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <string.h>

static SemaphoreHandle_t s_i2c_mutex;
static StaticSemaphore_t s_i2c_mutex_storage;
static I2CBusStatus s_bus_status;

static uint32_t normalize_timeout(uint32_t timeout_ms)
{
    return timeout_ms == 0U ? I2C_BUS_DEFAULT_TIMEOUT_MS : timeout_ms;
}

static void sample_bus_lines(void)
{
    s_bus_status.scl_level = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8);
    s_bus_status.sda_level = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9);
}

static void record_result(HAL_StatusTypeDef result)
{
    s_bus_status.last_status = result;
    s_bus_status.last_hal_error = HAL_I2C_GetError(&hi2c1);
    s_bus_status.transaction_count++;
    if (result != HAL_OK) {
        s_bus_status.error_count++;
    }
    sample_bus_lines();
}

void I2C_Bus_Init(void)
{
    if (s_i2c_mutex == NULL) {
        memset(&s_bus_status, 0, sizeof(s_bus_status));
        s_i2c_mutex = xSemaphoreCreateMutexStatic(&s_i2c_mutex_storage);
        sample_bus_lines();
    }
}

static uint8_t i2c_lock(uint32_t timeout_ms)
{
    I2C_Bus_Init();
    if (s_i2c_mutex == NULL || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return 1U;
    }
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(normalize_timeout(timeout_ms))) == pdTRUE) {
        return 1U;
    }
    s_bus_status.lock_timeout_count++;
    s_bus_status.last_status = HAL_BUSY;
    sample_bus_lines();
    return 0U;
}

static void i2c_unlock(void)
{
    if (s_i2c_mutex != NULL && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xSemaphoreGive(s_i2c_mutex);
    }
}

HAL_StatusTypeDef I2C_Bus_IsReady(uint8_t addr7, uint32_t timeout_ms)
{
    HAL_StatusTypeDef result;

    if (!i2c_lock(timeout_ms)) {
        return HAL_BUSY;
    }
    result = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr7 << 1), 2U,
                                   normalize_timeout(timeout_ms));
    record_result(result);
    i2c_unlock();
    return result;
}

HAL_StatusTypeDef I2C_Bus_Write(uint8_t addr7, const uint8_t *data, uint16_t len,
                               uint32_t timeout_ms)
{
    HAL_StatusTypeDef result;

    if (data == NULL || len == 0U) {
        return HAL_ERROR;
    }
    if (!i2c_lock(timeout_ms)) {
        return HAL_BUSY;
    }
    result = HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(addr7 << 1), (uint8_t *)data,
                                     len, normalize_timeout(timeout_ms));
    record_result(result);
    i2c_unlock();
    return result;
}

HAL_StatusTypeDef I2C_Bus_Read(uint8_t addr7, uint8_t *data, uint16_t len,
                              uint32_t timeout_ms)
{
    HAL_StatusTypeDef result;

    if (data == NULL || len == 0U) {
        return HAL_ERROR;
    }
    if (!i2c_lock(timeout_ms)) {
        return HAL_BUSY;
    }
    result = HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(addr7 << 1), data, len,
                                    normalize_timeout(timeout_ms));
    record_result(result);
    i2c_unlock();
    return result;
}

HAL_StatusTypeDef I2C_Bus_MemWrite8(uint8_t addr7, uint8_t reg, const uint8_t *data,
                                   uint16_t len, uint32_t timeout_ms)
{
    HAL_StatusTypeDef result;

    if (data == NULL || len == 0U) {
        return HAL_ERROR;
    }
    if (!i2c_lock(timeout_ms)) {
        return HAL_BUSY;
    }
    result = HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(addr7 << 1), reg,
                               I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, len,
                               normalize_timeout(timeout_ms));
    record_result(result);
    i2c_unlock();
    return result;
}

HAL_StatusTypeDef I2C_Bus_MemRead8(uint8_t addr7, uint8_t reg, uint8_t *data,
                                  uint16_t len, uint32_t timeout_ms)
{
    HAL_StatusTypeDef result;

    if (data == NULL || len == 0U) {
        return HAL_ERROR;
    }
    if (!i2c_lock(timeout_ms)) {
        return HAL_BUSY;
    }
    result = HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(addr7 << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, data, len,
                              normalize_timeout(timeout_ms));
    record_result(result);
    i2c_unlock();
    return result;
}

uint8_t I2C_Bus_Scan(uint8_t *addr_list, uint8_t max_count)
{
    uint8_t addr;
    uint8_t count = 0U;

    if (addr_list == NULL || max_count == 0U || !i2c_lock(250U)) {
        return 0U;
    }
    for (addr = 1U; addr < 0x7FU && count < max_count; addr++) {
        HAL_StatusTypeDef result = HAL_I2C_IsDeviceReady(
            &hi2c1, (uint16_t)(addr << 1), 1U, 5U);
        if (result == HAL_OK) {
            addr_list[count++] = addr;
        }
    }
    record_result(HAL_OK);
    i2c_unlock();
    return count;
}

const I2CBusStatus *I2C_Bus_GetStatus(void)
{
    sample_bus_lines();
    return &s_bus_status;
}

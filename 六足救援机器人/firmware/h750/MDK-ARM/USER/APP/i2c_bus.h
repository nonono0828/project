#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_BUS_DEFAULT_TIMEOUT_MS 50U

typedef struct {
    HAL_StatusTypeDef last_status;
    uint32_t last_hal_error;
    uint32_t transaction_count;
    uint32_t error_count;
    uint32_t lock_timeout_count;
    GPIO_PinState scl_level;
    GPIO_PinState sda_level;
} I2CBusStatus;

void I2C_Bus_Init(void);
HAL_StatusTypeDef I2C_Bus_IsReady(uint8_t addr7, uint32_t timeout_ms);
HAL_StatusTypeDef I2C_Bus_Write(uint8_t addr7, const uint8_t *data, uint16_t len, uint32_t timeout_ms);
HAL_StatusTypeDef I2C_Bus_Read(uint8_t addr7, uint8_t *data, uint16_t len, uint32_t timeout_ms);
HAL_StatusTypeDef I2C_Bus_MemWrite8(uint8_t addr7, uint8_t reg, const uint8_t *data,
                                   uint16_t len, uint32_t timeout_ms);
HAL_StatusTypeDef I2C_Bus_MemRead8(uint8_t addr7, uint8_t reg, uint8_t *data,
                                  uint16_t len, uint32_t timeout_ms);
uint8_t I2C_Bus_Scan(uint8_t *addr_list, uint8_t max_count);
const I2CBusStatus *I2C_Bus_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

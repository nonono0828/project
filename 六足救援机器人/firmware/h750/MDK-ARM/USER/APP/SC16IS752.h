#ifndef SC16IS752_H
#define SC16IS752_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC16IS752_DEFAULT_ADDR7 0x4DU
#define SC16IS752_DEFAULT_XTAL_HZ 1843200UL
#define SC16IS752_MAX_BAUD (SC16IS752_DEFAULT_XTAL_HZ / 16UL)

typedef enum {
    SC16IS752_CH_A = 0,
    SC16IS752_CH_B = 1
} SC16IS752_Channel;

typedef struct {
    uint8_t present;
    uint8_t scratch_a_ok;
    uint8_t scratch_b_ok;
    uint8_t loopback_a_ok;
    uint8_t loopback_b_ok;
    uint8_t last_scratch_a;
    uint8_t last_scratch_b;
    uint32_t transport_error_count;
    uint32_t health_check_count;
    uint32_t health_error_count;
    uint32_t health_retry_recovered_count;
    uint32_t last_health_tick;
    uint32_t rx_overrun_count;
    uint32_t line_error_count;
    uint32_t tx_error_count;
    uint32_t rx_bytes[2];
    uint32_t tx_bytes[2];
    uint32_t last_rx_tick[2];
} SC16IS752_Status;

HAL_StatusTypeDef SC16IS752_Init(uint8_t addr7, uint32_t xtal_hz);
HAL_StatusTypeDef SC16IS752_ConfigureUart(SC16IS752_Channel channel, uint32_t baud);
HAL_StatusTypeDef SC16IS752_ClearFifos(SC16IS752_Channel channel);
HAL_StatusTypeDef SC16IS752_LoopbackTest(SC16IS752_Channel channel);
HAL_StatusTypeDef SC16IS752_HealthCheck(void);
uint8_t SC16IS752_RxLevel(SC16IS752_Channel channel);
uint16_t SC16IS752_ReadAvailable(SC16IS752_Channel channel, uint8_t *buffer,
                                uint16_t max_length);
HAL_StatusTypeDef SC16IS752_Write(SC16IS752_Channel channel, const uint8_t *buffer,
                                 uint16_t length, uint32_t timeout_ms);
uint8_t SC16IS752_IrqAsserted(void);
const SC16IS752_Status *SC16IS752_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

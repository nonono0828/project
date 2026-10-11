#ifndef PERIPHERAL_TASK_H
#define PERIPHERAL_TASK_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t initialization_complete;
    uint8_t scan_count;
    uint8_t scan_addresses[8];
    uint8_t sc16_online;
    uint8_t radar_uart_online;
    uint32_t sc16_init_attempts;
    uint32_t sc16_reconnects;
    uint32_t sc16_health_failures;
    uint32_t last_sc16_init_tick;
    uint32_t spi3_recovery_attempts;
    uint32_t spi3_recovery_successes;
    uint8_t cloud_tx_failure_streak;
    uint32_t radar_uart_rx_bytes;
    uint32_t radar_uart_tx_bytes;
    uint32_t radar_uart_tx_error_count;
    uint32_t radar_uart_error_count;
    uint32_t radar_uart_drop_count;
    uint32_t radar_start_attempts;
    uint32_t radar_stop_attempts;
} PeripheralTaskStatus;

void Peripheral_Task(void const *argument);
void Peripheral_Task_NotifyFromISR(void);
const PeripheralTaskStatus *Peripheral_Task_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

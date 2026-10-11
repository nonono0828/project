#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include "main.h"
#include <stdint.h>

#define HAL_DEBUG_UART_Init MX_UART8_Init
#define DEBUG_UART UART8
#define DEBUG_UART_h huart8

#define APP_PRINT(...) Debug_UART_Printf(__VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

void Debug_UART_Init(void);
void Debug_UART_QueueInit(void);
void Debug_UART_Printf(const char *format, ...);
void Debug_UART_Write(const uint8_t *data, uint16_t length);
void Debug_UART_Process(uint32_t wait_ms);
uint32_t Debug_UART_GetDroppedCount(void);

/* Kept for compatibility with the existing global HAL callbacks. */
void Debug_UART_print(uint8_t *str, int bytes);
void Debug_UART_Callback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif

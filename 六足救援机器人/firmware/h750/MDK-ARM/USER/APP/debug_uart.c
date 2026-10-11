#include "debug_uart.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_MESSAGE_MAX 192U
#define DEBUG_QUEUE_DEPTH 16U

typedef struct {
    uint16_t length;
    uint8_t data[DEBUG_MESSAGE_MAX];
} DebugMessage;

static QueueHandle_t s_debug_queue;
static StaticQueue_t s_debug_queue_control;
static uint8_t s_debug_queue_storage[DEBUG_QUEUE_DEPTH * sizeof(DebugMessage)];
static uint32_t s_dropped_messages;

void Debug_UART_Init(void)
{
    HAL_DEBUG_UART_Init();
}

void Debug_UART_QueueInit(void)
{
    if (s_debug_queue == NULL) {
        s_debug_queue = xQueueCreateStatic(DEBUG_QUEUE_DEPTH,
                                           sizeof(DebugMessage),
                                           s_debug_queue_storage,
                                           &s_debug_queue_control);
    }
}

void Debug_UART_Write(const uint8_t *data, uint16_t length)
{
    DebugMessage message;

    if (data == NULL || length == 0U) {
        return;
    }
    if (length > DEBUG_MESSAGE_MAX) {
        length = DEBUG_MESSAGE_MAX;
    }

    if (s_debug_queue == NULL ||
        xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        (void)HAL_UART_Transmit(&DEBUG_UART_h, (uint8_t *)data, length, 100U);
        return;
    }

    message.length = length;
    memcpy(message.data, data, length);
    if (s_debug_queue == NULL ||
        xQueueSendToBack(s_debug_queue, &message, 0U) != pdPASS) {
        s_dropped_messages++;
    }
}

void Debug_UART_Printf(const char *format, ...)
{
    char buffer[DEBUG_MESSAGE_MAX];
    int length;
    va_list args;

    if (format == NULL) {
        return;
    }
    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length <= 0) {
        return;
    }
    if (length >= (int)sizeof(buffer)) {
        length = (int)sizeof(buffer) - 1;
    }
    Debug_UART_Write((const uint8_t *)buffer, (uint16_t)length);
}

void Debug_UART_Process(uint32_t wait_ms)
{
    DebugMessage message;

    if (s_debug_queue == NULL) {
        return;
    }
    if (xQueueReceive(s_debug_queue, &message, pdMS_TO_TICKS(wait_ms)) == pdPASS) {
        (void)HAL_UART_Transmit(&DEBUG_UART_h, message.data, message.length, 100U);
    }
}

uint32_t Debug_UART_GetDroppedCount(void)
{
    return s_dropped_messages;
}

void Debug_UART_print(uint8_t *str, int bytes)
{
    if (bytes > 0) {
        Debug_UART_Write(str, (uint16_t)bytes);
    }
}

void Debug_UART_Callback(UART_HandleTypeDef *huart)
{
    (void)huart;
}

#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
    uint8_t value = (uint8_t)ch;
    Debug_UART_Write(&value, 1U);
    return ch;
}

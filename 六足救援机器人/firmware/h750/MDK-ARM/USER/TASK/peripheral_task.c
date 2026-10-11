#include "peripheral_task.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "i2c_bus.h"
#include "env_sensors.h"
#include "sc16is752.h"
#include "gps_nmea.h"
#include "d6_radar_port.h"
#include "cloud_link.h"
#include "debug_uart.h"
#include "spi.h"
#include "usart.h"
#include <string.h>

#define GPS_CHANNEL SC16IS752_CH_A
#define CLOUD_CHANNEL SC16IS752_CH_B
#define GPS_BAUD 38400U
#define CLOUD_BAUD 115200U
#define RADAR_BAUD 230400U
#define FIFO_BUFFER_SIZE 64U
#define RADAR_UART_RX_SIZE 64U
#define RADAR_UART_RING_SIZE 4096U
#define RADAR_UART_RING_MASK (RADAR_UART_RING_SIZE - 1U)
#define FALLBACK_POLL_MS 2U
#define SC16_RETRY_MIN_MS 250U
#define SC16_RETRY_MAX_MS 8000U
#define SC16_SERVICE_ERROR_LIMIT 5U
#define SC16_HEALTH_PERIOD_MS 1000U
#define SC16_TX_FAILURE_LIMIT 5U
#define RADAR_RETRY_INTERVAL_MS 2000U
#define RADAR_STOP_SETTLE_MS 120U
#define CLOUD_TX_CHUNK_SIZE 16U
#define CLOUD_TX_STEP_MS 2U
#define CLOUD_TX_TIMEOUT_MS 3U

static PeripheralTaskStatus s_task_status;
static TaskHandle_t s_task_handle;
static uint8_t s_gps_buffer[FIFO_BUFFER_SIZE];
static uint8_t s_cloud_buffer[FIFO_BUFFER_SIZE];
static uint8_t s_radar_uart_rx[RADAR_UART_RX_SIZE];
static uint8_t s_radar_uart_ring[RADAR_UART_RING_SIZE];
static volatile uint16_t s_radar_uart_head;
static volatile uint16_t s_radar_uart_tail;
static volatile uint8_t s_radar_uart_rx_armed;
static uint32_t s_next_cloud_tx_tick;
static uint8_t s_cloud_tx_failure_streak;

static uint8_t scan_contains(uint8_t address)
{
    uint8_t index;

    for (index = 0U; index < s_task_status.scan_count; index++) {
        if (s_task_status.scan_addresses[index] == address) {
            return 1U;
        }
    }
    return 0U;
}

static void print_missing_address(uint8_t address, const char *name)
{
    const I2CBusStatus *bus;
    HAL_StatusTypeDef result;

    if (scan_contains(address)) {
        return;
    }
    result = I2C_Bus_IsReady(address, 20U);
    bus = I2C_Bus_GetStatus();
    Debug_UART_Printf(
        "[I2C] missing %s addr=0x%02X hal=%u err=0x%08lX "
        "scl=%u sda=%u pwr_pa8=%u\r\n",
        name, address, (unsigned int)result,
        (unsigned long)bus->last_hal_error,
        (unsigned int)bus->scl_level,
        (unsigned int)bus->sda_level,
        (unsigned int)HAL_GPIO_ReadPin(
            ENV_SENSOR_PWR_GPIO_Port, ENV_SENSOR_PWR_Pin));
}

static HAL_StatusTypeDef radar_write(const uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef result;

    if (!s_task_status.radar_uart_online) {
        return HAL_ERROR;
    }
    result = HAL_UART_Transmit(&huart7, (uint8_t *)data, length, 50U);
    if (result == HAL_OK) {
        s_task_status.radar_uart_tx_bytes += length;
    } else {
        s_task_status.radar_uart_tx_error_count++;
    }
    return result;
}

static HAL_StatusTypeDef arm_radar_uart_rx(void)
{
    HAL_StatusTypeDef result = HAL_UARTEx_ReceiveToIdle_IT(
        &huart7, s_radar_uart_rx, sizeof(s_radar_uart_rx));

    s_radar_uart_rx_armed = result == HAL_OK ? 1U : 0U;
    return result;
}

static HAL_StatusTypeDef initialize_radar_uart(void)
{
    HAL_StatusTypeDef result;

    MX_UART7_Init();
    s_radar_uart_head = 0U;
    s_radar_uart_tail = 0U;
    result = arm_radar_uart_rx();
    if (result == HAL_OK) {
        s_task_status.radar_uart_online = 1U;
        Debug_UART_Printf(
            "[D6] transport=UART7 rx=PB3 tx=PB4 baud=%lu\r\n",
            (unsigned long)RADAR_BAUD);
    } else {
        s_task_status.radar_uart_error_count++;
        Debug_UART_Printf("[D6] UART7 receive start failed hal=%u\r\n",
                          (unsigned int)result);
    }
    return result;
}

static void drain_radar_uart(void)
{
    while (s_radar_uart_tail != s_radar_uart_head) {
        uint8_t byte = s_radar_uart_ring[s_radar_uart_tail];

        s_radar_uart_tail =
            (uint16_t)((s_radar_uart_tail + 1U) & RADAR_UART_RING_MASK);
        D6_Radar_Port_FeedByte(byte);
    }

    if (s_task_status.radar_uart_online && !s_radar_uart_rx_armed) {
        (void)HAL_UART_AbortReceive(&huart7);
        __HAL_UART_CLEAR_OREFLAG(&huart7);
        if (arm_radar_uart_rx() != HAL_OK) {
            s_task_status.radar_uart_error_count++;
        }
    }
}

static void print_i2c_scan(void)
{
    const I2CBusStatus *bus;
    uint8_t index;

    s_task_status.scan_count = I2C_Bus_Scan(
        s_task_status.scan_addresses,
        sizeof(s_task_status.scan_addresses));
    bus = I2C_Bus_GetStatus();
    Debug_UART_Printf("[I2C] scan count=%u scl=%u sda=%u hal=%u err=0x%08lX\r\n",
                      s_task_status.scan_count,
                      (unsigned int)bus->scl_level,
                      (unsigned int)bus->sda_level,
                      (unsigned int)bus->last_status,
                      (unsigned long)bus->last_hal_error);
    for (index = 0U; index < s_task_status.scan_count; index++) {
        Debug_UART_Printf("[I2C] found 0x%02X\r\n",
                          s_task_status.scan_addresses[index]);
    }
    print_missing_address(0x38U, "AHT20");
    if (!scan_contains(0x76U) && !scan_contains(0x77U)) {
        print_missing_address(0x76U, "BMP280");
        print_missing_address(0x77U, "BMP280");
    }
}

static HAL_StatusTypeDef initialize_sc16_links(void)
{
    HAL_StatusTypeDef result;
    const SC16IS752_Status *status;

    s_task_status.sc16_init_attempts++;
    s_task_status.last_sc16_init_tick = HAL_GetTick();
    s_task_status.spi3_recovery_attempts++;
    result = SPI3_TryRecover();
    if (result != HAL_OK) {
        Debug_UART_Printf("[SC16] SPI3 recovery failed hal=%u\r\n",
                          (unsigned int)result);
        return result;
    }
    s_task_status.spi3_recovery_successes++;
    result = SC16IS752_Init(SC16IS752_DEFAULT_ADDR7,
                            SC16IS752_DEFAULT_XTAL_HZ);
    if (result != HAL_OK) {
        Debug_UART_Printf("[SC16] SPI3 probe failed hal=%u\r\n",
                          (unsigned int)result);
        return result;
    }

    result = SC16IS752_ConfigureUart(GPS_CHANNEL, GPS_BAUD);
    if (result == HAL_OK) {
        result = SC16IS752_LoopbackTest(GPS_CHANNEL);
    }
    if (result == HAL_OK) {
        result = SC16IS752_ConfigureUart(CLOUD_CHANNEL, CLOUD_BAUD);
    }
    if (result == HAL_OK) {
        result = SC16IS752_LoopbackTest(CLOUD_CHANNEL);
    }
    status = SC16IS752_GetStatus();
    Debug_UART_Printf(
        "[SC16] sprA=%u sprB=%u loopA=%u loopB=%u hal=%u\r\n",
        status->scratch_a_ok, status->scratch_b_ok,
        status->loopback_a_ok, status->loopback_b_ok,
        (unsigned int)result);
    if (result != HAL_OK) {
        return result;
    }

    result = SC16IS752_ConfigureUart(GPS_CHANNEL, GPS_BAUD);
    if (result != HAL_OK) {
        return result;
    }
    result = SC16IS752_ClearFifos(GPS_CHANNEL);
    if (result == HAL_OK) {
        result = SC16IS752_ConfigureUart(CLOUD_CHANNEL, CLOUD_BAUD);
    }
    if (result == HAL_OK) {
        result = SC16IS752_ClearFifos(CLOUD_CHANNEL);
    }
    if (result == HAL_OK) {
        s_task_status.sc16_online = 1U;
        CloudLink_SetTransportOnline(1U);
        Debug_UART_Printf(
            "[SC16] online transport=SPI3 xtal=1843200 "
            "gps=A/38400 cloud=B/115200\r\n");
    }
    return result;
}

static void drain_channel(SC16IS752_Channel channel,
                          uint8_t *buffer, uint8_t feed_gps)
{
    uint8_t pass;

    for (pass = 0U; pass < 4U; pass++) {
        uint16_t length = SC16IS752_ReadAvailable(
            channel, buffer, FIFO_BUFFER_SIZE);
        if (length == 0U) {
            break;
        }
        if (feed_gps) {
            GPS_NMEA_Feed(buffer, length);
        } else {
            CloudLink_Feed(buffer, length);
        }
    }
}

static uint8_t service_cloud_tx(void)
{
    const uint8_t *data;
    uint16_t length;
    uint32_t now = HAL_GetTick();

    if (!s_task_status.sc16_online ||
        (int32_t)(now - s_next_cloud_tx_tick) < 0) {
        return 0U;
    }
    length = CloudLink_TxPeek(&data, CLOUD_TX_CHUNK_SIZE);
    if (length == 0U) {
        return 0U;
    }

    s_next_cloud_tx_tick = now + CLOUD_TX_STEP_MS;
    if (SC16IS752_Write(CLOUD_CHANNEL, data, length,
                        CLOUD_TX_TIMEOUT_MS) == HAL_OK) {
        CloudLink_TxCommit(length);
        s_cloud_tx_failure_streak = 0U;
        s_task_status.cloud_tx_failure_streak = 0U;
    } else {
        CloudLink_TxFailed();
        if (s_cloud_tx_failure_streak < UINT8_MAX) {
            s_cloud_tx_failure_streak++;
        }
        s_task_status.cloud_tx_failure_streak =
            s_cloud_tx_failure_streak;
    }
    return s_cloud_tx_failure_streak >= SC16_TX_FAILURE_LIMIT ? 1U : 0U;
}

static void mark_sc16_offline(void)
{
    s_task_status.sc16_online = 0U;
    CloudLink_SetTransportOnline(0U);
}

void Peripheral_Task(void const *argument)
{
    uint32_t retry_delay = SC16_RETRY_MIN_MS;
    uint32_t next_retry_tick = 0U;
    uint32_t previous_error_count = 0U;
    uint32_t next_health_tick = 0U;
    uint32_t next_radar_retry_tick = 0U;
    uint32_t radar_start_tick = 0U;
    uint8_t consecutive_service_errors = 0U;
    uint8_t radar_waiting_to_start = 0U;
    HAL_StatusTypeDef result;

    (void)argument;
    s_task_handle = xTaskGetCurrentTaskHandle();
    memset(&s_task_status, 0, sizeof(s_task_status));
    s_next_cloud_tx_tick = 0U;
    s_cloud_tx_failure_streak = 0U;
    osDelay(100U);

    I2C_Bus_Init();
    print_i2c_scan();
    EnvSensors_Init();
    Debug_UART_Printf(
        "[AHT20] present=%u ready=%u status_mode=%u\r\n",
        EnvSensors_GetStatus()->aht20_present,
        EnvSensors_GetStatus()->aht20_ok,
        EnvSensors_GetStatus()->aht20_status_method);
    Debug_UART_Printf("[BMP280] present=%u ready=%u addr=0x%02X\r\n",
                      EnvSensors_GetStatus()->bmp280_present,
                      EnvSensors_GetStatus()->bmp280_ok,
                      EnvSensors_GetStatus()->bmp280_addr);

    GPS_NMEA_Init();
    CloudLink_Init();
    D6_Radar_Port_Init(radar_write);
    result = initialize_radar_uart();
    if (result == HAL_OK) {
        s_task_status.radar_stop_attempts++;
        result = D6_Radar_Port_Stop();
        osDelay(120U);
        s_radar_uart_tail = s_radar_uart_head;
    }
    result = initialize_sc16_links();
    if (result == HAL_OK) {
        retry_delay = SC16_RETRY_MIN_MS;
        next_health_tick = HAL_GetTick() + SC16_HEALTH_PERIOD_MS;
        previous_error_count =
            SC16IS752_GetStatus()->transport_error_count;
    } else {
        CloudLink_SetTransportOnline(0U);
        next_retry_tick = HAL_GetTick() + retry_delay;
        retry_delay <<= 1;
    }
    if (s_task_status.radar_uart_online) {
        s_task_status.radar_start_attempts++;
        result = D6_Radar_Port_Start();
        if (result != HAL_OK) {
            Debug_UART_Printf("[D6] start failed hal=%u\r\n",
                              (unsigned int)result);
        }
        next_radar_retry_tick =
            HAL_GetTick() + RADAR_RETRY_INTERVAL_MS;
    }
    s_task_status.initialization_complete = 1U;

    for (;;) {
        uint32_t now;
        const D6RadarStatus *radar_status;

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(FALLBACK_POLL_MS));
        drain_radar_uart();
        D6_Radar_Port_Service();
        now = HAL_GetTick();
        radar_status = D6_Radar_Port_GetStatus();

        if (radar_waiting_to_start) {
            if ((int32_t)(now - radar_start_tick) >= 0) {
                s_task_status.radar_start_attempts++;
                result = D6_Radar_Port_Start();
                radar_waiting_to_start = 0U;
                next_radar_retry_tick =
                    now + RADAR_RETRY_INTERVAL_MS;
                Debug_UART_Printf(
                    "[D6] restart start=%lu tx=%lu hal=%u\r\n",
                    (unsigned long)s_task_status.radar_start_attempts,
                    (unsigned long)s_task_status.radar_uart_tx_bytes,
                    (unsigned int)result);
            }
        } else if (s_task_status.radar_uart_online &&
                   radar_status->state == D6_RADAR_STATE_ERROR &&
                   (int32_t)(now - next_radar_retry_tick) >= 0) {
            s_task_status.radar_stop_attempts++;
            result = D6_Radar_Port_Stop();
            if (result == HAL_OK) {
                radar_waiting_to_start = 1U;
                radar_start_tick = now + RADAR_STOP_SETTLE_MS;
            } else {
                next_radar_retry_tick =
                    now + RADAR_RETRY_INTERVAL_MS;
            }
            Debug_UART_Printf(
                "[D6] restart stop=%lu tx=%lu hal=%u\r\n",
                (unsigned long)s_task_status.radar_stop_attempts,
                (unsigned long)s_task_status.radar_uart_tx_bytes,
                (unsigned int)result);
        }

        if (s_task_status.sc16_online) {
            drain_channel(GPS_CHANNEL, s_gps_buffer, 1U);
            drain_channel(CLOUD_CHANNEL, s_cloud_buffer, 0U);

            if ((int32_t)(now - next_health_tick) >= 0) {
                const SC16IS752_Status *sc16;

                result = SC16IS752_HealthCheck();
                sc16 = SC16IS752_GetStatus();
                next_health_tick = now + SC16_HEALTH_PERIOD_MS;
                if (result != HAL_OK) {
                    s_task_status.sc16_health_failures++;
                    mark_sc16_offline();
                    retry_delay = SC16_RETRY_MIN_MS;
                    next_retry_tick = now + retry_delay;
                    Debug_UART_Printf(
                        "[SC16] health failed spr=%02X/%02X hal=%u\r\n",
                        sc16->last_scratch_a, sc16->last_scratch_b,
                        (unsigned int)result);
                }
            }

            if (s_task_status.sc16_online &&
                SC16IS752_GetStatus()->transport_error_count !=
                previous_error_count) {
                previous_error_count =
                    SC16IS752_GetStatus()->transport_error_count;
                consecutive_service_errors++;
                if (consecutive_service_errors >= SC16_SERVICE_ERROR_LIMIT) {
                    mark_sc16_offline();
                    retry_delay = SC16_RETRY_MIN_MS;
                    next_retry_tick = now + retry_delay;
                    Debug_UART_Printf(
                        "[SC16] offline after repeated SPI errors\r\n");
                }
            } else if (s_task_status.sc16_online) {
                consecutive_service_errors = 0U;
            }
        } else if ((int32_t)(HAL_GetTick() - next_retry_tick) >= 0) {
            result = initialize_sc16_links();
            if (result == HAL_OK) {
                s_task_status.sc16_reconnects++;
                retry_delay = SC16_RETRY_MIN_MS;
                consecutive_service_errors = 0U;
                s_cloud_tx_failure_streak = 0U;
                s_task_status.cloud_tx_failure_streak = 0U;
                next_health_tick =
                    HAL_GetTick() + SC16_HEALTH_PERIOD_MS;
                previous_error_count =
                    SC16IS752_GetStatus()->transport_error_count;
            } else {
                next_retry_tick = HAL_GetTick() + retry_delay;
                if (retry_delay < SC16_RETRY_MAX_MS) {
                    retry_delay <<= 1;
                    if (retry_delay > SC16_RETRY_MAX_MS) {
                        retry_delay = SC16_RETRY_MAX_MS;
                    }
                }
            }
        }
        CloudLink_Service();
        if (service_cloud_tx()) {
            mark_sc16_offline();
            retry_delay = SC16_RETRY_MIN_MS;
            next_retry_tick = HAL_GetTick() + retry_delay;
            Debug_UART_Printf(
                "[SC16] offline after %u cloud TX failures\r\n",
                (unsigned int)s_cloud_tx_failure_streak);
        }
    }
}

void Peripheral_Task_NotifyFromISR(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (s_task_handle != NULL) {
        vTaskNotifyGiveFromISR(s_task_handle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    uint16_t index;

    if (huart->Instance != UART7) {
        return;
    }

    s_radar_uart_rx_armed = 0U;
    for (index = 0U; index < size; index++) {
        uint16_t head = s_radar_uart_head;
        uint16_t next =
            (uint16_t)((head + 1U) & RADAR_UART_RING_MASK);

        if (next == s_radar_uart_tail) {
            s_task_status.radar_uart_drop_count++;
        } else {
            s_radar_uart_ring[head] = s_radar_uart_rx[index];
            __DMB();
            s_radar_uart_head = next;
            s_task_status.radar_uart_rx_bytes++;
        }
    }

    if (arm_radar_uart_rx() != HAL_OK) {
        s_task_status.radar_uart_error_count++;
    }
    Peripheral_Task_NotifyFromISR();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART7) {
        s_radar_uart_rx_armed = 0U;
        s_task_status.radar_uart_error_count++;
        Peripheral_Task_NotifyFromISR();
    }
}

const PeripheralTaskStatus *Peripheral_Task_GetStatus(void)
{
    return &s_task_status;
}

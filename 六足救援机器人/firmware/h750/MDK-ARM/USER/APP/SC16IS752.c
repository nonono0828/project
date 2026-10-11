#include "sc16is752.h"
#include "spi.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define SC16_REG_RHR 0x00U
#define SC16_REG_THR 0x00U
#define SC16_REG_IER 0x01U
#define SC16_REG_FCR 0x02U
#define SC16_REG_LCR 0x03U
#define SC16_REG_MCR 0x04U
#define SC16_REG_LSR 0x05U
#define SC16_REG_SPR 0x07U
#define SC16_REG_TXLVL 0x08U
#define SC16_REG_RXLVL 0x09U
#define SC16_REG_IOCONTROL 0x0EU
#define SC16_REG_EFCR 0x0FU
#define SC16_REG_DLL 0x00U
#define SC16_REG_DLH 0x01U

#define SC16_LCR_8N1 0x03U
#define SC16_LCR_DIVISOR 0x80U
#define SC16_FCR_ENABLE 0x01U
#define SC16_FCR_RX_RESET 0x02U
#define SC16_FCR_TX_RESET 0x04U
#define SC16_IER_RX 0x01U
#define SC16_IER_RLS 0x04U
#define SC16_LSR_OVERRUN 0x02U
#define SC16_LSR_LINE_ERRORS 0x1CU
#define SC16_MCR_LOOPBACK 0x10U
#define SC16_SW_RESET 0x08U
#define SC16_FIFO_SIZE 64U
#define SC16_SPI_READ 0x80U
#define SC16_SPI_FRAME_SIZE (SC16_FIFO_SIZE + 1U)
#define SC16_MAX_BAUD_ERROR_PERMILLE 30UL
#define SC16_LOOPBACK_TIMEOUT_MS 50U

static uint32_t s_xtal_hz = SC16IS752_DEFAULT_XTAL_HZ;
static SC16IS752_Status s_status;

static uint8_t channel_index(SC16IS752_Channel channel)
{
    return channel == SC16IS752_CH_B ? 1U : 0U;
}

static uint8_t register_address(SC16IS752_Channel channel, uint8_t reg)
{
    return (uint8_t)(((reg & 0x0FU) << 3) |
                     ((channel_index(channel) & 0x01U) << 1));
}

static HAL_StatusTypeDef spi_write(SC16IS752_Channel channel, uint8_t reg,
                                   const uint8_t *buffer, uint16_t length,
                                   uint32_t timeout_ms)
{
    uint8_t frame[SC16_SPI_FRAME_SIZE];
    HAL_StatusTypeDef result;

    if (buffer == NULL || length == 0U || length > SC16_FIFO_SIZE) {
        return HAL_ERROR;
    }
    frame[0] = register_address(channel, reg);
    memcpy(&frame[1], buffer, length);

    HAL_GPIO_WritePin(SC16_CS_GPIO_Port, SC16_CS_Pin, GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(&hspi3, frame, (uint16_t)(length + 1U),
                              timeout_ms);
    HAL_GPIO_WritePin(SC16_CS_GPIO_Port, SC16_CS_Pin, GPIO_PIN_SET);
    if (result != HAL_OK) {
        s_status.transport_error_count++;
    }
    return result;
}

static HAL_StatusTypeDef spi_read(SC16IS752_Channel channel, uint8_t reg,
                                  uint8_t *buffer, uint16_t length,
                                  uint32_t timeout_ms)
{
    uint8_t tx_frame[SC16_SPI_FRAME_SIZE];
    uint8_t rx_frame[SC16_SPI_FRAME_SIZE];
    HAL_StatusTypeDef result;

    if (buffer == NULL || length == 0U || length > SC16_FIFO_SIZE) {
        return HAL_ERROR;
    }
    memset(tx_frame, 0xFF, (size_t)(length + 1U));
    tx_frame[0] = (uint8_t)(register_address(channel, reg) | SC16_SPI_READ);

    HAL_GPIO_WritePin(SC16_CS_GPIO_Port, SC16_CS_Pin, GPIO_PIN_RESET);
    result = HAL_SPI_TransmitReceive(&hspi3, tx_frame, rx_frame,
                                     (uint16_t)(length + 1U), timeout_ms);
    HAL_GPIO_WritePin(SC16_CS_GPIO_Port, SC16_CS_Pin, GPIO_PIN_SET);
    if (result != HAL_OK) {
        s_status.transport_error_count++;
        return result;
    }
    memcpy(buffer, &rx_frame[1], length);
    return result;
}

static HAL_StatusTypeDef write_register(SC16IS752_Channel channel,
                                        uint8_t reg, uint8_t value)
{
    return spi_write(channel, reg, &value, 1U, 20U);
}

static HAL_StatusTypeDef read_register(SC16IS752_Channel channel,
                                       uint8_t reg, uint8_t *value)
{
    return spi_read(channel, reg, value, 1U, 20U);
}

static HAL_StatusTypeDef burst_write(SC16IS752_Channel channel,
                                     uint8_t reg, const uint8_t *buffer,
                                     uint16_t length)
{
    return spi_write(channel, reg, buffer, length, 30U);
}

static HAL_StatusTypeDef burst_read(SC16IS752_Channel channel,
                                    uint8_t reg, uint8_t *buffer,
                                    uint16_t length)
{
    return spi_read(channel, reg, buffer, length, 30U);
}

static HAL_StatusTypeDef verify_scratch(SC16IS752_Channel channel,
                                        uint8_t pattern, uint8_t *passed)
{
    uint8_t value = 0U;
    HAL_StatusTypeDef result;

    *passed = 0U;
    result = write_register(channel, SC16_REG_SPR, pattern);
    if (result == HAL_OK) {
        result = read_register(channel, SC16_REG_SPR, &value);
    }
    if (result == HAL_OK && value == pattern) {
        *passed = 1U;
        return HAL_OK;
    }
    return HAL_ERROR;
}

static HAL_StatusTypeDef read_scratch_pair(uint8_t *scratch_a,
                                           uint8_t *scratch_b)
{
    HAL_StatusTypeDef result;

    result = read_register(SC16IS752_CH_A, SC16_REG_SPR, scratch_a);
    if (result == HAL_OK) {
        result = read_register(SC16IS752_CH_B, SC16_REG_SPR, scratch_b);
    }
    return result;
}

HAL_StatusTypeDef SC16IS752_Init(uint8_t addr7, uint32_t xtal_hz)
{
    HAL_StatusTypeDef result;

    s_status.present = 0U;
    s_status.scratch_a_ok = 0U;
    s_status.scratch_b_ok = 0U;
    s_status.loopback_a_ok = 0U;
    s_status.loopback_b_ok = 0U;
    s_status.last_scratch_a = 0U;
    s_status.last_scratch_b = 0U;
    (void)addr7;
    if (!SPI3_IsReady()) {
        s_status.transport_error_count++;
        return HAL_ERROR;
    }
    s_xtal_hz = xtal_hz == 0UL ? SC16IS752_DEFAULT_XTAL_HZ : xtal_hz;

    result = write_register(SC16IS752_CH_A, SC16_REG_IOCONTROL, SC16_SW_RESET);
    if (result != HAL_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(2U));

    result = verify_scratch(SC16IS752_CH_A, 0x5AU, &s_status.scratch_a_ok);
    if (result != HAL_OK) {
        return result;
    }
    result = verify_scratch(SC16IS752_CH_B, 0xA5U, &s_status.scratch_b_ok);
    if (result != HAL_OK) {
        return result;
    }
    s_status.present = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef SC16IS752_HealthCheck(void)
{
    uint8_t scratch_a = 0U;
    uint8_t scratch_b = 0U;
    HAL_StatusTypeDef result;
    uint8_t valid;

    s_status.health_check_count++;
    result = read_scratch_pair(&scratch_a, &scratch_b);
    valid = result == HAL_OK &&
            scratch_a == 0x5AU && scratch_b == 0xA5U;
    if (!valid) {
        vTaskDelay(pdMS_TO_TICKS(1U));
        scratch_a = 0U;
        scratch_b = 0U;
        result = read_scratch_pair(&scratch_a, &scratch_b);
        valid = result == HAL_OK &&
                scratch_a == 0x5AU && scratch_b == 0xA5U;
        if (valid) {
            s_status.health_retry_recovered_count++;
        }
    }

    s_status.last_health_tick = HAL_GetTick();
    s_status.last_scratch_a = scratch_a;
    s_status.last_scratch_b = scratch_b;
    s_status.scratch_a_ok =
        result == HAL_OK && scratch_a == 0x5AU ? 1U : 0U;
    s_status.scratch_b_ok =
        result == HAL_OK && scratch_b == 0xA5U ? 1U : 0U;
    if (!valid) {
        s_status.health_error_count++;
        s_status.present = 0U;
        return result == HAL_OK ? HAL_ERROR : result;
    }

    s_status.present = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef SC16IS752_ConfigureUart(SC16IS752_Channel channel,
                                         uint32_t baud)
{
    uint32_t actual_baud;
    uint32_t baud_error;
    uint32_t divisor_value;
    uint16_t divisor;
    HAL_StatusTypeDef result;

    if (!s_status.present || baud == 0U || s_xtal_hz == 0UL) {
        return HAL_ERROR;
    }
    divisor_value = (s_xtal_hz + baud * 8UL) / (baud * 16UL);
    if (divisor_value == 0UL || divisor_value > 0xFFFFUL) {
        return HAL_ERROR;
    }
    divisor = (uint16_t)divisor_value;
    actual_baud = s_xtal_hz / (16UL * divisor_value);
    baud_error = actual_baud > baud ?
                 actual_baud - baud : baud - actual_baud;
    if ((baud_error * 1000UL) / baud > SC16_MAX_BAUD_ERROR_PERMILLE) {
        return HAL_ERROR;
    }

    result = write_register(channel, SC16_REG_IER, 0x00U);
    if (result != HAL_OK) return result;
    result = write_register(channel, SC16_REG_LCR, SC16_LCR_DIVISOR);
    if (result != HAL_OK) return result;
    result = write_register(channel, SC16_REG_DLL,
                            (uint8_t)(divisor & 0xFFU));
    if (result != HAL_OK) return result;
    result = write_register(channel, SC16_REG_DLH,
                            (uint8_t)(divisor >> 8));
    if (result != HAL_OK) return result;
    result = write_register(channel, SC16_REG_LCR, SC16_LCR_8N1);
    if (result != HAL_OK) return result;
    result = SC16IS752_ClearFifos(channel);
    if (result != HAL_OK) return result;
    result = write_register(channel, SC16_REG_EFCR, 0x00U);
    if (result != HAL_OK) return result;
    return write_register(channel, SC16_REG_IER,
                          (uint8_t)(SC16_IER_RX | SC16_IER_RLS));
}

HAL_StatusTypeDef SC16IS752_ClearFifos(SC16IS752_Channel channel)
{
    HAL_StatusTypeDef result;

    result = write_register(channel, SC16_REG_FCR,
                            SC16_FCR_ENABLE |
                            SC16_FCR_RX_RESET |
                            SC16_FCR_TX_RESET);
    if (result != HAL_OK) {
        return result;
    }
    return write_register(channel, SC16_REG_FCR, SC16_FCR_ENABLE);
}

uint8_t SC16IS752_RxLevel(SC16IS752_Channel channel)
{
    uint8_t level = 0U;

    if (read_register(channel, SC16_REG_RXLVL, &level) != HAL_OK) {
        return 0U;
    }
    return level;
}

uint16_t SC16IS752_ReadAvailable(SC16IS752_Channel channel,
                                uint8_t *buffer, uint16_t max_length)
{
    uint8_t line_status = 0U;
    uint8_t level;
    uint16_t length;
    uint8_t index = channel_index(channel);

    if (buffer == NULL || max_length == 0U || !s_status.present) {
        return 0U;
    }
    if (read_register(channel, SC16_REG_LSR, &line_status) != HAL_OK) {
        return 0U;
    }
    if ((line_status & SC16_LSR_OVERRUN) != 0U) {
        s_status.rx_overrun_count++;
    }
    if ((line_status & SC16_LSR_LINE_ERRORS) != 0U) {
        s_status.line_error_count++;
    }

    level = SC16IS752_RxLevel(channel);
    if (level == 0U) {
        return 0U;
    }
    length = level;
    if (length > max_length) {
        length = max_length;
    }
    if (burst_read(channel, SC16_REG_RHR, buffer, length) != HAL_OK) {
        return 0U;
    }
    s_status.rx_bytes[index] += length;
    s_status.last_rx_tick[index] = HAL_GetTick();
    return length;
}

HAL_StatusTypeDef SC16IS752_Write(SC16IS752_Channel channel,
                                 const uint8_t *buffer, uint16_t length,
                                 uint32_t timeout_ms)
{
    uint16_t sent = 0U;
    uint32_t start_tick = HAL_GetTick();
    uint8_t index = channel_index(channel);

    if (buffer == NULL || length == 0U || !s_status.present) {
        return HAL_ERROR;
    }
    while (sent < length) {
        uint8_t available = 0U;
        uint16_t chunk;

        if (timeout_ms != 0U &&
            (HAL_GetTick() - start_tick) >= timeout_ms) {
            s_status.tx_error_count++;
            return HAL_TIMEOUT;
        }
        if (read_register(channel, SC16_REG_TXLVL, &available) != HAL_OK) {
            s_status.tx_error_count++;
            return HAL_ERROR;
        }
        if (available == 0U) {
            vTaskDelay(pdMS_TO_TICKS(1U));
            continue;
        }
        chunk = (uint16_t)(length - sent);
        if (chunk > available) {
            chunk = available;
        }
        if (chunk > SC16_FIFO_SIZE) {
            chunk = SC16_FIFO_SIZE;
        }
        if (burst_write(channel, SC16_REG_THR, &buffer[sent], chunk) != HAL_OK) {
            s_status.tx_error_count++;
            return HAL_ERROR;
        }
        sent = (uint16_t)(sent + chunk);
        s_status.tx_bytes[index] += chunk;
    }
    return HAL_OK;
}

HAL_StatusTypeDef SC16IS752_LoopbackTest(SC16IS752_Channel channel)
{
    static const uint8_t pattern[4] = {0x55U, 0xA5U, 0x5AU, 0x11U};
    uint8_t received[sizeof(pattern)] = {0U};
    uint8_t saved_mcr = 0U;
    uint8_t received_count;
    uint8_t passed = 0U;
    uint8_t index = channel_index(channel);
    uint32_t saved_rx_bytes = s_status.rx_bytes[index];
    uint32_t saved_tx_bytes = s_status.tx_bytes[index];
    uint32_t saved_last_rx_tick = s_status.last_rx_tick[index];
    uint32_t start_tick;
    HAL_StatusTypeDef result;
    HAL_StatusTypeDef restore_result;

    result = read_register(channel, SC16_REG_MCR, &saved_mcr);
    if (result != HAL_OK) {
        return result;
    }
    result = SC16IS752_ClearFifos(channel);
    if (result == HAL_OK) {
        result = write_register(channel, SC16_REG_MCR,
                                (uint8_t)(saved_mcr | SC16_MCR_LOOPBACK));
    }
    if (result == HAL_OK) {
        result = SC16IS752_Write(channel, pattern, sizeof(pattern), 50U);
    }
    if (result == HAL_OK) {
        start_tick = HAL_GetTick();
        while (SC16IS752_RxLevel(channel) < sizeof(pattern) &&
               (HAL_GetTick() - start_tick) < SC16_LOOPBACK_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(1U));
        }
        received_count = (uint8_t)SC16IS752_ReadAvailable(
            channel, received, sizeof(received));
        if (received_count == sizeof(pattern) &&
            memcmp(pattern, received, sizeof(pattern)) == 0) {
            passed = 1U;
        } else {
            result = HAL_ERROR;
        }
    }

    restore_result = write_register(channel, SC16_REG_MCR, saved_mcr);
    (void)SC16IS752_ClearFifos(channel);
    s_status.rx_bytes[index] = saved_rx_bytes;
    s_status.tx_bytes[index] = saved_tx_bytes;
    s_status.last_rx_tick[index] = saved_last_rx_tick;
    if (channel == SC16IS752_CH_A) {
        s_status.loopback_a_ok = passed;
    } else {
        s_status.loopback_b_ok = passed;
    }
    return result == HAL_OK ? restore_result : result;
}

uint8_t SC16IS752_IrqAsserted(void)
{
    return HAL_GPIO_ReadPin(SC16_IRQ_GPIO_Port, SC16_IRQ_Pin) == GPIO_PIN_RESET;
}

const SC16IS752_Status *SC16IS752_GetStatus(void)
{
    return &s_status;
}

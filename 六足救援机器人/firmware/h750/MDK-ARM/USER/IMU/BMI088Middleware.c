#include "BMI088Middleware.h"
#include "main.h"
#include "bsp_dwt.h"
#include "spi.h"

static BMI088_ComStatus s_com_status;

GPIO_PinState BMI088_MISO_READ(void)
{
    return HAL_GPIO_ReadPin(GYRO_MISO_GPIO_Port, GYRO_MISO_Pin);
}

uint8_t BMI088_read_write_byte(uint8_t data)
{
    uint8_t data_r = 0;
    HAL_StatusTypeDef status =
        HAL_SPI_TransmitReceive(&hspi1, &data, &data_r, 1, 0xffff);

    s_com_status.transfer_count++;
    s_com_status.last_hal_status = (uint8_t)status;
    s_com_status.last_hal_error = hspi1.ErrorCode;
    s_com_status.miso_level =
        (uint8_t)HAL_GPIO_ReadPin(GYRO_MISO_GPIO_Port, GYRO_MISO_Pin);
    if (status != HAL_OK)
    {
        s_com_status.error_count++;
    }
    return data_r;
}

void BMI088_GetComStatus(BMI088_ComStatus *status)
{
    if (status != 0)
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        *status = s_com_status;
        __set_PRIMASK(primask);
    }
}

void BMI088_ResetComStatus(void)
{
    s_com_status.transfer_count = 0U;
    s_com_status.error_count = 0U;
    s_com_status.last_hal_error = HAL_SPI_ERROR_NONE;
    s_com_status.last_hal_status = (uint8_t)HAL_OK;
    s_com_status.miso_level =
        (uint8_t)HAL_GPIO_ReadPin(GYRO_MISO_GPIO_Port, GYRO_MISO_Pin);
}

#include "debug_task.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "debug_uart.h"
#include "MPU_task.h"
#include "BMI088Middleware.h"
#include "env_sensors.h"
#include "sc16is752.h"
#include "gps_nmea.h"
#include "d6_radar_port.h"
#include "peripheral_task.h"
#include "gas_alarm.h"
#include "person_detect.h"
#include "cloud_link.h"
#include "leg_status.h"
#include "gait_status.h"
#include "obstacle_avoidance.h"
#include "autonomous_drive.h"
#include <string.h>

static long scaled_1000(float value)
{
    return (long)(value * 1000.0f);
}

extern "C"
void Debug_Task(void const *argument)
{
    uint32_t last_imu_tick = 0U;
    uint32_t last_status_tick = 0U;

    (void)argument;
    for (;;) {
        uint32_t now;

        Debug_UART_Process(20U);
        now = HAL_GetTick();
        if ((now - last_imu_tick) >= 200U) {
            INS_t snapshot;
            BMI088_ComStatus imu_com;
            const INS_t *imu = get_imu_control_point();

            last_imu_tick = now;
            taskENTER_CRITICAL();
            memcpy(&snapshot, imu, sizeof(snapshot));
            taskEXIT_CRITICAL();
            BMI088_GetComStatus(&imu_com);
            Debug_UART_Printf(
                "[IMU] ok=%u err=0x%02X id=%02X/%02X try=%lu/%lu "
                "hal=%u spierr=0x%08lX xfer=%lu/%lu miso=%u "
                "gyro_mrad=%ld,%ld,%ld accel_milli=%ld,%ld,%ld "
                "rpy_mdeg=%ld,%ld,%ld\r\n",
                snapshot.if_init_success,
                BMI088_InitError, BMI088_AccelChipId, BMI088_GyroChipId,
                (unsigned long)BMI088_InitSuccesses,
                (unsigned long)BMI088_InitAttempts,
                (unsigned int)imu_com.last_hal_status,
                (unsigned long)imu_com.last_hal_error,
                (unsigned long)imu_com.transfer_count,
                (unsigned long)imu_com.error_count,
                (unsigned int)imu_com.miso_level,
                scaled_1000(snapshot.Gyro[0]),
                scaled_1000(snapshot.Gyro[1]),
                scaled_1000(snapshot.Gyro[2]),
                scaled_1000(snapshot.Accel[0]),
                scaled_1000(snapshot.Accel[1]),
                scaled_1000(snapshot.Accel[2]),
                scaled_1000(snapshot.Roll),
                scaled_1000(snapshot.Pitch),
                scaled_1000(snapshot.Yaw));
        }

        if ((now - last_status_tick) >= 1000U) {
            const EnvSensorStatus *env = EnvSensors_GetStatus();
            const SC16IS752_Status *sc16 = SC16IS752_GetStatus();
            const GPS_NMEA_Status *gps = GPS_NMEA_GetStatus();
            const D6RadarStatus *radar = D6_Radar_Port_GetStatus();
            const PeripheralTaskStatus *peripheral =
                Peripheral_Task_GetStatus();
            const GasAlarmStatus *gas = GasAlarm_GetStatus();
            const PersonDetectStatus *person =
                PersonDetect_GetStatus();
            const CloudLinkStatus *cloud = CloudLink_GetStatus();
            const ObstacleAvoidanceStatus *avoidance =
                ObstacleAvoidance_GetStatus();
            const AutonomousDriveStatus *autonomous =
                AutonomousDrive_GetStatus();
            LegLinkStatus leg_link[6];
            uint16_t nearest_angle = 0U;
            uint16_t nearest = D6_Radar_Port_GetNearest(&nearest_angle);
            uint8_t leg_index;

            for (leg_index = 0U; leg_index < 6U; leg_index++) {
                Leg_GetLinkStatus(leg_index, &leg_link[leg_index]);
            }

            last_status_tick = now;
            Debug_UART_Printf(
                "[ENV] aht=%u t_centi=%ld rh_centi=%ld bmp=%u "
                "p_pa=%ld alt_centi=%ld err=%lu crc=%lu\r\n",
                env->aht20_ok,
                (long)(env->aht_temperature_c * 100.0f),
                (long)(env->humidity_rh * 100.0f),
                env->bmp280_ok,
                (long)(env->pressure_hpa * 100.0f),
                (long)(env->altitude_m * 100.0f),
                (unsigned long)env->error_count,
                (unsigned long)env->aht_crc_error_count);
            Debug_UART_Printf(
                "[IO] person=%u init=%u raw_pa11=%u changes=%lu "
                "gas=%u init=%u raw_pc9=%u changes=%lu\r\n",
                person->detected,
                person->initialized,
                (unsigned int)person->raw_level,
                (unsigned long)person->change_count,
                gas->alarm,
                gas->initialized,
                (unsigned int)gas->raw_level,
                (unsigned long)gas->change_count);
            Debug_UART_Printf(
                "[LINK] sc16=%u spr=%u/%u loop=%u/%u rx=%lu/%lu "
                "health=%lu/%lu/%lu spi=%lu/%lu recon=%lu tx_streak=%u "
                "radar_uart=%u\r\n",
                peripheral->sc16_online,
                sc16->scratch_a_ok, sc16->scratch_b_ok,
                sc16->loopback_a_ok, sc16->loopback_b_ok,
                (unsigned long)sc16->rx_bytes[0],
                (unsigned long)sc16->rx_bytes[1],
                (unsigned long)sc16->health_check_count,
                (unsigned long)sc16->health_error_count,
                (unsigned long)sc16->health_retry_recovered_count,
                (unsigned long)peripheral->spi3_recovery_successes,
                (unsigned long)peripheral->spi3_recovery_attempts,
                (unsigned long)peripheral->sc16_reconnects,
                peripheral->cloud_tx_failure_streak,
                peripheral->radar_uart_online);
            Debug_UART_Printf(
                "[GPS] rx=%lu nmea=%lu r/g/v/a=%lu/%lu/%lu/%lu "
                "crc=%lu parse=%lu\r\n",
                (unsigned long)gps->byte_count,
                (unsigned long)gps->sentence_count,
                (unsigned long)gps->rmc_count,
                (unsigned long)gps->gga_count,
                (unsigned long)gps->gsv_count,
                (unsigned long)gps->gsa_count,
                (unsigned long)gps->checksum_error_count,
                (unsigned long)gps->parse_error_count);
            Debug_UART_Printf(
                "[GPS_FIX] fix=%u rmc/gga=%u/%u q/type=%u/%u "
                "used/view=%u/%u report=%u hdop_milli=%ld\r\n",
                gps->fix_valid, gps->rmc_fix_valid, gps->gga_fix_valid,
                gps->fix_quality, gps->fix_type,
                gps->satellites_used, gps->satellites_in_view,
                gps->satellites, scaled_1000(gps->hdop));
            Debug_UART_Printf(
                "[CLOUD] on=%u self=%u rx=%lu frm=%lu cmd=%lu "
                "rej=%lu crc=%lu fto=%lu cto=%lu tx=%lu txe=%lu "
                "act=%u ry=%d lx=%d\r\n",
                cloud->transport_online,
                cloud->crc_self_test_ok,
                (unsigned long)cloud->rx_bytes,
                (unsigned long)cloud->rx_frames,
                (unsigned long)cloud->command_frames,
                (unsigned long)cloud->command_rejected,
                (unsigned long)cloud->crc_error_count,
                (unsigned long)cloud->frame_timeout_count,
                (unsigned long)cloud->control_timeout_count,
                (unsigned long)cloud->tx_frames,
                (unsigned long)cloud->tx_error_count,
                cloud->control_active,
                (int)cloud->motion_ry,
                (int)cloud->motion_lx);
            Debug_UART_Printf(
                "[CTRL] seq=%lu valid=%u old=%lu "
                "physical=%u rearm=0x%02X takeover=%lu blocked=%lu "
                "terrain=%u lift_pct=%u speed=%u speed_pct=%u\r\n",
                (unsigned long)cloud->last_control_sequence,
                cloud->control_sequence_valid,
                (unsigned long)cloud->control_sequence_rejected,
                cloud->physical_override_active,
                cloud->physical_rearm_mask,
                (unsigned long)cloud->physical_override_count,
                (unsigned long)cloud->physical_blocked_count,
                (unsigned int)Hexapod_GetTerrainMode(),
                Hexapod_GetTerrainMode() ? 350U : 100U,
                (unsigned int)Hexapod_GetSpeedMode(),
                (unsigned int)Hexapod_GetSpeedPercent());
            Debug_UART_Printf(
                "[AVOID] en=%u ready=%u state=%u "
                "mm=%u,%u,%u,%u in=%d,%d,%d out=%d,%d,%d "
                "modified=%lu blocked=%lu emergency=%lu stale=%lu\r\n",
                avoidance->enabled,
                avoidance->radar_ready,
                (unsigned int)avoidance->state,
                avoidance->front_mm,
                avoidance->right_mm,
                avoidance->rear_mm,
                avoidance->left_mm,
                (int)avoidance->input_rx,
                (int)avoidance->input_ry,
                (int)avoidance->input_lx,
                (int)avoidance->output_rx,
                (int)avoidance->output_ry,
                (int)avoidance->output_lx,
                (unsigned long)avoidance->modified_count,
                (unsigned long)avoidance->blocked_count,
                (unsigned long)avoidance->emergency_count,
                (unsigned long)avoidance->radar_stale_count);
            Debug_UART_Printf(
                "[AUTO] en=%u ready=%u state=%u cmd=%d,%d,%d "
                "transition=%lu turn=%lu reverse=%lu trapped=%lu "
                "manual_exit=%lu\r\n",
                autonomous->enabled,
                autonomous->radar_ready,
                (unsigned int)autonomous->state,
                (int)autonomous->command_rx,
                (int)autonomous->command_ry,
                (int)autonomous->command_lx,
                (unsigned long)autonomous->transition_count,
                (unsigned long)autonomous->turn_count,
                (unsigned long)autonomous->reverse_count,
                (unsigned long)autonomous->trapped_count,
                (unsigned long)autonomous->manual_override_count);
            Debug_UART_Printf(
                "[LEGS_TX] start/done="
                "%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu\r\n",
                (unsigned long)leg_link[0].tx_started,
                (unsigned long)leg_link[0].tx_completed,
                (unsigned long)leg_link[1].tx_started,
                (unsigned long)leg_link[1].tx_completed,
                (unsigned long)leg_link[2].tx_started,
                (unsigned long)leg_link[2].tx_completed,
                (unsigned long)leg_link[3].tx_started,
                (unsigned long)leg_link[3].tx_completed,
                (unsigned long)leg_link[4].tx_started,
                (unsigned long)leg_link[4].tx_completed,
                (unsigned long)leg_link[5].tx_started,
                (unsigned long)leg_link[5].tx_completed);
            Debug_UART_Printf(
                "[LEGS_TX_ERR] busy/err="
                "%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu\r\n",
                (unsigned long)leg_link[0].tx_busy,
                (unsigned long)leg_link[0].tx_error,
                (unsigned long)leg_link[1].tx_busy,
                (unsigned long)leg_link[1].tx_error,
                (unsigned long)leg_link[2].tx_busy,
                (unsigned long)leg_link[2].tx_error,
                (unsigned long)leg_link[3].tx_busy,
                (unsigned long)leg_link[3].tx_error,
                (unsigned long)leg_link[4].tx_busy,
                (unsigned long)leg_link[4].tx_error,
                (unsigned long)leg_link[5].tx_busy,
                (unsigned long)leg_link[5].tx_error);
            Debug_UART_Printf(
                "[LEGS_RX] start/done="
                "%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu\r\n",
                (unsigned long)leg_link[0].rx_started,
                (unsigned long)leg_link[0].rx_completed,
                (unsigned long)leg_link[1].rx_started,
                (unsigned long)leg_link[1].rx_completed,
                (unsigned long)leg_link[2].rx_started,
                (unsigned long)leg_link[2].rx_completed,
                (unsigned long)leg_link[3].rx_started,
                (unsigned long)leg_link[3].rx_completed,
                (unsigned long)leg_link[4].rx_started,
                (unsigned long)leg_link[4].rx_completed,
                (unsigned long)leg_link[5].rx_started,
                (unsigned long)leg_link[5].rx_completed);
            Debug_UART_Printf(
                "[LEGS_RX_ERR] count=%lu,%lu,%lu,%lu,%lu,%lu\r\n",
                (unsigned long)leg_link[0].rx_start_error,
                (unsigned long)leg_link[1].rx_start_error,
                (unsigned long)leg_link[2].rx_start_error,
                (unsigned long)leg_link[3].rx_start_error,
                (unsigned long)leg_link[4].rx_start_error,
                (unsigned long)leg_link[5].rx_start_error);
            Debug_UART_Printf(
                "[LEGS_FB] valid/invalid="
                "%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu,%lu/%lu\r\n",
                (unsigned long)leg_link[0].feedback_valid,
                (unsigned long)leg_link[0].feedback_invalid,
                (unsigned long)leg_link[1].feedback_valid,
                (unsigned long)leg_link[1].feedback_invalid,
                (unsigned long)leg_link[2].feedback_valid,
                (unsigned long)leg_link[2].feedback_invalid,
                (unsigned long)leg_link[3].feedback_valid,
                (unsigned long)leg_link[3].feedback_invalid,
                (unsigned long)leg_link[4].feedback_valid,
                (unsigned long)leg_link[4].feedback_invalid,
                (unsigned long)leg_link[5].feedback_valid,
                (unsigned long)leg_link[5].feedback_invalid);
            Debug_UART_Printf(
                "[D6] state=%u frame=%lu crc=%lu uart_rx=%lu "
                "uart_tx=%lu tx_err=%lu start=%lu stop=%lu "
                "uart_err=%lu uart_drop=%lu nearest_mm=%u angle=%u "
                "log_drop=%lu\r\n",
                (unsigned int)radar->state,
                (unsigned long)radar->frame_count,
                (unsigned long)radar->checksum_error_count,
                (unsigned long)peripheral->radar_uart_rx_bytes,
                (unsigned long)peripheral->radar_uart_tx_bytes,
                (unsigned long)peripheral->radar_uart_tx_error_count,
                (unsigned long)peripheral->radar_start_attempts,
                (unsigned long)peripheral->radar_stop_attempts,
                (unsigned long)peripheral->radar_uart_error_count,
                (unsigned long)peripheral->radar_uart_drop_count,
                nearest, nearest_angle,
                (unsigned long)Debug_UART_GetDroppedCount());
        }
    }
}

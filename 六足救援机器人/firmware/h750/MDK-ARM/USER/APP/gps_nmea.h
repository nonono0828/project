#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t fix_valid;
    uint8_t rmc_fix_valid;
    uint8_t gga_fix_valid;
    uint8_t fix_quality;
    uint8_t fix_type;
    uint8_t satellites;
    uint8_t satellites_used;
    uint8_t satellites_in_view;
    float hdop;
    float pdop;
    float vdop;
    double latitude_deg;
    double longitude_deg;
    float speed_knots;
    float heading_deg;
    float altitude_m;
    uint8_t speed_valid;
    uint8_t heading_valid;
    char utc_time[12];
    char utc_date[8];
    uint32_t byte_count;
    uint32_t sentence_count;
    uint32_t rmc_count;
    uint32_t gga_count;
    uint32_t gsv_count;
    uint32_t gsa_count;
    uint32_t checksum_error_count;
    uint32_t parse_error_count;
    uint32_t last_byte_tick;
    uint32_t last_update_tick;
    uint32_t last_rmc_tick;
    uint32_t last_gga_tick;
    uint32_t last_gsv_tick;
    uint32_t last_gsa_tick;
    uint32_t last_position_tick;
    uint32_t last_speed_tick;
    uint32_t last_heading_tick;
    uint32_t last_satellites_tick;
} GPS_NMEA_Status;

void GPS_NMEA_Init(void);
void GPS_NMEA_FeedByte(uint8_t byte);
void GPS_NMEA_Feed(const uint8_t *data, uint16_t length);
const GPS_NMEA_Status *GPS_NMEA_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

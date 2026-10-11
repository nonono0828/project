#include "gps_nmea.h"
#include <stdlib.h>
#include <string.h>

#define GPS_LINE_MAX 128U
#define GPS_FIELD_MAX 24U
#define GPS_FIX_FRESH_MS 3000U
#define GPS_GSV_FRESH_MS 3000U
#define GPS_GSV_SOURCE_COUNT 7U
#define GPS_GSV_SOURCE_GN 0U

static GPS_NMEA_Status s_gps;
static char s_line[GPS_LINE_MAX];
static uint8_t s_line_index;
static uint8_t s_in_sentence;
static uint8_t s_gsv_source_count[GPS_GSV_SOURCE_COUNT];
static uint32_t s_gsv_source_tick[GPS_GSV_SOURCE_COUNT];

static uint8_t hex_to_nibble(char value)
{
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'A' && value <= 'F') return (uint8_t)(value - 'A' + 10);
    if (value >= 'a' && value <= 'f') return (uint8_t)(value - 'a' + 10);
    return 0xFFU;
}

static uint8_t validate_checksum(const char *line)
{
    uint8_t calculated = 0U;
    uint8_t high;
    uint8_t low;
    const char *cursor;
    const char *asterisk = NULL;

    if (line == NULL || line[0] != '$') {
        return 0U;
    }
    cursor = &line[1];
    while (*cursor != '\0') {
        if (*cursor == '*') {
            asterisk = cursor;
            break;
        }
        calculated ^= (uint8_t)*cursor++;
    }
    if (asterisk == NULL || asterisk[1] == '\0' || asterisk[2] == '\0') {
        return 0U;
    }
    high = hex_to_nibble(asterisk[1]);
    low = hex_to_nibble(asterisk[2]);
    if (high == 0xFFU || low == 0xFFU) {
        return 0U;
    }
    return calculated == (uint8_t)((high << 4) | low);
}

static uint8_t split_fields(char *line, char *fields[], uint8_t max_fields)
{
    uint8_t count = 0U;
    char *cursor = line;

    while (*cursor != '\0' && count < max_fields) {
        fields[count++] = cursor;
        while (*cursor != '\0' && *cursor != ',' && *cursor != '*') {
            cursor++;
        }
        if (*cursor == '*') {
            *cursor = '\0';
            break;
        }
        if (*cursor == ',') {
            *cursor++ = '\0';
        }
    }
    return count;
}

static uint8_t parse_double_range(const char *text, double minimum,
                                  double maximum, double *output)
{
    char *end;
    double value;

    if (text == NULL || text[0] == '\0' || output == NULL) {
        return 0U;
    }
    value = strtod(text, &end);
    if (end == text || *end != '\0' || value != value ||
        value < minimum || value > maximum) {
        return 0U;
    }
    *output = value;
    return 1U;
}

static uint8_t parse_u8_range(const char *text, uint8_t maximum,
                              uint8_t *output)
{
    char *end;
    unsigned long value;

    if (text == NULL || text[0] == '\0' || output == NULL) {
        return 0U;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > maximum) {
        return 0U;
    }
    *output = (uint8_t)value;
    return 1U;
}

static uint8_t coordinate_to_decimal(const char *raw,
                                     const char *hemisphere,
                                     uint8_t latitude,
                                     double *output)
{
    double value;
    int degrees;
    double minutes;
    double result;
    int maximum_degrees = latitude ? 90 : 180;
    char positive = latitude ? 'N' : 'E';
    char negative = latitude ? 'S' : 'W';

    if (raw == NULL || raw[0] == '\0' ||
        hemisphere == NULL || hemisphere[0] == '\0' ||
        hemisphere[1] != '\0' || output == NULL ||
        (hemisphere[0] != positive && hemisphere[0] != negative) ||
        !parse_double_range(raw, 0.0,
                            (double)maximum_degrees * 100.0 + 60.0,
                            &value)) {
        return 0U;
    }
    degrees = (int)(value / 100.0);
    minutes = value - (double)degrees * 100.0;
    if (degrees > maximum_degrees ||
        minutes < 0.0 || minutes >= 60.0 ||
        (degrees == maximum_degrees && minutes != 0.0)) {
        return 0U;
    }
    result = (double)degrees + minutes / 60.0;
    if (hemisphere[0] == negative) {
        result = -result;
    }
    *output = result;
    return 1U;
}

static void copy_field(char *destination, uint8_t destination_length,
                       const char *source)
{
    if (destination == NULL || destination_length == 0U) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, destination_length - 1U);
    destination[destination_length - 1U] = '\0';
}

static uint8_t sentence_type_is(const char *identifier, const char *type)
{
    return identifier != NULL && type != NULL &&
           strlen(identifier) == 5U && strlen(type) == 3U &&
           identifier[2] == type[0] &&
           identifier[3] == type[1] &&
           identifier[4] == type[2];
}

static uint8_t gsv_source_index(const char *identifier)
{
    if (identifier[0] == 'G' && identifier[1] == 'N') return 0U;
    if (identifier[0] == 'G' && identifier[1] == 'P') return 1U;
    if (identifier[0] == 'G' && identifier[1] == 'L') return 2U;
    if (identifier[0] == 'G' && identifier[1] == 'A') return 3U;
    if ((identifier[0] == 'G' && identifier[1] == 'B') ||
        (identifier[0] == 'B' && identifier[1] == 'D')) return 4U;
    if (identifier[0] == 'G' && identifier[1] == 'Q') return 5U;
    return 6U;
}

static uint8_t gsv_source_fresh(uint8_t source, uint32_t now)
{
    return s_gsv_source_tick[source] != 0U &&
           (now - s_gsv_source_tick[source]) <= GPS_GSV_FRESH_MS;
}

static uint8_t gsv_visible_total(uint32_t now)
{
    uint16_t total = 0U;
    uint8_t source;

    if (gsv_source_fresh(GPS_GSV_SOURCE_GN, now)) {
        return s_gsv_source_count[GPS_GSV_SOURCE_GN];
    }
    for (source = 1U; source < GPS_GSV_SOURCE_COUNT; source++) {
        if (gsv_source_fresh(source, now)) {
            total = (uint16_t)(total + s_gsv_source_count[source]);
        }
    }
    return total > 99U ? 99U : (uint8_t)total;
}

static void update_fix_summary(uint32_t now)
{
    uint8_t rmc_fresh =
        s_gps.last_rmc_tick != 0U &&
        (now - s_gps.last_rmc_tick) <= GPS_FIX_FRESH_MS;
    uint8_t gga_fresh =
        s_gps.last_gga_tick != 0U &&
        (now - s_gps.last_gga_tick) <= GPS_FIX_FRESH_MS;

    s_gps.fix_valid =
        (uint8_t)((rmc_fresh && s_gps.rmc_fix_valid) ||
                  (gga_fresh && s_gps.gga_fix_valid));
}

static void update_satellite_summary(uint32_t now)
{
    if (s_gps.gga_fix_valid && s_gps.last_gga_tick != 0U &&
        (now - s_gps.last_gga_tick) <= GPS_FIX_FRESH_MS) {
        s_gps.satellites = s_gps.satellites_used;
    } else if (s_gps.last_gsv_tick != 0U &&
               (now - s_gps.last_gsv_tick) <= GPS_GSV_FRESH_MS) {
        s_gps.satellites = s_gps.satellites_in_view;
    } else {
        s_gps.satellites = s_gps.satellites_used;
    }
    s_gps.last_satellites_tick = now;
}

static uint8_t parse_rmc(char *fields[], uint8_t count)
{
    uint32_t now;
    double latitude;
    double longitude;
    double speed = 0.0;
    double heading = 0.0;
    uint8_t speed_valid;
    uint8_t heading_valid;
    uint8_t active;

    if (count < 10U) {
        return 0U;
    }
    if (fields[2][1] != '\0' ||
        (fields[2][0] != 'A' && fields[2][0] != 'V')) {
        return 0U;
    }
    active = fields[2][0] == 'A' ? 1U : 0U;
    speed_valid = fields[7][0] != '\0' ? 1U : 0U;
    heading_valid = fields[8][0] != '\0' ? 1U : 0U;
    if (active &&
        (!coordinate_to_decimal(fields[3], fields[4], 1U, &latitude) ||
         !coordinate_to_decimal(fields[5], fields[6], 0U, &longitude))) {
        return 0U;
    }
    if (speed_valid &&
        !parse_double_range(fields[7], 0.0, 200.0, &speed)) {
        return 0U;
    }
    if (heading_valid &&
        !parse_double_range(fields[8], 0.0, 359.999999, &heading)) {
        return 0U;
    }

    now = HAL_GetTick();
    copy_field(s_gps.utc_time, sizeof(s_gps.utc_time), fields[1]);
    copy_field(s_gps.utc_date, sizeof(s_gps.utc_date), fields[9]);
    s_gps.rmc_fix_valid = active;
    s_gps.last_rmc_tick = now;
    if (active) {
        s_gps.latitude_deg = latitude;
        s_gps.longitude_deg = longitude;
        s_gps.last_position_tick = now;
        s_gps.last_update_tick = now;
        s_gps.speed_valid = speed_valid;
        s_gps.heading_valid = heading_valid;
        if (speed_valid) {
            s_gps.speed_knots = (float)speed;
            s_gps.last_speed_tick = now;
        }
        if (heading_valid) {
            s_gps.heading_deg = (float)heading;
            s_gps.last_heading_tick = now;
        }
    } else {
        s_gps.speed_valid = 0U;
        s_gps.heading_valid = 0U;
    }
    update_fix_summary(now);
    s_gps.rmc_count++;
    return 1U;
}

static uint8_t parse_gga(char *fields[], uint8_t count)
{
    uint32_t now;
    double latitude;
    double longitude;
    double hdop = 0.0;
    double altitude = 0.0;
    uint8_t fix_quality;
    uint8_t satellites_used;
    uint8_t active;

    if (count < 10U) {
        return 0U;
    }
    if (!parse_u8_range(fields[6], 9U, &fix_quality) ||
        !parse_u8_range(fields[7], 99U, &satellites_used)) {
        return 0U;
    }
    if (fields[8][0] != '\0' &&
        !parse_double_range(fields[8], 0.0, 999.99, &hdop)) {
        return 0U;
    }
    active = fix_quality > 0U ? 1U : 0U;
    if (active &&
        (!coordinate_to_decimal(fields[2], fields[3], 1U, &latitude) ||
         !coordinate_to_decimal(fields[4], fields[5], 0U, &longitude) ||
         !parse_double_range(fields[9], -1000.0, 20000.0,
                             &altitude))) {
        return 0U;
    }

    now = HAL_GetTick();
    copy_field(s_gps.utc_time, sizeof(s_gps.utc_time), fields[1]);
    s_gps.fix_quality = fix_quality;
    s_gps.gga_fix_valid = active;
    s_gps.satellites_used = satellites_used;
    s_gps.hdop = (float)hdop;
    s_gps.last_gga_tick = now;
    if (active) {
        s_gps.latitude_deg = latitude;
        s_gps.longitude_deg = longitude;
        s_gps.altitude_m = (float)altitude;
        s_gps.last_position_tick = now;
        s_gps.last_update_tick = now;
    }
    update_fix_summary(now);
    update_satellite_summary(now);
    s_gps.gga_count++;
    return 1U;
}

static uint8_t parse_gsv(char *fields[], uint8_t count)
{
    uint8_t satellites_in_view;
    uint8_t source;
    uint32_t now;

    if (count < 4U ||
        !parse_u8_range(fields[3], 99U, &satellites_in_view)) {
        return 0U;
    }

    now = HAL_GetTick();
    source = gsv_source_index(fields[0]);
    s_gsv_source_count[source] = satellites_in_view;
    s_gsv_source_tick[source] = now;
    s_gps.satellites_in_view = gsv_visible_total(now);
    s_gps.last_gsv_tick = now;
    update_fix_summary(now);
    update_satellite_summary(now);
    s_gps.gsv_count++;
    return 1U;
}

static uint8_t parse_gsa(char *fields[], uint8_t count)
{
    uint8_t fix_type;
    double value;

    if (count < 3U ||
        !parse_u8_range(fields[2], 3U, &fix_type) ||
        fix_type == 0U) {
        return 0U;
    }

    s_gps.fix_type = fix_type;
    if (count > 15U && fields[15][0] != '\0' &&
        parse_double_range(fields[15], 0.0, 999.99, &value)) {
        s_gps.pdop = (float)value;
    }
    if (count > 16U && fields[16][0] != '\0' &&
        parse_double_range(fields[16], 0.0, 999.99, &value)) {
        s_gps.hdop = (float)value;
    }
    if (count > 17U && fields[17][0] != '\0' &&
        parse_double_range(fields[17], 0.0, 999.99, &value)) {
        s_gps.vdop = (float)value;
    }
    s_gps.last_gsa_tick = HAL_GetTick();
    update_fix_summary(s_gps.last_gsa_tick);
    s_gps.gsa_count++;
    return 1U;
}

static void parse_line(const char *input)
{
    char working[GPS_LINE_MAX];
    char *fields[GPS_FIELD_MAX];
    uint8_t count;
    uint8_t parsed = 0U;
    uint8_t recognized = 0U;

    if (!validate_checksum(input)) {
        s_gps.checksum_error_count++;
        return;
    }
    strncpy(working, &input[1], sizeof(working) - 1U);
    working[sizeof(working) - 1U] = '\0';
    count = split_fields(working, fields, GPS_FIELD_MAX);
    if (count == 0U) {
        s_gps.parse_error_count++;
        return;
    }
    if (sentence_type_is(fields[0], "RMC")) {
        recognized = 1U;
        parsed = parse_rmc(fields, count);
    } else if (sentence_type_is(fields[0], "GGA")) {
        recognized = 1U;
        parsed = parse_gga(fields, count);
    } else if (sentence_type_is(fields[0], "GSV")) {
        recognized = 1U;
        parsed = parse_gsv(fields, count);
    } else if (sentence_type_is(fields[0], "GSA")) {
        recognized = 1U;
        parsed = parse_gsa(fields, count);
    }
    if (parsed) {
        s_gps.sentence_count++;
    } else if (recognized) {
        s_gps.parse_error_count++;
    }
}

void GPS_NMEA_Init(void)
{
    memset(&s_gps, 0, sizeof(s_gps));
    memset(s_line, 0, sizeof(s_line));
    memset(s_gsv_source_count, 0, sizeof(s_gsv_source_count));
    memset(s_gsv_source_tick, 0, sizeof(s_gsv_source_tick));
    s_line_index = 0U;
    s_in_sentence = 0U;
}

void GPS_NMEA_FeedByte(uint8_t byte)
{
    s_gps.byte_count++;
    s_gps.last_byte_tick = HAL_GetTick();

    if (byte == '$') {
        s_line_index = 0U;
        s_in_sentence = 1U;
        s_line[s_line_index++] = '$';
        return;
    }
    if (!s_in_sentence || byte == '\r') {
        return;
    }
    if (byte == '\n') {
        if (s_line_index > 0U) {
            s_line[s_line_index] = '\0';
            parse_line(s_line);
        }
        s_line_index = 0U;
        s_in_sentence = 0U;
        return;
    }
    if (s_line_index < GPS_LINE_MAX - 1U) {
        s_line[s_line_index++] = (char)byte;
    } else {
        s_line_index = 0U;
        s_in_sentence = 0U;
        s_gps.parse_error_count++;
    }
}

void GPS_NMEA_Feed(const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if (data == NULL) {
        return;
    }
    for (index = 0U; index < length; index++) {
        GPS_NMEA_FeedByte(data[index]);
    }
}

const GPS_NMEA_Status *GPS_NMEA_GetStatus(void)
{
    update_fix_summary(HAL_GetTick());
    return &s_gps;
}

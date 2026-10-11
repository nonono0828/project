#ifndef CLOUD_LINK_H
#define CLOUD_LINK_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t transport_online;
    uint8_t crc_self_test_ok;
    uint8_t tx_pending;
    uint8_t control_active;
    uint8_t control_sequence_valid;
    uint8_t physical_override_active;
    uint8_t physical_rearm_mask;
    int8_t motion_ry;
    int8_t motion_lx;
    uint32_t rx_bytes;
    uint32_t rx_frames;
    uint32_t command_frames;
    uint32_t command_rejected;
    uint32_t control_sequence_rejected;
    uint32_t physical_override_count;
    uint32_t physical_blocked_count;
    uint32_t crc_error_count;
    uint32_t tail_error_count;
    uint32_t length_error_count;
    uint32_t type_error_count;
    uint32_t json_error_count;
    uint32_t frame_timeout_count;
    uint32_t control_timeout_count;
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t tx_error_count;
    uint32_t last_rx_sequence;
    uint32_t last_control_sequence;
    uint32_t last_tx_sequence;
    uint32_t last_command_tick;
    uint32_t last_tx_tick;
} CloudLinkStatus;

void CloudLink_Init(void);
void CloudLink_SetTransportOnline(uint8_t online);
void CloudLink_Feed(const uint8_t *data, uint16_t length);
void CloudLink_Service(void);
uint16_t CloudLink_TxPeek(const uint8_t **data, uint16_t max_length);
void CloudLink_TxCommit(uint16_t length);
void CloudLink_TxFailed(void);
uint8_t CloudLink_GetMotion(int8_t *right_y, int8_t *left_x);
void CloudLink_SetPhysicalOverride(uint8_t active);
const CloudLinkStatus *CloudLink_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

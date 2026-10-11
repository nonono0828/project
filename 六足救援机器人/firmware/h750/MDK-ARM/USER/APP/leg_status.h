#ifndef LEG_STATUS_H
#define LEG_STATUS_H

#include <stdint.h>

typedef struct
{
	uint32_t tx_attempts;
	uint32_t tx_started;
	uint32_t tx_busy;
	uint32_t tx_error;
	uint32_t tx_completed;
	uint32_t rx_started;
	uint32_t rx_start_error;
	uint32_t rx_completed;
	uint32_t feedback_valid;
	uint32_t feedback_invalid;
} LegLinkStatus;

#ifdef __cplusplus
extern "C" {
#endif

void Leg_GetLinkStatus(uint8_t leg_index, LegLinkStatus *status);

#ifdef __cplusplus
}
#endif

#endif

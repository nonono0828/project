#ifndef PERSON_DETECT_H
#define PERSON_DETECT_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t initialized;
    uint8_t detected;
    GPIO_PinState raw_level;
    uint32_t change_count;
    uint32_t last_change_tick;
} PersonDetectStatus;

void PersonDetect_Init(void);
void PersonDetect_Poll(void);
const PersonDetectStatus *PersonDetect_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif

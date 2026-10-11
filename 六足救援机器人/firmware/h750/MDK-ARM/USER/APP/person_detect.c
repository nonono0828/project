#include "person_detect.h"
#include <string.h>

#define PERSON_DEBOUNCE_MS 30U

static PersonDetectStatus s_person;
static GPIO_PinState s_last_raw;
static uint32_t s_candidate_tick;

void PersonDetect_Init(void)
{
    memset(&s_person, 0, sizeof(s_person));
    s_last_raw =
        HAL_GPIO_ReadPin(AI_PERSON_DETECT_GPIO_Port,
                         AI_PERSON_DETECT_Pin);
    s_person.raw_level = s_last_raw;
    s_person.detected =
        s_last_raw == GPIO_PIN_SET ? 1U : 0U;
    s_person.initialized = 1U;
    s_candidate_tick = HAL_GetTick();
}

void PersonDetect_Poll(void)
{
    GPIO_PinState raw =
        HAL_GPIO_ReadPin(AI_PERSON_DETECT_GPIO_Port,
                         AI_PERSON_DETECT_Pin);
    uint32_t now = HAL_GetTick();

    s_person.raw_level = raw;
    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_candidate_tick = now;
        return;
    }
    if ((now - s_candidate_tick) >= PERSON_DEBOUNCE_MS) {
        uint8_t detected = raw == GPIO_PIN_SET ? 1U : 0U;

        if (detected != s_person.detected) {
            s_person.detected = detected;
            s_person.change_count++;
            s_person.last_change_tick = now;
        }
    }
}

const PersonDetectStatus *PersonDetect_GetStatus(void)
{
    return &s_person;
}

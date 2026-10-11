#include "sensor_task.h"
#include "cmsis_os.h"
#include "env_sensors.h"
#include "gas_alarm.h"
#include "person_detect.h"
#include "peripheral_task.h"

void Sensor_Task(void const *argument)
{
    (void)argument;
    while (!Peripheral_Task_GetStatus()->initialization_complete) {
        osDelay(50U);
    }

    for (;;) {
        EnvSensors_Poll();
        osDelay(500U);
    }
}

void DigitalInput_Task(void const *argument)
{
    (void)argument;
    while (!Peripheral_Task_GetStatus()->initialization_complete) {
        osDelay(20U);
    }

    GasAlarm_Init();
    PersonDetect_Init();
    for (;;) {
        GasAlarm_Poll();
        PersonDetect_Poll();
        osDelay(10U);
    }
}

#include "led_task.h"
#include "cmsis_os.h"
#include "usart.h"
#include "debug_uart.h"


//只是个流水灯任务
extern "C"{
void LED_Task(void const * argument)
{
	while(1)
	{
		HAL_GPIO_TogglePin(LED_GPIO_Port,LED_Pin);
		osDelay(100);
	}
}
}

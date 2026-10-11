#include "ps2_task.h"
#include "cmsis_os.h"
#include "ps2.h"
#include "my_math.h"

/*
PS2遥控器功能：
默认为行走模式：左侧摇杆控制机身高度和转弯，右侧摇杆控制机器人行走方向
点按L1进入位置控制模式：左侧摇杆控制机身高度，右侧摇杆控制机器人XY方向位置
点按L2进入姿态控制模式：左侧摇杆控制yaw轴，右侧摇杆控制pitch轴和roll轴
点按B切换步态模式（依次在2，3，6步态切换）
点按A停止机器人行动，再按一次回复(已取消)
长按Y让机器人机身姿态与位置归零
点按X控制是否开启陀螺仪平衡
*/


//ps2数据接收任务
extern "C"
{	
void ps2_task(void const *argument)
{
	uint32_t last_valid_tick;
	osDelay(1000); //刚上电可能会有波纹，直接初始化会导致初始化失败，故延迟一会再初始化
	PS2_SetInit(); //PS2初始化
	last_valid_tick = HAL_GetTick();
	while(1)
	{ 
		if (PS2_ReadData())
		{
			last_valid_tick = HAL_GetTick();
		}
		else if ((HAL_GetTick() - last_valid_tick) >= 100U)
		{
			PS2_ClearInput();
		}
		osDelay(10);
	}
}
}

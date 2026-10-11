#include "LegControl_task.h"
#include "gait_prg.h"
#include "cmsis_os.h"
#include "leg.h"
#include "main.h"
#include "INS_interface.h"
#include "bsp_dwt.h"
#include "cloud_link.h"
#include "gait_status.h"
#include "obstacle_avoidance.h"
#include "autonomous_drive.h"
#include <cmath>

using namespace std;

// 全局变量
uint32_t LegControl_round; // 机器人回合数
Hexapod hexapod;		   // 机器人结构体

Gait_prg gait_prg;	  // 步态规划
uint32_t round_time;  // 回合时间
Thetas leg_offset[6]; // 腿部关节角偏移，用于将舵机相对机器人本体的角度换算至相对舵机本身的角度

float code_time_ms,code_time_ms_start;

#define AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND 8

static uint8_t autonomous_manual_override_active(const ps2_t &data)
{
	return (data.RX > AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND ||
			data.RX < -AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND ||
			data.RY > AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND ||
			data.RY < -AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND ||
			data.LX > AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND ||
			data.LX < -AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND ||
			data.LY > AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND ||
			data.LY < -AUTONOMOUS_MANUAL_OVERRIDE_DEADBAND) ? 1U : 0U;
}

static float motion_speed_scale(motion_speed_mode_e mode)
{
	switch (mode)
	{
	case MOTION_SPEED_SLOW:
		return 0.60f;
	case MOTION_SPEED_LOW:
		return 0.30f;
	case MOTION_SPEED_NORMAL:
	default:
		return 1.00f;
	}
}

static uint8_t clamp_avoided_axis(float &filtered_value,
								  int8_t desired_output,
								  uint16_t positive_distance,
								  uint16_t negative_distance,
								  float speed_scale)
{
	uint16_t distance;
	float limit;

	if (filtered_value > 0.0001f)
	{
		distance = positive_distance;
	}
	else if (filtered_value < -0.0001f)
	{
		distance = negative_distance;
	}
	else
	{
		return 0U;
	}
	if (distance == 0U || distance >= OBSTACLE_SLOW_MM)
	{
		return 0U;
	}

	if (distance <= OBSTACLE_STOP_MM ||
		desired_output == 0 ||
		(filtered_value > 0.0f && desired_output < 0) ||
		(filtered_value < 0.0f && desired_output > 0))
	{
		limit = 0.0f;
	}
	else
	{
		limit = fabsf(
			0.9f * speed_scale * (float)desired_output);
	}
	if (fabsf(filtered_value) > limit)
	{
		filtered_value =
			filtered_value < 0.0f ? -limit : limit;
		return 1U;
	}
	return 0U;
}

// 函数
static void remote_deal(void);
extern "C"
{
	void LegControl_Task(void const *argument)
	{
		hexapod.Init();
		gait_prg.Init();
		osDelay(100);
		static uint32_t code_time_start, code_time_end, code_time; // 用于计算程序运行时间，保证程序隔一段时间跑一遍
		while (1)
		{
			code_time_start = xTaskGetTickCount(); // 获取当前systick时间
			code_time_ms_start = DWT_GetTimeline_ms();
			hexapod.ins_angle = INS_I.get_euler_angle(); // 获取陀螺仪角度
			remote_deal();
			if (hexapod.velocity.omega >= 0)
				LegControl_round = (++LegControl_round) % N_POINTS; // 控制回合自增长
			else
			{
				if (LegControl_round == 0)
					LegControl_round = N_POINTS - 1;
				else
					LegControl_round--;
			}
			/*步态控制*/
			gait_prg.CEN_and_pace_cal();
			gait_prg.gait_proggraming();
			/*开始移动*/
			round_time = gait_prg.get_pace_time() / N_POINTS; //计算本次运动回合需要的时间
			hexapod.move(round_time);
			//demo
			//hexapod.legs[0].read_angle(2);
			// 计算程序运行时间
			code_time_ms = DWT_GetTimeline_ms() - code_time_ms_start;
			code_time_end = xTaskGetTickCount();		 // 获取当前systick时间
			code_time = code_time_end - code_time_start; // 做差获取程序运行时间（8ms）
			if (code_time < round_time)
				osDelay(round_time - code_time); // 保证程序执行周期等于回合时间
			else
				osDelay(1); // 至少延时1ms
			
		}
	}
}

// 初始化腿部变量并初始化串口，使能串口发送
void Hexapod::Init(void)
{
	legs[0] = Leg(&huart1);
	legs[1] = Leg(&huart2);
	legs[2] = Leg(&huart3);
	legs[3] = Leg(&huart4);
	legs[4] = Leg(&huart5);
	legs[5] = Leg(&huart6);
	MX_USART1_UART_Init();
	MX_USART2_UART_Init();
	MX_USART3_UART_Init();
	MX_UART4_Init();
	MX_UART5_Init();
	MX_USART6_UART_Init();
	//由于运动解算得出的角度是相对于机身而不是电机的，因此需要设置电机角度与相对于机身角度的偏差值
	leg_offset[0] = Thetas(LEG1_JOINT1_OFFSET, LEG_JOINT2_OFFSET, LEG_JOINT3_OFFSET);
	leg_offset[1] = Thetas(LEG2_JOINT1_OFFSET, LEG_JOINT2_OFFSET, LEG_JOINT3_OFFSET);
	leg_offset[2] = Thetas(LEG3_JOINT1_OFFSET, LEG_JOINT2_OFFSET, LEG_JOINT3_OFFSET);
	leg_offset[3] = Thetas(LEG4_JOINT1_OFFSET, LEG_JOINT2_OFFSET, LEG_JOINT3_OFFSET);
	leg_offset[4] = Thetas(LEG5_JOINT1_OFFSET, LEG_JOINT2_OFFSET, LEG_JOINT3_OFFSET);
	leg_offset[5] = Thetas(LEG6_JOINT1_OFFSET, LEG_JOINT2_OFFSET, LEG_JOINT3_OFFSET);
	ins_pid_x.Init(INS_X_PID_KP, INS_X_PID_KI, INS_X_PID_KD, CIR_OFF);
	ins_pid_y.Init(INS_Y_PID_KP, INS_Y_PID_KI, INS_Y_PID_KD, CIR_OFF);
	velocity_fof[0].set_k_filter(VELOCITY_FOF_K);
	velocity_fof[1].set_k_filter(VELOCITY_FOF_K);
	velocity_fof[2].set_k_filter(VELOCITY_FOF_K);
	body_pos_fof[0].set_k_filter(BODY_POS_FOF_K);
	body_pos_fof[1].set_k_filter(BODY_POS_FOF_K);
	body_pos_fof[2].set_k_filter(BODY_POS_FOF_K);
	body_angle_fof[0].set_k_filter(BODY_ANGLE_FOF_K);
	body_angle_fof[1].set_k_filter(BODY_ANGLE_FOF_K);
	body_angle_fof[2].set_k_filter(BODY_ANGLE_FOF_K);
	terrain_mode = TERRAIN_NORMAL;
	terrain_mode_requested = TERRAIN_NORMAL;
	speed_mode = MOTION_SPEED_NORMAL;

	key_body_angle = single_Press.key_init();
	key_body_pos = single_Press.key_init();
	key_gait_mode = single_Press.key_init();
	key_ins_mode = single_Press.key_init();
	key_terrain_mode = single_Press.key_init();
	key_speed_mode = single_Press.key_init();
	key_autonomous = single_Press.key_init();
	ObstacleAvoidance_Init(1U);
	AutonomousDrive_Init();
}

// 计算机器人速度
void Hexapod::velocity_cal(const ps2_t &remote_data)
{
	const ObstacleAvoidanceStatus *avoidance =
		ObstacleAvoidance_GetStatus();
	float speed_scale = motion_speed_scale(speed_mode);
	float speed_limit = MAX_SPEED * speed_scale;
	float speed_norm;

	if (this->ctrl_mode != HEXAPOD_MOVE) // 若不是行走模式则速度为0
	{
		velocity.Vx = 0;
		velocity.Vy = 0;
		velocity.omega = 0;
	}
	else
	{
		velocity.Vx =
			velocity_fof[0].cal(0.9f * speed_scale * remote_data.RX);
		velocity.Vy =
			velocity_fof[1].cal(0.9f * speed_scale * remote_data.RY);
		velocity.omega =
			velocity_fof[2].cal(-0.9f * speed_scale * remote_data.LX);

		speed_norm = sqrtf(
			velocity.Vx * velocity.Vx +
			velocity.Vy * velocity.Vy +
			velocity.omega * velocity.omega);
		if (speed_norm > speed_limit && speed_norm > 0.0001f)
		{
			float limit_scale = speed_limit / speed_norm;
			velocity.Vx *= limit_scale;
			velocity.Vy *= limit_scale;
			velocity.omega *= limit_scale;
		}
		if (avoidance->enabled && avoidance->radar_ready)
		{
			float turn_axis = -velocity.omega;

			if (clamp_avoided_axis(
					velocity.Vx, avoidance->output_rx,
					avoidance->right_mm, avoidance->left_mm,
					speed_scale))
			{
				velocity_fof[0].reset(velocity.Vx);
			}
			if (clamp_avoided_axis(
					velocity.Vy, avoidance->output_ry,
					avoidance->front_mm, avoidance->rear_mm,
					speed_scale))
			{
				velocity_fof[1].reset(velocity.Vy);
			}
			if (clamp_avoided_axis(
					turn_axis, avoidance->output_lx,
					avoidance->right_mm, avoidance->left_mm,
					speed_scale))
			{
				velocity.omega = -turn_axis;
				velocity_fof[2].reset(velocity.omega);
			}
		}
	}
	if (velocity.Vx > -0.0001f && velocity.Vx < 0.0001f)
		velocity.Vx = 0;
	if (velocity.Vy > -0.0001f && velocity.Vy < 0.0001f)
		velocity.Vy = 0;
	if (velocity.omega > -0.0001f && velocity.omega < 0.0001f)
		velocity.omega = 0;
	gait_prg.set_velocity(velocity);
}

void Hexapod::body_position_cal(const ps2_t &remote_data)
{
	if (this->ctrl_mode != HEXAPOD_BODY_ANGEL_CONTROL) // 除了姿态控制模式，其他情况下都能控制z轴高度
		body_pos.z += BODY_POS_SENSI * remote_data.LY;
	if (this->ctrl_mode == HEXAPOD_BODY_POS_CONTROL) // 若是身体位置控制模式则计算xy位置
	{
		body_pos.y = HEXAPOD_MAX_Y / 128.0f * remote_data.RY;
		body_pos.x = -HEXAPOD_MIN_X / 128.0f * remote_data.RX;
	}
	// 限制数值
	value_limit(body_pos.z, HEXAPOD_MIN_HEIGHT, HEXAPOD_MAX_HEIGHT);
	value_limit(body_pos.y, HEXAPOD_MIN_Y, HEXAPOD_MAX_Y);
	value_limit(body_pos.x, HEXAPOD_MIN_X, HEXAPOD_MAX_X);
	// 一阶低通滤波
	body_pos.y = body_pos_fof[1].cal(body_pos.y);
	body_pos.x = body_pos_fof[2].cal(body_pos.x);
	gait_prg.set_body_position(body_pos);
}

void Hexapod::body_angle_cal(const ps2_t &remote_data)
{
	static Position3 body_angle_temp, ins_angle_diff;
	
	if (this->ins_sw == INS_ON)
	{
		if (this->ctrl_mode == HEXAPOD_BODY_ANGEL_CONTROL)  //在姿态控制模式下用摇杆控制姿态
		{
			ins_angle_set.x += BODY_INS_ANGLE_SENSI_X * remote_data.RY;
			ins_angle_set.y += BODY_INS_ANGLE_SENSI_Y * remote_data.RX;
			ins_angle_set.z -= BODY_INS_ANGLE_SENSI_Z * remote_data.LX;
			ins_angle_set.z = loop_constrain(ins_angle_set.z, -180, 180); // 限制角度在-180到180度
		}
		else
			ins_angle_set.z = ins_angle.z; // 其他模式下不管z轴，xy轴做闭环

		ins_angle_diff = ins_angle_set - ins_angle;
		ins_angle_diff.z = loop_constrain(ins_angle_diff.z,-180,180);
		body_angle_temp = body_angle - ins_angle_diff;

		body_angle_fof[0].set_k_filter(2*BODY_ANGLE_FOF_K);
		body_angle_fof[1].set_k_filter(2*BODY_ANGLE_FOF_K);  //降低响应时间
		body_angle_fof[2].set_k_filter(1.5f*BODY_ANGLE_FOF_K);
	}
	else if (this->ctrl_mode == HEXAPOD_BODY_ANGEL_CONTROL) // 非陀螺仪模式且为机身控制模式
	{
		body_angle_temp.x = -BODY_ANGLE_SENSI_X * remote_data.RY;
		body_angle_temp.y = -BODY_ANGLE_SENSI_Y * remote_data.RX;
		body_angle_temp.z = BODY_ANGLE_SENSI_Z * remote_data.LX;
		
		body_angle_fof[0].set_k_filter(BODY_ANGLE_FOF_K);
		body_angle_fof[1].set_k_filter(BODY_ANGLE_FOF_K);  //相当于启动滤波器
		body_angle_fof[2].set_k_filter(BODY_ANGLE_FOF_K);
	}

	body_angle.x = body_angle_fof[0].cal(body_angle_temp.x);
	body_angle.y = body_angle_fof[1].cal(body_angle_temp.y);
	body_angle.z = body_angle_fof[2].cal(body_angle_temp.z);
	if (this->ins_sw == INS_ON) // 若是陀螺仪控制则限制陀螺仪设置角度
	{
		if (value_limit(body_angle.x, HEXAPOD_MIN_X_ROTATE, HEXAPOD_MAX_X_ROTATE))
			ins_angle_set.x = ins_angle.x;
		if (value_limit(body_angle.y, HEXAPOD_MIN_Y_ROTATE, HEXAPOD_MAX_Y_ROTATE))
			ins_angle_set.y = ins_angle.y;
		if (value_limit(body_angle.z, HEXAPOD_MIN_Z_ROTATE, HEXAPOD_MAX_Z_ROTATE))
			ins_angle_set.z = ins_angle.z;
	}
	else
	{
		value_limit(body_angle.x, HEXAPOD_MIN_X_ROTATE, HEXAPOD_MAX_X_ROTATE);
		value_limit(body_angle.y, HEXAPOD_MIN_Y_ROTATE, HEXAPOD_MAX_Y_ROTATE);
		value_limit(body_angle.z, HEXAPOD_MIN_Z_ROTATE, HEXAPOD_MAX_Z_ROTATE);
	}

	body_angle_temp = body_angle / 180.f * PI; // 转换为弧度制后设置机体角度
	gait_prg.set_body_rotate_angle(body_angle_temp);
}

/*
 *@brief 检查遥控器数据，符合要求就归零
 *@param remote_data 遥控数据
 */
void Hexapod::body_angle_and_pos_zero(const ps2_t &remote_data)
{
	if (remote_data.CIRCLE == 1) //按下此按键归零
	{
		this->ins_angle_set = this->ins_angle;
		this->body_angle.zero();
		this->body_pos.zero();
		gait_prg.set_body_rotate_angle(body_angle);
		gait_prg.set_body_position(body_pos);
	}
}

void Hexapod::mode_select(const ps2_t &remote_data)
{
	if (single_Press.if_single_press(key_body_pos, remote_data.L1))
	{
		if (ctrl_mode != HEXAPOD_BODY_POS_CONTROL)
			ctrl_mode = HEXAPOD_BODY_POS_CONTROL;
		else
			ctrl_mode = HEXAPOD_MOVE;
	}
	else if (single_Press.if_single_press(key_body_angle, remote_data.L2))
	{
		if (ctrl_mode != HEXAPOD_BODY_ANGEL_CONTROL)
			ctrl_mode = HEXAPOD_BODY_ANGEL_CONTROL;
		else
			ctrl_mode = HEXAPOD_MOVE;
	}
	if (single_Press.if_single_press(key_ins_mode, remote_data.SQUARE)&&INS_I.check_if_init_success()==true)
	{
		this->ins_sw = (INS_SW_e) !(this->ins_sw);
		if (this->ins_sw == INS_ON)				   // 刚进入陀螺仪模式
			this->ins_angle_set = this->ins_angle; // 将设定角度等于陀螺仪角度
	}
}

void Hexapod::gait_mode_switch(const ps2_t &remote_data)
{
	if (single_Press.if_single_press(key_gait_mode, remote_data.TRIANGLE))
	{
		this->gait_mode = (gait_mode_e)(((uint8_t)this->gait_mode + 1) % 3); //轮番切换步态
	}
	gait_prg.gait_mode = this->gait_mode;
}

void Hexapod::terrain_mode_switch(const ps2_t &remote_data)
{
	if (single_Press.if_single_press(key_terrain_mode, remote_data.R1))
	{
		terrain_mode_requested =
			(terrain_mode_requested == TERRAIN_NORMAL) ?
			TERRAIN_OFFROAD : TERRAIN_NORMAL;
	}

	if (terrain_mode != terrain_mode_requested && LegControl_round == 0U)
	{
		terrain_mode = terrain_mode_requested;
		gait_prg.set_offroad_mode(terrain_mode == TERRAIN_OFFROAD);
	}
}

void Hexapod::speed_mode_switch(const ps2_t &remote_data)
{
	if (single_Press.if_single_press(key_speed_mode, remote_data.R2))
	{
		speed_mode = (motion_speed_mode_e)(
			((uint8_t)speed_mode + 1U) %
			(uint8_t)MOTION_SPEED_MODE_COUNT);
	}
}

void Hexapod::autonomous_switch(const ps2_t &remote_data)
{
	if (single_Press.if_single_press(key_autonomous, remote_data.START))
	{
		AutonomousDrive_Toggle();
	}
}

extern "C" uint8_t Hexapod_GetTerrainMode(void)
{
	return (uint8_t)hexapod.terrain_mode;
}

extern "C" uint8_t Hexapod_GetSpeedMode(void)
{
	return (uint8_t)hexapod.speed_mode;
}

extern "C" uint8_t Hexapod_GetSpeedPercent(void)
{
	switch (hexapod.speed_mode)
	{
	case MOTION_SPEED_SLOW:
		return 60U;
	case MOTION_SPEED_LOW:
		return 30U;
	case MOTION_SPEED_NORMAL:
	default:
		return 100U;
	}
}

ps2_t remote_data;
static void remote_deal(void)
{
	ps2_t motion_data;
	int8_t cloud_right_y = 0;
	int8_t cloud_left_x = 0;
	uint8_t physical_motion_active;
	uint8_t autonomous_manual_override;
	uint8_t cloud_motion_active = 0U;

	remote_data = ps2_getvalue();
	motion_data = remote_data;
	physical_motion_active =
		(remote_data.RX != 0 || remote_data.RY != 0 ||
		 remote_data.LX != 0 || remote_data.LY != 0) ? 1U : 0U;
	autonomous_manual_override =
		autonomous_manual_override_active(remote_data);
	CloudLink_SetPhysicalOverride(physical_motion_active);
	if (!physical_motion_active &&
		CloudLink_GetMotion(&cloud_right_y, &cloud_left_x))
	{
		/* Cloud commands emulate only the walking RY and turning LX axes. */
		motion_data.RX = 0;
		motion_data.RY = cloud_right_y;
		motion_data.LX = cloud_left_x;
		cloud_motion_active = 1U;
	}
	hexapod.mode_select(remote_data);
	hexapod.speed_mode_switch(remote_data);
	hexapod.autonomous_switch(remote_data);
	AutonomousDrive_Update(
		autonomous_manual_override || cloud_motion_active,
		&motion_data.RX, &motion_data.RY, &motion_data.LX);
	ObstacleAvoidance_Apply(
		&motion_data.RX, &motion_data.RY, &motion_data.LX);
	hexapod.velocity_cal(motion_data);
	hexapod.body_angle_cal(remote_data);
	hexapod.body_position_cal(remote_data);
	hexapod.body_angle_and_pos_zero(remote_data);
	hexapod.gait_mode_switch(remote_data);
	hexapod.terrain_mode_switch(remote_data);
}

/*
 * @brief 让机器人动起来
 * @param round_time 回合时间，单位ms
 */
void Hexapod::move(uint32_t round_time)
{
	Thetas theta_temp;
	for (int i = 0; i < 6; i++)
	{
		theta_temp = (gait_prg.actions[i].thetas[LegControl_round]) - leg_offset[i];
		theta_temp.angle[0] = loop_constrain(theta_temp.angle[0],-PI,PI);  //循环限幅
		legs[i].set_thetas(theta_temp); // 设置机械腿角度
		legs[i].set_time(round_time);
		legs[i].move_read();
	}
}

extern "C" 
void legs_Tx_Callback(UART_HandleTypeDef *huart)
{
	Leg *leg = 0;
	HAL_StatusTypeDef receive_status;
	switch ((uint32_t)(huart->Instance))
	{
	case (uint32_t)USART1:
		leg = &hexapod.legs[0];
		break;
	case (uint32_t)USART2:
		leg = &hexapod.legs[1];
		break;
	case (uint32_t)USART3:
		leg = &hexapod.legs[2];
		break;
	case (uint32_t)UART4:
		leg = &hexapod.legs[3];
		break;
	case (uint32_t)UART5:
		leg = &hexapod.legs[4];
		break;
	case (uint32_t)USART6:
		leg = &hexapod.legs[5];
		break;
	default:
		return;
	}
	leg->mark_transmit_complete();
	leg->prepare_feedback_receive();
	HAL_HalfDuplex_EnableReceiver(huart);
	receive_status = HAL_UART_Receive_IT(
		huart, leg->get_reciv_buffer_p(), RECV_SERVO_POS_READ_LEN + 3);
	leg->record_feedback_receive_start(receive_status);
}

extern "C"
void legs_Rx_Callback(UART_HandleTypeDef *huart)
{
	switch ((uint32_t)(huart->Instance))
	{
	case (uint32_t)USART1:
		hexapod.legs[0].mark_feedback_received();
		break;
	case (uint32_t)USART2:
		hexapod.legs[1].mark_feedback_received();
		break;
	case (uint32_t)USART3:
		hexapod.legs[2].mark_feedback_received();
		break;
	case (uint32_t)UART4:
		hexapod.legs[3].mark_feedback_received();
		break;
	case (uint32_t)UART5:
		hexapod.legs[4].mark_feedback_received();
		break;
	case (uint32_t)USART6:
		hexapod.legs[5].mark_feedback_received();
		break;
	default:
		break;
	}
}

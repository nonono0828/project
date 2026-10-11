#ifndef LEG_H
#define LEG_H

#include "Servo.h"
#include "usart.h"
#include "my_math.h"
#include "leg_status.h"

class Leg
{
private:
	uint8_t send_buffer[100]; //发送缓存
	volatile uint8_t reciv_buffer[20]; //接收缓存
	Servo servos[3];
	Thetas theta_set;
	Thetas theta;
	Servo_Broad_Cast servo_broad_cast;
	UART_HandleTypeDef *huart;
	volatile uint8_t feedback_ready;
	void TX_Enable();
	void RX_Enable();
	void TX_Unable();
	bool process_feedback();
public:
	Leg(UART_HandleTypeDef *huart); // 构造函数
	Leg(){huart = 0; feedback_ready = 0U;};	// 无参构造
	void set_thetas(Thetas thetas); // 设置机械腿的角度
	void set_time(uint16_t tims);	// 设置机械腿移动的时间
	void move();					// 机械腿移动命令
	void move_wait();				// 设置机械腿角度，但需要等待开始命令才移动
	void move_start();				// 让机械腿开始运动
	void load();					// 上电
	void unload();					// 掉电
	void read_angle(uint32_t id);	// 读取舵机角度
	void move_read();       //设置角度后获取角度
	uint8_t* get_reciv_buffer_p();  //获取接收缓存指针
	void prepare_feedback_receive();
	void record_feedback_receive_start(HAL_StatusTypeDef status);
	void mark_transmit_complete();
	void mark_feedback_received();
};



#endif

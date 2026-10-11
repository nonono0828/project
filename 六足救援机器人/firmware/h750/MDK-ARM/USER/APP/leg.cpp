#include "leg.h"
#include "stdlib.h"
#include "cmsis_os.h"
#include "main.h"
#include "usart.h"
#include "bsp.h"

// 全局变量
extern uint32_t LegControl_round; // 控制回合

static volatile LegLinkStatus s_leg_link_status[6];

static int8_t leg_link_index(const UART_HandleTypeDef *huart)
{
	switch ((uint32_t)huart->Instance)
	{
	case (uint32_t)USART1:
		return 0;
	case (uint32_t)USART2:
		return 1;
	case (uint32_t)USART3:
		return 2;
	case (uint32_t)UART4:
		return 3;
	case (uint32_t)UART5:
		return 4;
	case (uint32_t)USART6:
		return 5;
	default:
		return -1;
	}
}

extern "C" void Leg_GetLinkStatus(uint8_t leg_index, LegLinkStatus *status)
{
	uint32_t primask;

	if (leg_index >= 6U || status == 0)
		return;

	primask = __get_PRIMASK();
	__disable_irq();
	status->tx_attempts = s_leg_link_status[leg_index].tx_attempts;
	status->tx_started = s_leg_link_status[leg_index].tx_started;
	status->tx_busy = s_leg_link_status[leg_index].tx_busy;
	status->tx_error = s_leg_link_status[leg_index].tx_error;
	status->tx_completed = s_leg_link_status[leg_index].tx_completed;
	status->rx_started = s_leg_link_status[leg_index].rx_started;
	status->rx_start_error = s_leg_link_status[leg_index].rx_start_error;
	status->rx_completed = s_leg_link_status[leg_index].rx_completed;
	status->feedback_valid = s_leg_link_status[leg_index].feedback_valid;
	status->feedback_invalid = s_leg_link_status[leg_index].feedback_invalid;
	__set_PRIMASK(primask);
}

Leg::Leg(UART_HandleTypeDef *huart)
{
	this->huart = huart;
	this->feedback_ready = 0U;
	servos[0] = Servo(1);
	servos[1] = Servo(2);
	servos[2] = Servo(3);
}

void Leg::set_thetas(Thetas theta)
{
	this->theta_set.angle[0] = theta.angle[0];
	this->theta_set.angle[1] = theta.angle[1];
	this->theta_set.angle[2] = -theta.angle[2];  // 第三关节方向反转
	this->servos[0].set_angle(theta.angle[0]);
	this->servos[1].set_angle(theta.angle[1]);
	this->servos[2].set_angle(-theta.angle[2]);  // 第三关节方向反转
}

void Leg::set_time(uint16_t move_time)
{
	servos[0].set_time(move_time);
	servos[1].set_time(move_time);
	servos[2].set_time(move_time);
}

void Leg::move()
{
	this->TX_Enable();
	this->servos[0].move(this->send_buffer);
	this->servos[1].move(this->send_buffer + SERVO_MOVE_TIME_WRITE_LEN + 3);
	this->servos[2].move(this->send_buffer + (SERVO_MOVE_TIME_WRITE_LEN + 3) * 2);
	HAL_UART_Transmit_DMA(this->huart, this->send_buffer, (SERVO_MOVE_TIME_WRITE_LEN + 3) * 3);
}

void Leg::move_wait()
{
	this->TX_Enable();
	this->servos[0].move_wait(this->send_buffer);
	this->servos[1].move_wait(this->send_buffer + SERVO_MOVE_TIME_WAIT_WRITE_LEN + 3);
	this->servos[2].move(this->send_buffer + (SERVO_MOVE_TIME_WAIT_WRITE_LEN + 3) * 2);
	HAL_UART_Transmit_DMA(this->huart, this->send_buffer, (SERVO_MOVE_TIME_WAIT_WRITE_LEN + 3) * 3);
}

void Leg::move_start()
{
	this->TX_Enable();
	this->servo_broad_cast.move_start(this->send_buffer);
	HAL_UART_Transmit_DMA(this->huart, this->send_buffer, SERVO_MOVE_START_LEN + 3);
}

void Leg::load()
{
	this->servo_broad_cast.load(this->send_buffer);
	HAL_UART_Transmit_DMA(this->huart, this->send_buffer, SERVO_LOAD_OR_UNLOAD_WRITE_LEN + 3);
}

void Leg::unload()
{
	this->servo_broad_cast.unload(this->send_buffer);
	HAL_UART_Transmit_DMA(this->huart, this->send_buffer, SERVO_LOAD_OR_UNLOAD_WRITE_LEN + 3);
}

void Leg::read_angle(uint32_t id)
{
	if (id < 1U || id > 3U)
		return;
	(void)this->process_feedback();
	if (this->huart->RxState != HAL_UART_STATE_READY)
		HAL_UART_AbortReceive(this->huart);
	this->TX_Enable();
	this->servos[id - 1].read_angle(this->send_buffer);

	HAL_UART_Transmit(this->huart, this->send_buffer, SERVO_POS_READ_LEN + 3, 1000); // 为了在发送完成后再开启接收，这里不使用DMA
																					 // while (__HAL_UART_GET_FLAG(this->huart, UART_FLAG_TC))
	;																				 // 等待串口发送完成

	this->RX_Enable(); // 使能接收
	this->prepare_feedback_receive();
	HAL_UART_Receive_IT(this->huart, this->get_reciv_buffer_p(),
						RECV_SERVO_POS_READ_LEN + 3);
}


void Leg::move_read()
{
	int8_t link_index = leg_link_index(this->huart);
	HAL_StatusTypeDef tx_status;
	uint8_t *temp_p = this->send_buffer;
	(void)this->process_feedback();
	if (this->huart->RxState != HAL_UART_STATE_READY)
		HAL_UART_AbortReceive(this->huart);
	HAL_HalfDuplex_EnableTransmitter(this->huart);
  
	this->servos[0].move(temp_p);
	temp_p += SERVO_MOVE_TIME_WRITE_LEN + 3;
	this->servos[1].move(temp_p);
	temp_p += SERVO_MOVE_TIME_WRITE_LEN + 3;
	this->servos[2].move(temp_p);
	temp_p += (SERVO_MOVE_TIME_WRITE_LEN + 3);
	this->servos[LegControl_round % 3].read_angle(temp_p); // 每三个回合读取一次角度
	temp_p += SERVO_POS_READ_LEN + 3;
	if (link_index >= 0)
		s_leg_link_status[link_index].tx_attempts++;
	tx_status = HAL_UART_Transmit_DMA(
		this->huart, this->send_buffer, temp_p - this->send_buffer);
	if (link_index >= 0)
	{
		if (tx_status == HAL_OK)
			s_leg_link_status[link_index].tx_started++;
		else if (tx_status == HAL_BUSY)
			s_leg_link_status[link_index].tx_busy++;
		else
			s_leg_link_status[link_index].tx_error++;
	}
}

uint8_t *Leg::get_reciv_buffer_p()
{
	return (uint8_t *)reciv_buffer;
}

void Leg::prepare_feedback_receive()
{
	feedback_ready = 0U;
}

void Leg::record_feedback_receive_start(HAL_StatusTypeDef status)
{
	int8_t link_index = leg_link_index(this->huart);

	if (link_index < 0)
		return;
	if (status == HAL_OK)
		s_leg_link_status[link_index].rx_started++;
	else
		s_leg_link_status[link_index].rx_start_error++;
}

void Leg::mark_transmit_complete()
{
	int8_t link_index = leg_link_index(this->huart);

	if (link_index >= 0)
		s_leg_link_status[link_index].tx_completed++;
}

void Leg::mark_feedback_received()
{
	int8_t link_index = leg_link_index(this->huart);

	feedback_ready = 1U;
	if (link_index >= 0)
		s_leg_link_status[link_index].rx_completed++;
}

bool Leg::process_feedback()
{
	uint8_t servo_id;
	uint16_t raw_angle;

	if (feedback_ready == 0U)
		return false;
	feedback_ready = 0U;

	if (reciv_buffer[0] != 0x55U || reciv_buffer[1] != 0x55U ||
		reciv_buffer[3] != RECV_SERVO_POS_READ_LEN ||
		reciv_buffer[4] != SERVO_POS_READ)
	{
		int8_t link_index = leg_link_index(this->huart);
		if (link_index >= 0)
			s_leg_link_status[link_index].feedback_invalid++;
		return false;
	}

	servo_id = reciv_buffer[2];
	if (servo_id < 1U || servo_id > 3U ||
		check_sum(reciv_buffer) != reciv_buffer[RECV_SERVO_POS_READ_LEN + 2])
	{
		int8_t link_index = leg_link_index(this->huart);
		if (link_index >= 0)
			s_leg_link_status[link_index].feedback_invalid++;
		return false;
	}

	raw_angle = (uint16_t)(((uint16_t)reciv_buffer[6] << 8) |
						   (uint16_t)reciv_buffer[5]);
	this->theta.angle[servo_id - 1U] =
		((float)raw_angle - 500.0f) / 750.0f * 180.0f;
	this->servos[servo_id - 1U].set_real_angle(raw_angle);
	{
		int8_t link_index = leg_link_index(this->huart);
		if (link_index >= 0)
			s_leg_link_status[link_index].feedback_valid++;
	}
	return true;
}

// 使能接收
void Leg::RX_Enable()
{
	switch ((uint32_t)(this->huart->Instance))
	{
	case (uint32_t)USART1:
		__LEG1_RXEN();
		break;
	case (uint32_t)USART2:
		__LEG2_RXEN();
		break;
	case (uint32_t)USART3:
		__LEG3_RXEN();
		break;
	case (uint32_t)UART4:
		__LEG4_RXEN();
		break;
	case (uint32_t)UART5:
		__LEG5_RXEN();
		break;
	case (uint32_t)USART6:
		__LEG6_RXEN();
		break;
	default:
		break;
	}
}

// 使能发送
void Leg::TX_Enable()
{
	switch ((uint32_t)(this->huart->Instance))
	{
	case (uint32_t)USART1:
		__LEG1_TXEN();
		break;
	case (uint32_t)USART2:
		__LEG2_TXEN();
		break;
	case (uint32_t)USART3:
		__LEG3_TXEN();
		break;
	case (uint32_t)UART4:
		__LEG4_TXEN();
		break;
	case (uint32_t)UART5:
		__LEG5_TXEN();
		break;
	case (uint32_t)USART6:
		__LEG6_TXEN();
		break;
	default:
		break;
	}
}

void Leg::TX_Unable()
{
	switch ((uint32_t)(this->huart->Instance))
	{
	case (uint32_t)USART1:
		__LEG1_TXUEN();
		break;
	case (uint32_t)USART2:
		__LEG2_TXUEN();
		break;
	case (uint32_t)USART3:
		__LEG3_TXUEN();
		break;
	case (uint32_t)UART4:
		__LEG4_TXUEN();
		break;
	case (uint32_t)UART5:
		__LEG5_TXUEN();
		break;
	case (uint32_t)USART6:
		__LEG6_TXUEN();
		break;
	default:
		break;
	}
}


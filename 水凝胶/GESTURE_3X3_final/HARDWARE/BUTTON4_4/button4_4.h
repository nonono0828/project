#ifndef __BUTTON4_4_H
#define	__BUTTON4_4_H
#include "stm32f10x.h"
#include "delay.h"
#include "math.h"

/*****************辰哥单片机设计******************
											STM32
 * 文件			:	4×4矩阵键盘h文件                   
 * 版本			: V1.0
 * 日期			: 2024.9.5
 * MCU			:	STM32F103C8T6
 * 接口			:	见代码										
 * IP账号		:	辰哥单片机设计（同BILIBILI|抖音|快手|小红书|CSDN|公众号|视频号等）
 * 作者			:	辰哥 
 * 工作室		: 异方辰电子工作室
 * 讲解视频	:	https://www.bilibili.com/video/BV1fbpgeVEDJ/?share_source=copy_web
 * 官方网站	:	www.yfcdz.cn

**********************BEGIN***********************/

/***************根据自己需求更改****************/
// 4×4矩阵键盘 GPIO宏定义

#define		BUTTON_GPIO_CLK											RCC_APB2Periph_GPIOA
#define 	BUTTON_ROW1_GPIO_PORT								GPIOA
#define 	BUTTON_ROW1_GPIO_PIN							  GPIO_Pin_0	
#define 	BUTTON_ROW2_GPIO_PORT								GPIOA
#define 	BUTTON_ROW2_GPIO_PIN								GPIO_Pin_1
#define 	BUTTON_ROW3_GPIO_PORT								GPIOA
#define 	BUTTON_ROW3_GPIO_PIN								GPIO_Pin_2
#define 	BUTTON_ROW4_GPIO_PORT								GPIOA
#define 	BUTTON_ROW4_GPIO_PIN								GPIO_Pin_3

#define 	BUTTON_COL1_GPIO_PORT								GPIOA
#define 	BUTTON_COL1_GPIO_PIN								GPIO_Pin_4
#define 	BUTTON_COL2_GPIO_PORT								GPIOA
#define 	BUTTON_COL2_GPIO_PIN								GPIO_Pin_5
#define 	BUTTON_COL3_GPIO_PORT								GPIOA
#define 	BUTTON_COL3_GPIO_PIN								GPIO_Pin_6
#define 	BUTTON_COL4_GPIO_PORT								GPIOA
#define 	BUTTON_COL4_GPIO_PIN								GPIO_Pin_7

	
/*********************END**********************/

void Button4_4_Init(void);
int Button4_4_Scan(void);


#endif


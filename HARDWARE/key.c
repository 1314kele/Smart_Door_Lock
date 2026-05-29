#include "stm32f4xx.h"
#include "key.h"
#include "led.h"
#include "sys.h"

#include "FreeRTOS.h"
#include "task.h"


//按键初始化
void key_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	//1.使能时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA|RCC_AHB1Periph_GPIOE,ENABLE);
	
	//2.初始化GPIO PA0
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;//输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;//PA0
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;//输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4;//PE2 PE3 PE4
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_Init(GPIOE,&GPIO_InitStructure);
}

//外部中断初始化
void exti_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	//1.使能时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA|RCC_AHB1Periph_GPIOE,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG,ENABLE);
	
	//2.初始化GPIO为中断功能
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;//输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;//PA0
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_Init(GPIOA,&GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;//输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4;//PE2 PE3 PE4
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_Init(GPIOE,&GPIO_InitStructure);
	
	//3.映射IO口和外部中断线 PA0------------EXTI0
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA,EXTI_PinSource0);
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOE,EXTI_PinSource2);
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOE,EXTI_PinSource3);
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOE,EXTI_PinSource4);
	
	//4.初始化外部中断
	EXTI_InitStructure.EXTI_Line = EXTI_Line0|EXTI_Line2|EXTI_Line3|EXTI_Line4;//外部中断0 2 3 4
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;//中断模式
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;//下降沿触发
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;//使能中断
	EXTI_Init(&EXTI_InitStructure);
	
	//5.初始化NVIC
	NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;//外部中断0通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x8;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;//响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;//使能中断
	NVIC_Init(&NVIC_InitStructure);
	
	NVIC_InitStructure.NVIC_IRQChannel = EXTI2_IRQn;//外部中断2通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x7;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;//响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;//使能中断
	NVIC_Init(&NVIC_InitStructure);
	
	NVIC_InitStructure.NVIC_IRQChannel = EXTI3_IRQn;//外部中断3通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x7;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x1;//响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;//使能中断
	NVIC_Init(&NVIC_InitStructure);
	
	NVIC_InitStructure.NVIC_IRQChannel = EXTI4_IRQn;//外部中断4通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x7;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;//响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;//使能中断
	NVIC_Init(&NVIC_InitStructure);
}


//6.外部中断0通道中断处理函数
void EXTI0_IRQHandler(void)
{
	uint32_t ulReturn;
    
    /* 进入临界区，防止高优先级中断打断 */
    ulReturn = taskENTER_CRITICAL_FROM_ISR(); 
	
	//判断某个中断源是否触发
	if(EXTI_GetITStatus(EXTI_Line0)==SET){
		//中断处理需要做的事
//		PFout(9) = 0;
//		delay(2000);
//		PFout(9) = 1;
		
//		TIM_SetCompare1(TIM3,100);
		
		//清除中断标志
		EXTI_ClearITPendingBit(EXTI_Line0);
	}
	
	/* 退出临界区 */
    taskEXIT_CRITICAL_FROM_ISR( ulReturn );
}


void EXTI2_IRQHandler(void)
{
	//判断某个中断源是否触发
	if(EXTI_GetITStatus(EXTI_Line2)==SET){
		//中断处理需要做的事
//		PFout(10) = 0;
//		delay(2000);
//		PFout(10) = 1;
		//启动定时器
//		TIM_Cmd(TIM3,ENABLE);
		D1 = ~D1;
		TIM_SetCompare1(TIM3,500);
		
		//清除中断标志
		EXTI_ClearITPendingBit(EXTI_Line2);
	}
}

void EXTI3_IRQHandler(void)
{
	//判断某个中断源是否触发
	if(EXTI_GetITStatus(EXTI_Line3)==SET){
		//中断处理需要做的事
//		PEout(13) = 0;
//		delay(2000);
//		PEout(13) = 1;
		
		TIM_SetCompare1(TIM3,800);
		
		//清除中断标志
		EXTI_ClearITPendingBit(EXTI_Line3);
	}
}

void EXTI4_IRQHandler(void)
{
	//判断某个中断源是否触发
	if(EXTI_GetITStatus(EXTI_Line4)==SET){
		//中断处理需要做的事
		//PEout(14) = ~PEout(14);
		TIM_SetCompare1(TIM3,990);
		
		//清除中断标志
		EXTI_ClearITPendingBit(EXTI_Line4);
	}
}

#include "stm32f4xx.h"
#include "timer.h"
#include "led.h"

void timer2_init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	//1.开启timer2时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);

	//2.初始化定时器配置(周期)
	//1s ---- 84M / 8400 / 10000 = 1s
	TIM_TimeBaseInitStructure.TIM_Prescaler = 8400-1;//8400分频
	TIM_TimeBaseInitStructure.TIM_Period = 10000-1;//重装载值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;//时钟因子
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
	
	//3.配置定时器超时中断
	//中断源
	TIM_ITConfig(TIM2,TIM_IT_Update,ENABLE);
	//中断控制器
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;//定时器2通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x9;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;//响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;//使能中断
	NVIC_Init(&NVIC_InitStructure);
	
	//4.启动定时器
	TIM_Cmd(TIM2,ENABLE);
}

void timer3_init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	//1.开启timer2时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);

	//2.初始化定时器配置(周期)
	//10ms ---- 84M / 840 / 1000 = 100Hz
	TIM_TimeBaseInitStructure.TIM_Prescaler = 840-1;//8400分频
	TIM_TimeBaseInitStructure.TIM_Period = 4000-1;//重装载值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;//时钟因子
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
	
	//3.配置定时器超时中断
	//中断源
	TIM_ITConfig(TIM3,TIM_IT_Update,ENABLE);
	//中断控制器
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;//定时器2通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x9;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;//响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;//使能中断
	NVIC_Init(&NVIC_InitStructure);
	
	//4.关闭定时器
	TIM_Cmd(TIM3,DISABLE);
}

void timer9_init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	//1.开启timer9时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM9,ENABLE);

	//2.初始化定时器配置(周期)
	//200ms ---- 168M / 16800 / 2000 = 5Hz = 200ms
	TIM_TimeBaseInitStructure.TIM_Prescaler = 16800-1;//8400分频
	TIM_TimeBaseInitStructure.TIM_Period = 2000-1;//重装载值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;//时钟因子
	TIM_TimeBaseInit(TIM9, &TIM_TimeBaseInitStructure);
	
	//3.配置定时器超时中断
	//中断源
	TIM_ITConfig(TIM9,TIM_IT_Update,ENABLE);
	//中断控制器
	NVIC_InitStructure.NVIC_IRQChannel = TIM1_BRK_TIM9_IRQn;//定时器9通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x9;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;//响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;//使能中断
	NVIC_Init(&NVIC_InitStructure);
	
	//4.启动定时器
	TIM_Cmd(TIM9,ENABLE);
}

//定时器2中断处理函数
void TIM2_IRQHandler(void)
{
	//判断更新中断是否触发
	if(TIM_GetITStatus(TIM2,TIM_IT_Update)==SET){
		//中断程序需要完成的工作
		D1 = ~D1;
		
		//清除中断标志
		TIM_ClearITPendingBit(TIM2,TIM_IT_Update);
	}
}

void TIM1_BRK_TIM9_IRQHandler(void)
{
	//判断更新中断是否触发
	if(TIM_GetITStatus(TIM9,TIM_IT_Update)==SET){
		//中断程序需要完成的工作
		D4 = ~D4;
		
		//清除中断标志
		TIM_ClearITPendingBit(TIM9,TIM_IT_Update);
	}
}

//定时器3中断处理函数
void TIM3_IRQHandler(void)
{
	//判断更新中断是否触发
	if(TIM_GetITStatus(TIM3,TIM_IT_Update)==SET){
		//中断程序需要完成的工作
		if(PEin(2)==0)
			D1 = ~D1;
		
		//关闭定时器
		TIM_Cmd(TIM3,DISABLE);
		//清除中断标志
		TIM_ClearITPendingBit(TIM3,TIM_IT_Update);
	}
}

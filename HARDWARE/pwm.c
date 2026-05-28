#include "stm32f4xx.h"
#include "pwm.h"


void timer14_pwm_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;
	
	//1.开启时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM14,ENABLE);
	
	//2.初始化GPIO PF9
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;//PF9
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;//输出速度
	GPIO_Init(GPIOF,&GPIO_InitStructure);
	//将PF9复用功能映射到TIM14 CH1
	GPIO_PinAFConfig(GPIOF,GPIO_PinSource9,GPIO_AF_TIM14);
	
	//3.初始化定时器14 周期短 ms级
	TIM_TimeBaseInitStructure.TIM_Prescaler = 84-1;//84分频
	TIM_TimeBaseInitStructure.TIM_Period = 1000-1;//重装载值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;//时钟因子
	TIM_TimeBaseInit(TIM14, &TIM_TimeBaseInitStructure);
	
	//4.配置定时器14的PWM参数
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;//PWM模式1
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;//极性 低
	TIM_OCInitStructure.TIM_Pulse = 500;//比较计数值
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;//使能
	TIM_OC1Init(TIM14,&TIM_OCInitStructure);
	
	//5.使能预装载和重装载
	TIM_OC1PreloadConfig(TIM14,TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM14,ENABLE);
	
	//6.启动定时器 先低后高
	TIM_Cmd(TIM14,ENABLE);
	//TIM_CtrlPWMOutputs
}

void timer1_pwm_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;
	
	//1.开启时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1,ENABLE);
	
	//2.初始化GPIO PE13 13 14
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13|GPIO_Pin_14;//PE13
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;//输出速度
	GPIO_Init(GPIOE,&GPIO_InitStructure);
	//将PE13复用功能映射到TIM1 CH1
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource13,GPIO_AF_TIM1);
	GPIO_PinAFConfig(GPIOE,GPIO_PinSource14,GPIO_AF_TIM1);
	
	//3.初始化定时器14 周期短 ms级
	TIM_TimeBaseInitStructure.TIM_Prescaler = 84-1;//84分频
	TIM_TimeBaseInitStructure.TIM_Period = 1000-1;//重装载值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;//时钟因子
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);
	
	//4.配置定时器14的PWM参数
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;//PWM模式1
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;//极性 低
	TIM_OCInitStructure.TIM_Pulse = 500;//比较计数值
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;//使能
	TIM_OC3Init(TIM1,&TIM_OCInitStructure);
	
	TIM_OC4Init(TIM1,&TIM_OCInitStructure);
	
	//5.使能预装载和重装载
	TIM_OC3PreloadConfig(TIM1,TIM_OCPreload_Enable);
	TIM_OC4PreloadConfig(TIM1,TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM1,ENABLE);
	
	//6.启动定时器 先低后高
	TIM_Cmd(TIM1,ENABLE);
	TIM_CtrlPWMOutputs(TIM1,ENABLE);
}


void timer3_pwm_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;
	
	//1.开启时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);
	
	//2.初始化GPIO PC6
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;//PC6
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;//输出速度
	GPIO_Init(GPIOC,&GPIO_InitStructure);
	//将PF9复用功能映射到TIM3 CH1
	GPIO_PinAFConfig(GPIOC,GPIO_PinSource6,GPIO_AF_TIM3);
	
	//3.初始化定时器3 周期短 ms级
	TIM_TimeBaseInitStructure.TIM_Prescaler = 84-1;//84分频
	TIM_TimeBaseInitStructure.TIM_Period = 1000-1;//重装载值
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;//时钟因子
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
	
	//4.配置定时器3的PWM参数
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;//PWM模式1
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;//极性 低
	TIM_OCInitStructure.TIM_Pulse = 950;//比较计数值
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;//使能
	TIM_OC1Init(TIM3,&TIM_OCInitStructure);
	
	//5.使能预装载和重装载
	TIM_OC1PreloadConfig(TIM3,TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM3,ENABLE);
	
	//6.启动定时器 先低后高
	TIM_Cmd(TIM3,ENABLE);
	//TIM_CtrlPWMOutputs
}


#include "stm32f4xx.h"
#include "sr04.h"
#include "delay.h"


void sr04_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	//1.开启时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA|RCC_AHB1Periph_GPIOE,ENABLE);
	
	//2.初始化GPIO PA8 ECHO
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;//输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;//PA8
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_Init(GPIOA,&GPIO_InitStructure);	
	//PE6 TRIG
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;//输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;//PE6
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;//输出速度 低
	GPIO_Init(GPIOE,&GPIO_InitStructure);
	
	//TRIG默认输出低
	TRIG = 0;
}

//测距
int get_distance(void)
{
	u32 retry = 0;//,count = 0;
	
	//1.TRIG发送 >10us的高电平起始信号
	TRIG = 1;
	delay_us(15);
	TRIG = 0;
	
	//2.等待ECHO变高,超过60ms不变高，直接退出返回-1
	while(ECHO==0){
		retry++;
		delay_us(1);
		
		if(retry>60000)
			return -1;
	}
	
	//启动定时器
//	TIM_SetCounter(TIM2,0);
//	TIM_Cmd(TIM2,ENABLE);
	
	retry = 0;
	//3.计算高电平事件,等待ECHO变低
	while(ECHO==1){
		retry++;
		delay_us(10);
		
		if(retry>6000){
			//TIM_Cmd(TIM2,DISABLE);
			return -1;
		}
	}
	
	//读取定时器的计数值
	//count = TIM_GetCounter(TIM2);
	//TIM_Cmd(TIM2,DISABLE);
	
	//4.通过高电平时间计算距离
	return retry*10/58;
	
}

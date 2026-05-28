#include "stm32f4xx.h"
#include "beep.h"

//BEEP初始化
void beep_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	//1.开启时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF,ENABLE);
	
	//2.初始化GPIO
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;//输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;//PF8
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;//输出速度 低
	GPIO_Init(GPIOF,&GPIO_InitStructure);
	
	//3.默认输出低
	GPIO_ResetBits(GPIOF,GPIO_Pin_8);
}

#include "stm32f4xx.h"
#include "mq2.h"

//输入模式
void mq2_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	//1.开启时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);
	
	//2.初始化GPIO PA0
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;//输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;//PA9
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;//无上下拉
	GPIO_Init(GPIOA,&GPIO_InitStructure);	
}

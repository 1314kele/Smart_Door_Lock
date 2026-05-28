#include "stm32f4xx.h"
#include "led.h"

//循环延时
void delay(unsigned int ms)
{
	int i,j;
	
	for(i=0;i<ms;i++)
		for(j=0;j<41750;j++);
}

//LED初始化
void led_init(void)
{
	//使能GPIOF和GPIOE的时钟
	RCC->AHB1ENR |= 0x3<<4;
	

	//同时设置PF9 PF10  18 ~ 21位
	GPIOF->MODER &= ~(0xf<<18);// 18~21 ===> 0000
	GPIOF->MODER |= 0x5<<18;//输出模式 18~21 ===> 0101
	
	GPIOF->OTYPER &= ~(0x3<<9);//推挽
	
	GPIOF->OSPEEDR &= ~(0xf<<18);//低速 18~21 ===> 00
	
	GPIOF->PUPDR &= ~(0xf<<18);//无上下拉 18~21 ===> 00
	
	//默认关闭 --- 高电平
	GPIOF->ODR |= 0x3<<9;
	
	
	//同时设置PE13 PE14   26~ 29位
	GPIOE->MODER &= ~(0xf<<26);// 18~21 ===> 0000
	GPIOE->MODER |= 0x5<<26;//输出模式 18~21 ===> 0101
	
	GPIOE->OTYPER &= ~(0x3<<13);//推挽
	
	GPIOE->OSPEEDR &= ~(0xf<<26);//低速 18~21 ===> 00
	
	GPIOE->PUPDR &= ~(0xf<<26);//无上下拉 18~21 ===> 00
	
	//默认关闭 --- 高电平
	GPIOE->ODR |= 0x3<<13;
}

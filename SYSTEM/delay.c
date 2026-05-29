#include "stm32f4xx.h"
#include "delay.h"

#include "FreeRTOS.h"
#include "task.h"

void delay_init(void)
{
	SysTick->CTRL &= ~SysTick_CTRL_CLKSOURCE_Msk;
	SysTick->LOAD = 0xFFFFFF;
	SysTick->VAL = 0;
	SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

// ms延时  1ms = 21000    nms < 2^24 / 21000 = 798
void delay_ms(unsigned int nms)
{
	uint32_t ticks;
    uint32_t told,tnow,tcnt=0;
    uint32_t reload=SysTick->LOAD;    //系统时钟重装载值            
    ticks=nms*(SystemCoreClock/1000);//需要的节拍数 
    told=SysTick->VAL;            //上次时间计数器的值
 
    /* 挂起任务调度器[可选,避免高优先级任务抢占当前任务,能够获得更高的当前延时精度] */
    vTaskSuspendAll();    

    while(1)
    {
        tnow=SysTick->VAL;
        
        if(tnow!=told)
        {     
            /* SYSTICK是一个递减的计数器 */
            if(tnow<told)
                tcnt+=told-tnow;        
            else 
                tcnt+=reload-tnow+told;      
            
            told=tnow;
            
            /* 时间到达/超过需要延迟的时间,则退出循环*/
            if(tcnt>=ticks)
                break;            
        }  
    }

    /* 恢复任务调度器[可选] */
    xTaskResumeAll();
}


void delay_us(uint32_t nus)
{        
    uint32_t ticks;
    uint32_t told,tnow,tcnt=0;
    uint32_t reload=SysTick->LOAD;    //系统时钟重装载值            
    ticks=nus*(SystemCoreClock/1000000);//需要的节拍数 
    told=SysTick->VAL;            //上次时间计数器的值
 
    /* 挂起任务调度器[可选,避免高优先级任务抢占当前任务,能够获得更高的当前延时精度] */
    vTaskSuspendAll();    

    while(1)
    {
        tnow=SysTick->VAL;
        
        if(tnow!=told)
        {     
            /* SYSTICK是一个递减的计数器 */
            if(tnow<told)
                tcnt+=told-tnow;        
            else 
                tcnt+=reload-tnow+told;      
            
            told=tnow;
            
            /* 时间到达/超过需要延迟的时间,则退出循环*/
            if(tcnt>=ticks)
                break;            
        }  
    }

    /* 恢复任务调度器[可选] */
    xTaskResumeAll();
}  

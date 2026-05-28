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

//ms��ʱ  1ms = 21000    nms < 2^24 / 21000 = 798
void delay_ms(unsigned int nms)
{
	uint32_t ticks;
    uint32_t told,tnow,tcnt=0;
    uint32_t reload=SysTick->LOAD;    //ϵͳ��ʱ��������ֵ             
    ticks=nms*(SystemCoreClock/1000);//��Ҫ�Ľ����� 
    told=SysTick->VAL;            //�ս���ʱ�ļ�����ֵ
 
    /* ���������[��ѡ,�ᵼ�¸����ȼ������޷���ռ��ǰ���񣬵��ܹ���ߵ�ǰ����ʱ��ľ�ȷ��] */
    vTaskSuspendAll();    
 
    while(1)
    {
        tnow=SysTick->VAL;
        
        if(tnow!=told)
        {     
            /* SYSTICK��һ���ݼ��ļ����� */
            if(tnow<told)
                tcnt+=told-tnow;        
            else 
                tcnt+=reload-tnow+told;      
            
            told=tnow;
            
            /* ʱ�䳬��/����Ҫ�ӳٵ�ʱ��,���˳���*/
            if(tcnt>=ticks)
                break;            
        }  
    }

    /* �ָ�������[��ѡ] */
    xTaskResumeAll();
}


void delay_us(uint32_t nus)
{        
    uint32_t ticks;
    uint32_t told,tnow,tcnt=0;
    uint32_t reload=SysTick->LOAD;    //ϵͳ��ʱ��������ֵ             
    ticks=nus*(SystemCoreClock/1000000);//��Ҫ�Ľ����� 
    told=SysTick->VAL;            //�ս���ʱ�ļ�����ֵ
 
    /* ���������[��ѡ,�ᵼ�¸����ȼ������޷���ռ��ǰ���񣬵��ܹ���ߵ�ǰ����ʱ��ľ�ȷ��] */
    vTaskSuspendAll();    
 
    while(1)
    {
        tnow=SysTick->VAL;
        
        if(tnow!=told)
        {     
            /* SYSTICK��һ���ݼ��ļ����� */
            if(tnow<told)
                tcnt+=told-tnow;        
            else 
                tcnt+=reload-tnow+told;      
            
            told=tnow;
            
            /* ʱ�䳬��/����Ҫ�ӳٵ�ʱ��,���˳���*/
            if(tcnt>=ticks)
                break;            
        }  
    }

    /* �ָ�������[��ѡ] */
    xTaskResumeAll();
}  

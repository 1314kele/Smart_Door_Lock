#include "keypad.h"
#include "delay.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"

/* 
 * 行线 (推挽输出) 
 * R1 -> PB7 (引脚1)
 * R2 -> PG15 (引脚5) 我们跳过 PA4(引脚3)，把它还给 RC522 的 CS
 * R3 -> PC7 (引脚7)
 * R4 -> PC9 (引脚9)
 * 
 * 列线 (上拉输入) 
 * C1 -> PB6 (引脚11)
 * C2 -> PE6 (引脚13)
 * C3 -> PA8 (引脚15)
 * C4 -> PA4 (引脚3，原本是RC522 CS，现在空闲)                                                                   
 * 
 * 注：第8根线接右边的 引脚8(PC6)！
 */

void keypad_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    // 开启使用到的 GPIO 时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB | 
                           RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOE | 
                           RCC_AHB1Periph_GPIOG, ENABLE);

    // ================= 配置 4 个行线为 推挽输出 =================
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7; // PB7
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15; // PG15
    GPIO_Init(GPIOG, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_9; // PC7, PC9
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // ================= 配置 4 个列线为 带上拉输入 =================
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6; // PB6
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6; // PE6
    GPIO_Init(GPIOE, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8; // PA8
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 指定列线 4 为 PA4
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    GPIO_SetBits(GPIOB, GPIO_Pin_7);
    GPIO_SetBits(GPIOG, GPIO_Pin_15);
    GPIO_SetBits(GPIOC, GPIO_Pin_7 | GPIO_Pin_9);
}

// 拉低指定行函数
static void set_row_low(int row) {
    GPIO_SetBits(GPIOB, GPIO_Pin_7);
    GPIO_SetBits(GPIOG, GPIO_Pin_15);
    GPIO_SetBits(GPIOC, GPIO_Pin_7 | GPIO_Pin_9);
    
    if (row == 0) GPIO_ResetBits(GPIOB, GPIO_Pin_7);
    else if (row == 1) GPIO_ResetBits(GPIOG, GPIO_Pin_15);
    else if (row == 2) GPIO_ResetBits(GPIOC, GPIO_Pin_7);
    else if (row == 3) GPIO_ResetBits(GPIOC, GPIO_Pin_9);
}

// 读取指定列状态，0表示按下
static int read_col(int col) {
    if (col == 0) return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6);
    if (col == 1) return GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_6);
    if (col == 2) return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8);
    if (col == 3) return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_4); // C4 -> PA4
    return 1;
}

//扫描键盘
uint8_t keypad_scan(void)
{
    char keymap[4][4] = {
        {'1','2','3','A'},
        {'4','5','6','B'},
        {'7','8','9','C'},
        {'*','0','#','D'}
    };
    int r, c;
    uint8_t key_detected = 0;
    
    for(r = 0; r < 4; r++) {
        set_row_low(r);
        delay_ms(5);
        
        for(c = 0; c < 4; c++) {
            if(read_col(c) == Bit_RESET) {
                delay_ms(20);
                if(read_col(c) == Bit_RESET) {
                    delay_ms(20);
                    if(read_col(c) == Bit_RESET) {
                        key_detected = keymap[r][c];
                        while(read_col(c) == Bit_RESET);
                        delay_ms(30);
                        return key_detected;
                    }
                }
            }
        }
    }
    return 0;
}




#include "oled.h"
#include "delay.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "font8x16.h"

static uint8_t oled_addr = 0x78;

#define SCL_Clr() GPIO_ResetBits(GPIOB, GPIO_Pin_8)
#define SCL_Set() GPIO_SetBits(GPIOB, GPIO_Pin_8)
#define SDA_Clr() GPIO_ResetBits(GPIOB, GPIO_Pin_9)
#define SDA_Set() GPIO_SetBits(GPIOB, GPIO_Pin_9)
#define SDA_In()  ((GPIOB->IDR & GPIO_Pin_9) ? 1 : 0)

static void SDA_Out(void) { GPIOB->MODER |= (1 << 18); }
static void SDA_InMode(void) { GPIOB->MODER &= ~(3 << 18); }

static void I2C_Start(void) {
    SDA_Set(); SCL_Set(); delay_us(5);
    SDA_Clr(); delay_us(5); SCL_Clr(); delay_us(5);
}
static void I2C_Stop(void) {
    SCL_Clr(); SDA_Clr(); delay_us(5);
    SCL_Set(); delay_us(5); SDA_Set(); delay_us(5);
}
static uint8_t I2C_WaitAck(void) {
    uint8_t ack;
    SDA_Set();
    SDA_InMode();
    delay_us(2);
    SCL_Set();
    delay_us(2);
    ack = SDA_In();
    SCL_Clr();
    SDA_Out();
    delay_us(2);
    return ack;
}
static void I2C_WriteByte(uint8_t dat) {
    uint8_t i;
    for(i = 0; i < 8; i++) {
        if(dat & 0x80) SDA_Set(); else SDA_Clr();
        SCL_Set(); delay_us(5); SCL_Clr(); delay_us(5);
        dat <<= 1;
    }
    I2C_WaitAck();
}

static void OLED_WriteCmd(uint8_t cmd) {
    I2C_Start(); I2C_WriteByte(oled_addr); I2C_WriteByte(0x00); I2C_WriteByte(cmd); I2C_Stop();
}
static void OLED_WriteDat(uint8_t dat) {
    I2C_Start(); I2C_WriteByte(oled_addr); I2C_WriteByte(0x40); I2C_WriteByte(dat); I2C_Stop();
}

static uint8_t oled_probe_addr(uint8_t addr) {
    I2C_Start();
    I2C_WriteByte(addr);
    I2C_Stop();
    return 1;
}

void oled_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    SDA_Set(); SCL_Set();
    delay_ms(50);

    oled_probe_addr(0x78);
    oled_probe_addr(0x7A);

    OLED_WriteCmd(0xAE);
    delay_ms(10);
    OLED_WriteCmd(0xD5); OLED_WriteCmd(0x80);
    OLED_WriteCmd(0xA8); OLED_WriteCmd(0x3F);
    OLED_WriteCmd(0xD3); OLED_WriteCmd(0x00);
    OLED_WriteCmd(0x40);
    OLED_WriteCmd(0x8D); OLED_WriteCmd(0x14);
    OLED_WriteCmd(0x20); OLED_WriteCmd(0x02);
    OLED_WriteCmd(0xA1);
    OLED_WriteCmd(0xC8);
    OLED_WriteCmd(0xDA); OLED_WriteCmd(0x12);
    OLED_WriteCmd(0x81); OLED_WriteCmd(0xCF);
    OLED_WriteCmd(0xD9); OLED_WriteCmd(0xF1);
    OLED_WriteCmd(0xDB); OLED_WriteCmd(0x40);
    OLED_WriteCmd(0xA4);
    OLED_WriteCmd(0xA6);
    OLED_WriteCmd(0xAF);
    delay_ms(100);
    oled_clear();
}

void oled_clear(void) {
    uint8_t i, n;
    for(i = 0; i < 8; i++) {
        OLED_WriteCmd(0xb0 + i); OLED_WriteCmd(0x00); OLED_WriteCmd(0x10);
        for(n = 0; n < 128; n++) OLED_WriteDat(0);
    }
}

void oled_show_string(uint8_t x, uint8_t y, char *str) {
    uint8_t c, i;
    while(*str) {
        c = *str;
        if(c < 32 || c > 126) c = 32;
        c -= 32;
        OLED_WriteCmd(0xb0 + y);
        OLED_WriteCmd(0x00 + (x & 0x0F));
        OLED_WriteCmd(0x10 + (x >> 4));
        for(i = 0; i < 8; i++) OLED_WriteDat(F8X16[c][i]);
        OLED_WriteCmd(0xb0 + y + 1);
        OLED_WriteCmd(0x00 + (x & 0x0F));
        OLED_WriteCmd(0x10 + (x >> 4));
        for(i = 8; i < 16; i++) OLED_WriteDat(F8X16[c][i]);
        x += 8;
        str++;
    }
}

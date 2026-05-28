#include "rc522.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include <stdio.h>

#define RC522_CS_L()   GPIO_ResetBits(GPIOD, GPIO_Pin_7)
#define RC522_CS_H()   GPIO_SetBits(GPIOD, GPIO_Pin_7)
#define RC522_RST_L()  GPIO_ResetBits(GPIOC, GPIO_Pin_11)
#define RC522_RST_H()  GPIO_SetBits(GPIOC, GPIO_Pin_11)

#define SCK_H()        GPIO_SetBits(GPIOD, GPIO_Pin_6)
#define SCK_L()        GPIO_ResetBits(GPIOD, GPIO_Pin_6)
#define MOSI_H()       GPIO_SetBits(GPIOC, GPIO_Pin_6)
#define MOSI_L()       GPIO_ResetBits(GPIOC, GPIO_Pin_6)
#define MISO_IN()      GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_8)

static void spi_delay(void) {
    volatile uint16_t i;
    for(i = 0; i < 50; i++);
}

static void rc522_delay_ms(uint16_t ms) {
    volatile uint32_t i;
    for(i = 0; i < (uint32_t)ms * 8000; i++);
}

#define PCD_IDLE       0x00
#define PCD_TRANSCEIVE 0x0C
#define PCD_RESETPHASE 0x0F

#define CommandReg     0x01
#define ComIEnReg      0x02
#define ComIrqReg      0x04
#define FIFODataReg    0x09
#define FIFOLevelReg   0x0A
#define BitFramingReg  0x0D
#define ModeReg        0x11
#define TxControlReg   0x14
#define TxAutoReg      0x15
#define TModeReg       0x2A
#define TPrescalerReg  0x2B
#define TReloadRegH    0x2C
#define TReloadRegL    0x2D
#define VersionReg     0x37

uint8_t rc522_ver = 0;

static uint8_t SPI_RW(uint8_t data) {
    uint8_t i;
    for(i = 0; i < 8; i++) {
        if(data & 0x80) MOSI_H(); else MOSI_L();
        spi_delay();
        SCK_H();
        spi_delay();
        data <<= 1;
        if(MISO_IN()) data |= 1;
        SCK_L();
        spi_delay();
    }
    return data;
}

static uint8_t RC522_ReadReg(uint8_t addr) {
    uint8_t val;
    RC522_CS_L();
    SPI_RW(((addr << 1) & 0x7E) | 0x80);
    val = SPI_RW(0x00);
    RC522_CS_H();
    return val;
}

static void RC522_WriteReg(uint8_t addr, uint8_t val) {
    RC522_CS_L();
    SPI_RW((addr << 1) & 0x7E);
    SPI_RW(val);
    RC522_CS_H();
}

static void RC522_SetBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) | mask);
}

static void RC522_ClearBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) & ~mask);
}

static void RC522_AntennaOn(void) {
    uint8_t val = RC522_ReadReg(TxControlReg);
    if(!(val & 0x03)) RC522_SetBitMask(TxControlReg, 0x03);
}

static void RC522_Reset(void) {
    RC522_RST_L();
    rc522_delay_ms(10);
    RC522_RST_H();
    rc522_delay_ms(10);
}

static uint8_t RC522_ToCard(uint8_t cmd, uint8_t *sendData, uint8_t sendLen,
                            uint8_t *backData, uint8_t *backLen) {
    uint8_t n, irqEn = 0x00, waitIRq = 0x00;
    uint16_t i;
    uint8_t lastBits, j;
    if(cmd == PCD_TRANSCEIVE) {
        irqEn = 0x77;
        waitIRq = 0x30;
    }

    RC522_WriteReg(ComIEnReg, irqEn | 0x80);
    RC522_ClearBitMask(ComIrqReg, 0x80);
    RC522_WriteReg(CommandReg, PCD_IDLE);
    RC522_SetBitMask(FIFOLevelReg, 0x80);

    for(n = 0; n < sendLen; n++) RC522_WriteReg(FIFODataReg, sendData[n]);
    RC522_WriteReg(CommandReg, cmd);
    if(cmd == PCD_TRANSCEIVE) RC522_SetBitMask(BitFramingReg, 0x80);

    i = 2000;
    do {
        n = RC522_ReadReg(ComIrqReg);
        i--;
    } while((i != 0) && !(n & 0x01) && !(n & waitIRq));

    RC522_ClearBitMask(BitFramingReg, 0x80);

    if(i == 0) return 1;

    if(RC522_ReadReg(ComIrqReg) & 0x01) return 2;

    if(!(RC522_ReadReg(ComIrqReg) & 0x01)) {
        n = RC522_ReadReg(FIFOLevelReg);
        lastBits = RC522_ReadReg(0x0C) & 0x07;
        if(lastBits) *backLen = (n - 1) * 8 + lastBits;
        else *backLen = n * 8;

        if(n == 0) n = 1;
        if(n > 16) n = 16;
        for(j = 0; j < n; j++) backData[j] = RC522_ReadReg(FIFODataReg);
    }

    return 0;
}

static uint8_t RC522_Request(uint8_t reqMode, uint8_t *TagType) {
    uint8_t backLen;
    RC522_WriteReg(BitFramingReg, 0x07);
    TagType[0] = reqMode;
    if(RC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backLen) != 0) return 1;
    if(backLen != 0x10) return 2;
    return 0;
}

static uint8_t RC522_Anticoll(uint8_t *serNum) {
    uint8_t i, backLen;
    RC522_WriteReg(BitFramingReg, 0x00);
    serNum[0] = 0x93;
    serNum[1] = 0x20;
    if(RC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &backLen) != 0) return 1;
    if(serNum[0] != 0x88) {
        for(i = 0; i < 4; i++) serNum[i] = serNum[i];
    }
    return 0;
}

void rc522_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    RC522_CS_H();
    MOSI_H();
    SCK_L();
    RC522_RST_H();
    rc522_delay_ms(50);

    RC522_Reset();

    RC522_WriteReg(TModeReg, 0x8D);
    RC522_WriteReg(TPrescalerReg, 0x3E);
    RC522_WriteReg(TReloadRegL, 30);
    RC522_WriteReg(TReloadRegH, 0);
    RC522_WriteReg(TxAutoReg, 0x40);
    RC522_WriteReg(ModeReg, 0x3D);
    RC522_AntennaOn();
    rc522_ver = RC522_ReadReg(VersionReg);
}

uint8_t rc522_read_card(uint32_t *card_id)
{
    uint8_t buf[8];
    uint8_t TagType[2];
    uint8_t ret;

    ret = RC522_Request(0x26, TagType);
    if(ret != 0) return 0;

    ret = RC522_Anticoll(buf);
    if(ret != 0) return 0;

    *card_id = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
               ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
    return 1;
}

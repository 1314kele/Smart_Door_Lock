#include "rc522.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include <stdio.h>

/*
 * RC522 RFID 读卡器引脚连接：
 * ----------------------------------------
 * RC522模块    STM32F407
 * ----------------------------------------
 * SDA(CS)      PD7   - 片选信号，低电平选中
 * SCK          PD6   - SPI时钟线
 * MOSI         PC6   - 主发从收数据线
 * MISO         PC8   - 主收从发数据线
 * RST          PC11  - 复位信号，低电平复位
 * ----------------------------------------
 */

// ==================== 硬件控制宏定义 ====================

#define RC522_CS_L()   GPIO_ResetBits(GPIOD, GPIO_Pin_7)   // 片选拉低，选中RC522
#define RC522_CS_H()   GPIO_SetBits(GPIOD, GPIO_Pin_7)    // 片选拉高，取消选中
#define RC522_RST_L()  GPIO_ResetBits(GPIOC, GPIO_Pin_11) // 复位拉低，开始复位
#define RC522_RST_H()  GPIO_SetBits(GPIOC, GPIO_Pin_11)   // 复位拉高，复位完成

#define SCK_H()        GPIO_SetBits(GPIOD, GPIO_Pin_6)     // SPI时钟高
#define SCK_L()        GPIO_ResetBits(GPIOD, GPIO_Pin_6)   // SPI时钟低
#define MOSI_H()       GPIO_SetBits(GPIOC, GPIO_Pin_6)     // MOSI数据线高
#define MOSI_L()       GPIO_ResetBits(GPIOC, GPIO_Pin_6)   // MOSI数据线低
#define MISO_IN()      GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_8)  // 读取MISO数据线状态

// ==================== 延时函数 ====================

/*
 * SPI通信延时函数
 * 约50个空循环，用于SPI时钟边沿间的建立保持时间
 */
static void spi_delay(void) {
    volatile uint16_t i;
    for(i = 0; i < 50; i++);
}

/*
 * 毫秒级延时函数
 * 使用空循环实现延时，约8000次循环对应1ms（72MHz主频）
 */
static void rc522_delay_ms(uint16_t ms) {
    volatile uint32_t i;
    for(i = 0; i < (uint32_t)ms * 8000; i++);
}

// ==================== RC522 命令码定义 ====================

#define PCD_IDLE       0x00  // 空闲命令，取消当前命令
#define PCD_TRANSCEIVE 0x0C  // 发送并接收命令，用于寻卡和读卡
#define PCD_RESETPHASE 0x0F  // 复位命令

// ==================== RC522 寄存器地址定义 ====================

#define CommandReg     0x01  // 命令寄存器，控制RC522的工作状态
#define ComIEnReg      0x02  // 中断使能寄存器
#define ComIrqReg      0x04  // 中断标志寄存器
#define FIFODataReg    0x09  // FIFO数据寄存器，用于读写数据缓冲区
#define FIFOLevelReg   0x0A  // FIFO状态寄存器，指示数据数量
#define BitFramingReg  0x0D  // 位帧寄存器，控制发送/接收位
#define ModeReg        0x11  // 模式寄存器，配置RC522工作模式
#define TxControlReg   0x14  // TX控制寄存器，控制天线驱动
#define TxAutoReg      0x15  // TX自动控制寄存器
#define TModeReg       0x2A  // 定时器模式寄存器
#define TPrescalerReg  0x2B  // 定时器预分频寄存器
#define TReloadRegH    0x2C  // 定时器重载值高位
#define TReloadRegL    0x2D  // 定时器重载值低位
#define VersionReg     0x37  // 版本寄存器，存储芯片版本号

/*
 * 全局变量：存储RC522芯片版本号
 * 0x91或0x92表示正常，高版本号表示芯片更新
 */
uint8_t rc522_ver = 0;

// ==================== SPI 通信函数 ====================

/*
 * SPI单字节读写函数
 * 采用模式0：时钟默认低电平，第一个边沿采样
 * param: data - 要发送的字节
 * return: 接收到的字节
 */
static uint8_t SPI_RW(uint8_t data) {
    uint8_t i;
    for(i = 0; i < 8; i++) {
        // 发送最高位
        if(data & 0x80) MOSI_H(); else MOSI_L();
        spi_delay();
        SCK_H();                        // 时钟上升沿，采集MISO
        spi_delay();
        data <<= 1;                    // 数据左移，准备接收下一位
        if(MISO_IN()) data |= 1;       // 读取MISO位
        SCK_L();                        // 时钟低电平
        spi_delay();
    }
    return data;
}

// ==================== RC522 寄存器操作函数 ====================

/*
 * 读取RC522寄存器
 * param: addr - 寄存器地址
 * return: 读取到的寄存器值
 * 注意：地址需要左移1位，最低位置1表示读操作
 */
static uint8_t RC522_ReadReg(uint8_t addr) {
    uint8_t val;
    RC522_CS_L();                                      // 片选拉低，开始通信
    SPI_RW(((addr << 1) & 0x7E) | 0x80);               // 发送读命令：地址左移+读标志位
    val = SPI_RW(0x00);                                // 读取返回值，发送0x00产生时钟
    RC522_CS_H();                                      // 片选拉高，结束通信
    return val;
}

/*
 * 写入RC522寄存器
 * param: addr - 寄存器地址
 * param: val  - 要写入的值
 * 注意：地址需要左移1位，最低位置0表示写操作
 */
static void RC522_WriteReg(uint8_t addr, uint8_t val) {
    RC522_CS_L();                                      // 片选拉低
    SPI_RW((addr << 1) & 0x7E);                        // 发送写命令：地址左移+写标志位
    SPI_RW(val);                                       // 写入数据
    RC522_CS_H();                                      // 片选拉高
}

/*
 * 置位寄存器（将指定位设为1）
 * param: reg  - 寄存器地址
 * param: mask - 位掩码，要置位的位
 */
static void RC522_SetBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) | mask);
}

/*
 * 清除寄存器位（将指定位设为0）
 * param: reg  - 寄存器地址
 * param: mask - 位掩码，要清除的位
 */
static void RC522_ClearBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) & ~mask);
}

// ==================== 天线与复位控制 ====================

/*
 * 开启天线
 * 只有当天线驱动位(0x03)未设置时才设置
 * TxControlReg bit0-1 控制天线驱动开关
 */
static void RC522_AntennaOn(void) {
    uint8_t val = RC522_ReadReg(TxControlReg);
    if(!(val & 0x03)) {
        RC522_SetBitMask(TxControlReg, 0x03);
    }
}

/*
 * 复位RC522芯片
 * 通过RST引脚实现硬件复位
 * 复位脉冲宽度约10ms
 */
static void RC522_Reset(void) {
    RC522_RST_L();           // 拉低复位引脚
    rc522_delay_ms(10);
    RC522_RST_H();           // 拉高复位引脚
    rc522_delay_ms(10);
}

// ==================== 核心通信函数 ====================

/*
 * 与RFID卡通信的核心函数
 * param: cmd      - 命令码（PCD_IDLE/PCD_TRANSCEIVE等）
 * param: sendData - 发送数据缓冲区
 * param: sendLen  - 发送数据长度
 * param: backData - 接收数据缓冲区
 * param: backLen  - 接收数据长度指针
 * return: 0成功，1超时，2错误
 */
static uint8_t RC522_ToCard(uint8_t cmd, uint8_t *sendData, uint8_t sendLen,
                            uint8_t *backData, uint8_t *backLen) {
    uint8_t n, irqEn = 0x00, waitIRq = 0x00;
    uint16_t i;
    uint8_t lastBits, j;
    
    // 对于收发命令，设置中断使能
    if(cmd == PCD_TRANSCEIVE) {
        irqEn = 0x77;      // 使能RxIRq、IdleIRq、ErrIRq等
        waitIRq = 0x30;    // 等待接收完成和错误标志
    }

    // 1. 配置通信参数
    RC522_WriteReg(ComIEnReg, irqEn | 0x80);   // 使能全局中断
    RC522_ClearBitMask(ComIrqReg, 0x80);         // 清除所有中断标志
    RC522_WriteReg(CommandReg, PCD_IDLE);       // 先发送空闲命令停止当前操作
    RC522_SetBitMask(FIFOLevelReg, 0x80);       // 清除FIFO

    // 2. 写入要发送的数据到FIFO
    for(n = 0; n < sendLen; n++) {
        RC522_WriteReg(FIFODataReg, sendData[n]);
    }

    // 3. 执行命令
    RC522_WriteReg(CommandReg, cmd);            // 启动命令
    if(cmd == PCD_TRANSCEIVE) {
        RC522_SetBitMask(BitFramingReg, 0x80); // 启动发送
    }

    // 4. 等待命令完成（最多2000次轮询）
    i = 2000;
    do {
        n = RC522_ReadReg(ComIrqReg);           // 读取中断标志
        i--;
    } while((i != 0) && !(n & 0x01) && !(n & waitIRq));  // 等待完成或错误

    RC522_ClearBitMask(BitFramingReg, 0x80);    // 停止发送

    // 5. 检查结果
    if(i == 0) return 1;                        // 超时
    if(RC522_ReadReg(ComIrqReg) & 0x01) return 2;  // 错误

    // 6. 读取返回数据
    if(!(RC522_ReadReg(ComIrqReg) & 0x01)) {
        n = RC522_ReadReg(FIFOLevelReg);        // 读取FIFO中的数据个数
        lastBits = RC522_ReadReg(0x0C) & 0x07;  // 读取最后接收的位数
        
        // 计算有效数据长度（字节数×8 + 剩余位数）
        if(lastBits) *backLen = (n - 1) * 8 + lastBits;
        else *backLen = n * 8;

        if(n == 0) n = 1;                      // 最少1字节
        if(n > 16) n = 16;                     // 最多16字节
        
        // 读取FIFO数据
        for(j = 0; j < n; j++) {
            backData[j] = RC522_ReadReg(FIFODataReg);
        }
    }

    return 0;                                   // 成功
}

// ==================== RFID卡操作函数 ====================

/*
 * 请求卡（检测近距离范围内的RFID卡）
 * param: reqMode  - 请求模式
 *        0x26 = REQ_STD  : 标准寻卡（默认）
 *        0x52 = REQ_ALL   : 寻所有卡
 * param: TagType - 返回卡片类型（2字节）
 * return: 0成功，1失败，2无效响应
 */
static uint8_t RC522_Request(uint8_t reqMode, uint8_t *TagType) {
    uint8_t backLen;
    
    RC522_WriteReg(BitFramingReg, 0x07);       // 发送7个最后位
    TagType[0] = reqMode;                       // 设置请求模式
    
    // 发送请求命令，获取卡类型
    if(RC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backLen) != 0) {
        return 1;
    }
    
    // 检查返回数据长度是否为16位（2字节）
    if(backLen != 0x10) {
        return 2;
    }
    
    return 0;
}

/*
 * 防冲突（获取卡序列号）
 * 使用Level 1防冲突机制，卡片会返回其唯一序列号
 * param: serNum - 返回卡序列号（4字节）
 * return: 0成功，1失败
 */
static uint8_t RC522_Anticoll(uint8_t *serNum) {
    uint8_t i, backLen;
    
    RC522_WriteReg(BitFramingReg, 0x00);        // 发送0个最后位
    
    // 0x93 = CASCADE LEVEL 1 选择命令
    // 0x20 = 防冲突命令码
    serNum[0] = 0x93;
    serNum[1] = 0x20;
    
    // 执行防冲突，获取卡号
    if(RC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &backLen) != 0) {
        return 1;
    }
    
    // 0x88表示多张卡重叠，需要级联处理
    // 本项目使用单卡模式，不处理级联情况
    if(serNum[0] != 0x88) {
        for(i = 0; i < 4; i++) {
            serNum[i] = serNum[i];              // 复制序列号
        }
    }
    
    return 0;
}

// ==================== 公共接口函数 ====================

/*
 * RC522初始化函数
 * 配置GPIO引脚，复位芯片，配置通信参数
 * 调用此函数后RC522即可使用
 */
void rc522_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    // 1. 使能GPIO时钟（GPIOC: SPI数据线/复位, GPIOD: SPI时钟/片选）
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, ENABLE);

    // 2. 配置GPIO引脚
    
    // PC6: MOSI（主机输出从机输入）- 推挽输出
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // PD6: SCK（SPI时钟）, PD7: CS（片选）- 推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // PC11: RST（复位）- 推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // PC8: MISO（主机输入从机输出）- 上拉输入
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // 3. 初始化引脚电平
    RC522_CS_H();       // 片选默认高（未选中）
    MOSI_H();          // MOSI默认高
    SCK_L();           // SPI时钟默认低
    RC522_RST_H();     // 复位脚默认高
    rc522_delay_ms(50);

    // 4. 复位RC522芯片
    RC522_Reset();

    // 5. 配置定时器（用于通信超时）
    RC522_WriteReg(TModeReg, 0x8D);      // 定时器模式：自动重启
    RC522_WriteReg(TPrescalerReg, 0x3E); // 预分频：62（最终分频约488）
    RC522_WriteReg(TReloadRegL, 30);     // 定时重载值低字节
    RC522_WriteReg(TReloadRegH, 0);      // 定时重载值高字节

    // 6. 配置通信模式
    RC522_WriteReg(TxAutoReg, 0x40);      // 100%ASK调制
    RC522_WriteReg(ModeReg, 0x3D);       // CRC初始值0x6363，CRC计算后反转

    // 7. 开启天线
    RC522_AntennaOn();

    // 8. 读取版本号
    rc522_ver = RC522_ReadReg(VersionReg);
}

/*
 * 读取RFID卡号
 * 综合调用Request和Anticoll完成寻卡和读卡号
 * param: card_id - 返回卡号（32位UID）
 * return: 1成功（检测到卡），0失败（无卡或通信错误）
 * 
 * 使用示例：
 *   uint32_t card_id;
 *   if(rc522_read_card(&card_id)) {
 *       printf("Card ID: %08X\r\n", card_id);
 *   }
 */
uint8_t rc522_read_card(uint32_t *card_id)
{
    uint8_t buf[8];
    uint8_t TagType[2];
    uint8_t ret;

    // 1. 发送请求命令，检测是否有名为卡在感应范围内
    ret = RC522_Request(0x26, TagType);  // 0x26 = REQ_STD，标准寻卡模式
    if(ret != 0) {
        return 0;  // 无卡或寻卡失败
    }

    // 2. 执行防冲突，获取卡序列号
    ret = RC522_Anticoll(buf);
    if(ret != 0) {
        return 0;  // 读卡号失败
    }

    // 3. 组合4字节卡号为32位整数（大端序）
    *card_id = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
               ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
               
    return 1;  // 读卡成功
}

#include "as608.h"
#include "delay.h"
#include "stm32f4xx_usart.h"
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * AS608 指纹传感器模块说明：
 * ----------------------------------------
 * AS608模块    STM32F407
 * ----------------------------------------
 * TX           PA3   - 模块发送 → 单片机接收（USART2_RX）
 * RX           PA2   - 模块接收 ← 单片机发送（USART2_TX）
 * GND          GND   - 地线
 * VCC          3.3V  - 电源（注意：部分模块支持5V，但建议用3.3V）
 * ----------------------------------------
 * 
 * AS608通信协议：
 * - 波特率：默认57600（可配置）
 * - 数据格式：8位数据位，1位停止位，无校验
 * - 帧格式（11字节以上）：
 *   [0xEF] [0x01] [Addr高] [Addr中] [Addr低] [0x01] [Len高] [Len低] [Cmd] [Data...] [Checksum]
 *   固定地址：0xFF 0xFF 0xFF （广播地址）
 *   校验和：从第6字节开始到最后一字节的累加和（低字节）
 */

/*
 * 全局变量说明：
 * - as608Mutex: 互斥锁，保护AS608 UART通信，防止多任务并发访问
 * - fingerprint_enrolling: 指纹录入状态标志，用于互斥识别和录入操作
 */
extern SemaphoreHandle_t as608Mutex;      // 从main.c引入的互斥锁
uint8_t fingerprint_enrolling = 0;         // 是否正在录入指纹（0=否，1=是）

// ==================== 内部函数声明 ====================

/*
 * 通过USART2发送命令数据
 * param: buf  - 要发送的命令缓冲区
 * param: len  - 数据长度
 * 注意：USART2必须预先初始化（在main.c中完成）
 */
static void as608_send_cmd(uint8_t *buf, uint16_t len) {
    uint16_t i;
    for(i = 0; i < len; i++) {
        while(USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);  // 等待发送寄存器为空
        USART_SendData(USART2, buf[i]);                                // 发送一字节
    }
    while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);       // 等待发送完成
}

/*
 * 接收AS608的响应数据
 * param: reply       - 接收缓冲区
 * param: timeout_ms  - 超时时间（毫秒）
 * param: min_len     - 最小期望接收长度
 * return: 实际接收到的字节数
 * 
 * 工作流程：
 * 1. 先清空接收缓冲区（丢弃可能的残留数据）
 * 2. 循环等待接收数据，直到超时或达到最小长度
 */
static uint8_t as608_receve_reply(uint8_t *reply, uint16_t timeout_ms, uint16_t min_len) {
    uint16_t i = 0;
    volatile uint32_t t;
    
    // 清空接收缓冲区（最多读取1000个残留字节）
    for(t = 0; t < 1000; t++) {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET) {
            USART_ReceiveData(USART2);
        } else {
            break;
        }
    }
    
    // 接收数据，超时时间约为 timeout_ms 毫秒
    // 每个循环约0.05us，timeout_ms*20000 ≈ timeout_ms毫秒
    for(t = 0; t < (uint32_t)timeout_ms * 20000; t++) {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET) {
            reply[i++] = USART_ReceiveData(USART2);
            if(i >= min_len) break;  // 达到最小长度即可返回
        }
    }
    return i;
}

// ==================== 公共接口函数 ====================

/*
 * AS608初始化函数
 * 注意：此函数仅做简单延时，USART2的初始化在main.c中完成
 * 延时200ms确保模块稳定上电
 */
void as608_init(void) {
    delay_ms(200);
}

/*
 * 读取AS608模块参数
 * return: 1成功，0失败
 * 
 * 读取的参数包括：
 * - reply[10]: 波特率设置（0=9600, 1=19200, 2=38400, 3=57600, 4=115200）
 * - reply[11]: 安全等级（0-5，等级越高越严格）
 * - reply[12]: 数据包长度（0=32字节, 1=64字节, 2=128字节, 3=256字节）
 */
uint8_t as608_read_params(void) {
    uint8_t reply[16] = {0};
    // 命令帧：读取系统参数（0x26）
    uint8_t cmd_read_para[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x26,0x00,0x2A};
    
    xSemaphoreTake(as608Mutex, portMAX_DELAY);  // 获取互斥锁
    as608_send_cmd(cmd_read_para, sizeof(cmd_read_para));
    
    if(as608_receve_reply(reply, 1000, 12) < 12) {
        printf("[AS608] Read params: no response\r\n");
        xSemaphoreGive(as608Mutex);
        return 0;
    }
    
    printf("[AS608] Params: BaudRate=%d, Security=%d, DataLen=%d\r\n", 
           reply[10], reply[11], reply[12]);
    xSemaphoreGive(as608Mutex);
    return 1;
}

/*
 * 与AS608模块握手（检测模块是否在线）
 * return: 1成功，0失败
 * 
 * 通过读取参数命令进行握手验证
 */
uint8_t as608_handshake(void) {
    uint8_t reply[16] = {0};
    uint8_t cmd_read_para[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x26,0x00,0x2A};

    xSemaphoreTake(as608Mutex, portMAX_DELAY);
    as608_send_cmd(cmd_read_para, sizeof(cmd_read_para));
    
    if(as608_receve_reply(reply, 1000, 12) == 0) {
        printf("[AS608] Handshake: no response\r\n");
        xSemaphoreGive(as608Mutex);
        return 0;
    }
    
    printf("[AS608] Handshake OK! confirm_code=0x%02X\r\n", reply[9]);
    xSemaphoreGive(as608Mutex);
    return 1;
}

/*
 * 指纹搜索（识别指纹）
 * param: finger_id - 输出参数，匹配到的指纹ID
 * return: 1成功（匹配到指纹），0失败（无匹配或错误）
 * 
 * 操作流程：
 * 1. 获取图像（PS_GetImage）
 * 2. 生成特征（PS_GenChar）
 * 3. 搜索比对（PS_HighSpeedSearch）
 * 
 * 命令帧说明：
 * - cmd_get_img: 采集指纹图像（0x01）
 * - cmd_gen_char: 生成特征模板（0x02），存入缓冲区1
 * - cmd_search: 高速搜索（0x04），在指纹库中搜索匹配
 */
uint8_t as608_search_finger(uint16_t *finger_id) {
    uint8_t reply[32];
    uint8_t ret;
    uint8_t cmd_get_img[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x01,0x00,0x05};      // 获取图像
    uint8_t cmd_gen_char[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x01,0x00,0x08}; // 生成特征
    uint8_t cmd_search[]   = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x08,0x04,0x01,0x00,0x00,0x03,0xE7,0x00,0xF8}; // 搜索
    BaseType_t mutex_taken = pdFALSE;

    // 尝试获取互斥锁（最多等待100ms）
    if(xSemaphoreTake(as608Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;  // 获取锁失败，可能正在录入
    }
    mutex_taken = pdTRUE;
    
    // 获取锁后再次检查是否在录入状态
    if(fingerprint_enrolling) {
        goto exit;  // 正在录入，不允许识别
    }

    // ========== 步骤1：获取指纹图像 ==========
    as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
    ret = as608_receve_reply(reply, 500, 12);
    if(ret < 10 || reply[9] != 0x00) {
        goto exit;  // 获取图像失败
    }

    // ========== 步骤2：生成特征模板 ==========
    as608_send_cmd(cmd_gen_char, sizeof(cmd_gen_char));
    ret = as608_receve_reply(reply, 500, 12);
    if(ret < 10 || reply[9] != 0x00) {
        goto exit;  // 生成特征失败
    }

    // ========== 步骤3：搜索比对 ==========
    as608_send_cmd(cmd_search, sizeof(cmd_search));
    ret = as608_receve_reply(reply, 500, 16);
    if(ret < 10 || reply[9] != 0x00) {
        goto exit;  // 搜索失败或无匹配
    }

    // 提取匹配结果：指纹ID和匹配分数
    *finger_id = (reply[10] << 8) | reply[11];
    printf("[Finger] Match! ID=%d score=%d\r\n", *finger_id, (reply[14]<<8)|reply[15]);
    
exit:
    if(mutex_taken == pdTRUE) {
        xSemaphoreGive(as608Mutex);
    }
    return (ret >= 12 && reply[9] == 0x00) ? 1 : 0;
}

/*
 * 指纹录入（注册新指纹）
 * param: finger_id - 要注册的指纹ID（0-9）
 * return: 1成功，0失败
 * 
 * 录入流程（4步）：
 * 1. 获取第一幅指纹图像
 * 2. 生成第一个特征模板（存入缓冲区1）
 * 3. 获取第二幅指纹图像
 * 4. 生成第二个特征模板（存入缓冲区2）
 * 5. 合并两个模板生成最终模板
 * 6. 保存模板到指定ID位置
 * 
 * 命令帧说明：
 * - cmd_get_img: 获取图像
 * - cmd_gen_char1: 生成特征到缓冲区1（0x01）
 * - cmd_gen_char2: 生成特征到缓冲区2（0x02）
 * - cmd_reg_model: 合并两个缓冲区生成模板（0x05）
 * - cmd_store: 保存模板到指纹库（0x06）
 */
uint8_t as608_enroll_finger(uint16_t finger_id) {
    uint8_t reply[32] = {0};
    uint8_t ret;
    uint8_t cmd_get_img[]    = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x01,0x00,0x05};      // 获取图像
    uint8_t cmd_gen_char1[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x01,0x00,0x08}; // 生成特征1
    uint8_t cmd_gen_char2[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x02,0x00,0x09}; // 生成特征2
    uint8_t cmd_reg_model[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x05,0x00,0x09};      // 合并模板
    uint8_t cmd_store[]      = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x06,0x06,0x01,0x00,0x00,0x00,0x00}; // 保存
    uint8_t result = 0;
    uint16_t attempt = 0;
    uint16_t sum = 0;

    // 获取互斥锁
    if(xSemaphoreTake(as608Mutex, portMAX_DELAY) != pdTRUE) {
        printf("[Enroll] Failed to get mutex!\r\n");
        return 0;
    }
    
    fingerprint_enrolling = 1;  // 设置录入标志，防止识别操作干扰

    // 构建保存命令：设置目标ID和计算校验和
    cmd_store[11] = (finger_id >> 8) & 0xFF;          // ID高字节
    cmd_store[12] = finger_id & 0xFF;                  // ID低字节
    sum = 0x01 + 0x00 + 0x06 + 0x06 + 0x01 + cmd_store[11] + cmd_store[12];
    cmd_store[13] = (sum >> 8) & 0xFF;
    cmd_store[14] = sum & 0xFF;

    // ========== 步骤1/4：获取第一幅指纹图像 ==========
    printf("[Enroll] ID=%d - Step 1/4: Place finger on sensor...\r\n", finger_id);
    attempt = 0;
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(200));  // 200ms间隔轮询
        as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
        ret = as608_receve_reply(reply, 1000, 12);
        
        if(ret == 0) {
            printf("[Enroll] GetImage1: no response from module!\r\n");
            goto exit;
        }
        if(ret >= 12 && reply[9] == 0x00) break;          // 成功
        if(ret >= 12 && reply[9] == 0x02) {               // 传感器无手指
            if(attempt++ % 10 == 0) printf("[Enroll] Waiting for finger...\r\n");
            continue;
        }
        printf("[Enroll] GetImage1 fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Step 1 OK - Remove finger...\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待用户移开手指

    // ========== 步骤2/4：生成第一个特征模板 ==========
    printf("[Enroll] Step 2/4: Generating template...\r\n");
    as608_send_cmd(cmd_gen_char1, sizeof(cmd_gen_char1));
    ret = as608_receve_reply(reply, 1000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] GenChar1 fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Step 2 OK - Place finger again...\r\n");

    // ========== 步骤3/4：获取第二幅指纹图像 ==========
    attempt = 0;
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
        ret = as608_receve_reply(reply, 1000, 12);
        
        if(ret == 0) {
            printf("[Enroll] GetImage2: no response!\r\n");
            goto exit;
        }
        if(ret >= 12 && reply[9] == 0x00) break;
        if(ret >= 12 && reply[9] == 0x02) {
            if(attempt++ % 10 == 0) printf("[Enroll] Waiting for finger again...\r\n");
            continue;
        }
        printf("[Enroll] GetImage2 fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Step 3 OK - Generating template 2...\r\n");

    // ========== 步骤4/4：生成第二个特征模板 ==========
    as608_send_cmd(cmd_gen_char2, sizeof(cmd_gen_char2));
    ret = as608_receve_reply(reply, 1000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] GenChar2 fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Step 4/4: Merging templates...\r\n");

    // ========== 合并两个特征模板 ==========
    as608_send_cmd(cmd_reg_model, sizeof(cmd_reg_model));
    ret = as608_receve_reply(reply, 2000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] RegModel fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Storing fingerprint...\r\n");

    // ========== 保存模板到指纹库 ==========
    as608_send_cmd(cmd_store, sizeof(cmd_store));
    ret = as608_receve_reply(reply, 1000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] Store fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] SUCCESS! Finger ID=%d enrolled!\r\n", finger_id);
    result = 1;
    
exit:
    fingerprint_enrolling = 0;  // 清除录入标志
    xSemaphoreGive(as608Mutex);
    return result;
}

/*
 * 清空所有指纹
 * 删除指纹库中的所有指纹模板
 */
void as608_clear_all(void) {
    uint8_t reply[16];
    // 清空指纹库命令（0x0D）
    uint8_t cmd_empty[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x0D,0x00,0x11};

    if(xSemaphoreTake(as608Mutex, portMAX_DELAY) != pdTRUE) {
        printf("[Finger] Clear: failed to get mutex\r\n");
        return;
    }
    
    as608_send_cmd(cmd_empty, sizeof(cmd_empty));
    if(as608_receve_reply(reply, 200, 12) >= 12 && reply[9] == 0x00) {
        printf("[Finger] All fingerprints cleared!\r\n");
    } else {
        printf("[Finger] Clear fail: code=0x%02X\r\n", reply[9]);
    }
    
    xSemaphoreGive(as608Mutex);
}

/*
 * 删除指定ID的指纹
 * param: finger_id - 要删除的指纹ID（0-9）
 * return: 1成功，0失败
 */
uint8_t as608_delete_finger(uint16_t finger_id) {
    uint8_t reply[16];
    // 删除指纹命令（0x0C）
    // 格式：[EF 01] [FF FF FF FF] [01] [00 07] [0C] [ID_H ID_L] [00 01] [Sum_H Sum_L]
    uint8_t cmd_delete[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x07, 0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    uint8_t result = 0;
    uint16_t sum = 0;
    
    if(xSemaphoreTake(as608Mutex, portMAX_DELAY) != pdTRUE) {
        printf("[Delete] Failed to get mutex!\r\n");
        return 0;
    }
    
    // 设置要删除的指纹ID（PageID）
    cmd_delete[10] = (finger_id >> 8) & 0xFF;
    cmd_delete[11] = finger_id & 0xFF;
    
    // 计算校验和：Sum = Type(01) + Len(0007) + Cmd(0C) + PageID + Number(0001)
    sum = 0x01 + 0x00 + 0x07 + 0x0C + cmd_delete[10] + cmd_delete[11] + 0x00 + 0x01;
    cmd_delete[14] = (sum >> 8) & 0xFF;
    cmd_delete[15] = sum & 0xFF;
    
    as608_send_cmd(cmd_delete, sizeof(cmd_delete));
    if(as608_receve_reply(reply, 1000, 12) >= 12 && reply[9] == 0x00) {
        printf("[Delete] Finger ID=%d deleted!\r\n", finger_id);
        result = 1;
    } else {
        printf("[Delete] Fail! code=0x%02X\r\n", reply[9]);
    }
    
    xSemaphoreGive(as608Mutex);
    return result;
}

/*
 * AS608错误码说明：
 * ----------------------------------------
 * 错误码   含义
 * ----------------------------------------
 * 0x00     操作成功
 * 0x01     数据包接收错误
 * 0x02     传感器无手指
 * 0x03     录入失败（两次指纹不匹配）
 * 0x05     特征生成失败
 * 0x07     没有找到匹配的指纹
 * 0x08     指纹库已满
 * 0x09     指定ID不存在
 * 0x0A     删除失败
 * ----------------------------------------
 */

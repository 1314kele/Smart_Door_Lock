#include "includes.h"
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// 硬件驱动头文件
#include "oled.h"
#include "rc522.h"
#include "as608.h"
#include "keypad.h"
#include "uart.h"
#include "flash.h"

// 任务句柄
TaskHandle_t app_task_init_handle = NULL;
TaskHandle_t finger_task_handle = NULL;
TaskHandle_t pwd_task_handle = NULL;
TaskHandle_t key_task_handle = NULL;
TaskHandle_t oled_task_handle = NULL;
TaskHandle_t rfid_task_handle = NULL;
TaskHandle_t uart_cmd_task_handle = NULL;

// 队列与互斥锁
QueueHandle_t msgQueue = NULL;      // 消息队列，用于传递开锁指令、显示指令
QueueHandle_t keyQueue = NULL;      // 按键队列，用于接收按键值
SemaphoreHandle_t oledMutex = NULL; // OLED显示互斥锁，防止多个任务同时写屏幕
SemaphoreHandle_t as608Mutex = NULL;// AS608模块互斥锁，防止USART2冲突

uint32_t last_card_uid = 0;
uint32_t saved_card_uid = 0;

// 函数声明
void app_task_init(void* pvParameters);
void vFingerTask(void* pvParameters);
void vPwdTask(void* pvParameters);
void vKeyTask(void* pvParameters);
void vOledTask(void* pvParameters);
void vRfidTask(void* pvParameters);
void vUartCmdTask(void* pvParameters);

// 密码固定预设2个
const char* const correct_pwd_1 = "123456";
const char* const correct_pwd_2 = "654321";

int main()
{
    // 中断优先级分组，4:0
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    delay_init();

    // 硬件初始化
    led_init();
    //beep_init();
    keypad_init();   // 4x4矩阵键盘初始化
    oled_init();     // I2C OLED初始化
    rc522_init();    // SPI RC522初始化
    as608_init();    // 指纹模块初始化
    flash_init();    // Flash存储初始化
    saved_card_uid = flash_read_card_uid();
    
    // 初始化串口，调试/通信
    uart1_init(115200); // 串口控制台
    uart2_init(57600);   // 指纹模块 (AS608波特率-商家资料)
    uart3_init(115200);  // WiFi模块
    
    printf("Smart Door Lock System Start!\n");
    printf("RC522 Version: 0x%02X\r\n", rc522_ver);

    /* 创建app_task_init任务 */
    xTaskCreate((TaskFunction_t )app_task_init,   
                (const char*    )"app_task_init", 
                (uint16_t       )512,             
                (void*          )NULL,            
                (UBaseType_t    )5,               
                (TaskHandle_t*  )&app_task_init_handle); 

    /* 启动任务调度器 */
    vTaskStartScheduler();

    while(1);
}

void app_task_init(void* pvParameters)
{
    printf("app_task_init running!\r\n");

    // 创建队列与互斥锁
    msgQueue = xQueueCreate(10, sizeof(uint32_t)); 
    keyQueue = xQueueCreate(16, sizeof(char)); 
    oledMutex = xSemaphoreCreateMutex();
    as608Mutex = xSemaphoreCreateMutex();

    // 进入临界区
    taskENTER_CRITICAL();

    /* 创建指纹识别任务 */
    xTaskCreate(vFingerTask, "FingerTask", 512, NULL, 3, &finger_task_handle);      
    /* 创建密码键盘任务 */
    xTaskCreate(vPwdTask, "PwdTask", 512, NULL, 3, &pwd_task_handle);      
    /* 创建按键扫描任务 */
    xTaskCreate(vKeyTask, "KeyTask", 512, NULL, 4, &key_task_handle);        
    /* 创建OLED显示任务 */
    xTaskCreate(vOledTask, "OledTask", 512, NULL, 2, &oled_task_handle);
    /* 创建RFID读卡任务 */
    xTaskCreate(vRfidTask, "RfidTask", 512, NULL, 3, &rfid_task_handle);
    /* 创建串口指令处理任务 */
    xTaskCreate(vUartCmdTask, "UartCmdTask", 256, NULL, 3, &uart_cmd_task_handle);

    // 退出临界区
    taskEXIT_CRITICAL();

    // 初始显示
    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
        oled_clear();
        oled_show_string(0, 0, "Wait Unlock...");
        xSemaphoreGive(oledMutex);
    }

    // 删除初始化任务
    vTaskDelete(NULL);
}

// 1. 指纹识别任务
void vFingerTask(void* pvParameters)
{
    uint16_t finger_id = 0;
    uint32_t cmd = 1;
    for(;;)
    {
        if (as608_search_finger(&finger_id)) {
            printf("Finger Unlock! ID: %d\r\n", finger_id);
            xQueueSend(msgQueue, &cmd, 10);
            D1 = 0;
            delay_ms(2000);
            D1 = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// 2. 密码键盘任务（支持模糊匹配）
void vPwdTask(void* pvParameters)
{
    char input_buf[17] = {0}; // 最多16位密码字符
    uint8_t index = 0;
    char key_char;
    uint32_t cmd = 2; // 2表示密码开锁成功

    for(;;)
    {
        // 等待接收从keyQueue传来的按键字符
        if(xQueueReceive(keyQueue, &key_char, portMAX_DELAY) == pdTRUE) {
            
            // 检测到确认键或结束键（#）
            if (key_char == '#') {
                input_buf[index] = '\0'; // 补0
                
                // 验证密码：在输入中搜索匹配的 6 位正确密码（模糊匹配）
                if (strstr(input_buf, correct_pwd_1) != NULL || 
                    strstr(input_buf, correct_pwd_2) != NULL) {
                    
                    printf("Password Unlock!\r\n");
                    xQueueSend(msgQueue, &cmd, 10);
                    D1 = 0; 
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    D1 = 1;
                } else {
                    printf("Password Error!\r\n");
                }
                
                // 清空输入缓冲区
                memset(input_buf, 0, sizeof(input_buf));
                index = 0;
            } else {
                if(index < 16) {
                    input_buf[index++] = key_char;
                }
            }
        }
    }
}

// 3. 按键扫描任务
void vKeyTask(void* pvParameters)
{
    char key_val;
    for(;;)
    {
        // 获取按键值
        key_val = keypad_scan();
        if(key_val != 0) {
            // 发送到按键队列
            xQueueSend(keyQueue, &key_val, 10);
        }
        // 防按键抖动
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// 4. OLED显示任务
void vOledTask(void* pvParameters)
{
    uint32_t msg_cmd;
    for(;;)
    {
        // 等待 msgQueue 中有解锁消息，并实时显示
        if(xQueueReceive(msgQueue, &msg_cmd, portMAX_DELAY) == pdTRUE) {
            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                oled_clear();
                if(msg_cmd == 1)      oled_show_string(0, 0, "Finger OK!");
                else if(msg_cmd == 2) oled_show_string(0, 0, "Pwd OK!");
                else if(msg_cmd == 3) oled_show_string(0, 0, "RFID OK!");
                else if(msg_cmd == 4) oled_show_string(0, 0, "BLE OK!");
                
                xSemaphoreGive(oledMutex);
                
                // 关闭成功提示
                vTaskDelay(pdMS_TO_TICKS(2000));
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    oled_show_string(0, 0, "Wait Unlock...");
                    xSemaphoreGive(oledMutex);
                }
            }
        }
    }
}

// 5. RFID读卡任务
void vRfidTask(void* pvParameters)
{
    uint32_t card_id = 0;
    uint32_t cmd = 3;
    for(;;)
    {
        if(rc522_read_card(&card_id)) {
            last_card_uid = card_id;
            printf("RFID Card: %08X\r\n", card_id);
            if(saved_card_uid == 0) {
                printf("No card saved yet. Send 'save_card*' to save this card.\r\n");
            } else if(card_id == saved_card_uid) {
                printf("RFID Unlock! Card: %08X\r\n", card_id);
                xQueueSend(msgQueue, &cmd, 10);
                D1 = 0;
                vTaskDelay(pdMS_TO_TICKS(2000));
                D1 = 1;
            } else {
                printf("RFID Denied! Unknown card.\r\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void vUartCmdTask(void* pvParameters)
{
    for(;;)
    {
        parse_cmd();
        parse_bl_cmd();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

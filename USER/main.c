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
TaskHandle_t keyMenu_task_handle = NULL;  // 新增键盘菜单任务句柄
TaskHandle_t oled_task_handle = NULL;
TaskHandle_t rfid_task_handle = NULL;
TaskHandle_t uart_cmd_task_handle = NULL;

// 队列与互斥锁
QueueHandle_t msgQueue = NULL;      // 消息队列，用于传递开锁指令、显示指令
QueueHandle_t keyQueue = NULL;      // 按键队列，用于接收按键值
QueueHandle_t keyMenuQueue = NULL;   // 键盘菜单专用队列
QueueHandle_t unlockReqQueue = NULL; // 解锁请求队列
QueueHandle_t unlockResultQueue = NULL; // 解锁结果队列
SemaphoreHandle_t oledMutex = NULL; // OLED显示互斥锁，防止多个任务同时写屏幕
SemaphoreHandle_t as608Mutex = NULL;// AS608模块互斥锁，防止USART2冲突
SemaphoreHandle_t rc522Mutex = NULL;// RC522模块互斥锁，防止SPI冲突

// 键盘解锁模式控制变量
volatile uint8_t keyboard_mode_active = 0;  // 键盘模式是否激活
volatile uint8_t current_mode = 0;          // 当前选中的解锁模式
volatile uint8_t unlock_executing = 0;      // 是否正在执行解锁

// 解锁模式定义
typedef enum {
    MODE_FINGER = 0,      // 指纹解锁
    MODE_PASSWORD = 1,    // 密码解锁
    MODE_RFID = 2,        // RFID解锁
    MODE_COUNT = 3        // 模式总数
} UnlockMode;

// 解锁模式名称
const char* mode_names[] = {
    "1.Finger",
    "2.Password",
    "3.RFID"
};

const char* mode_full_names[] = {
    "Finger Mode",
    "Password Mode",
    "RFID Mode"
};

uint32_t last_card_uid = 0;
uint32_t saved_card_uid = 0;

// 解锁请求结构体
typedef struct {
    uint8_t mode;           // 解锁模式
    uint32_t timeout_ms;    // 超时时间
} UnlockRequest;

// 解锁结果结构体
typedef struct {
    uint8_t mode;           // 解锁模式
    uint8_t success;        // 0=失败, 1=成功
} UnlockResult;

// 函数声明
void app_task_init(void* pvParameters);
void vFingerTask(void* pvParameters);
void vPwdTask(void* pvParameters);
void vKeyTask(void* pvParameters);
void vKeyMenuTask(void* pvParameters);  // 新增键盘菜单任务
void vOledTask(void* pvParameters);
void vRfidTask(void* pvParameters);
void vUartCmdTask(void* pvParameters);
void vUnlockExecTask(void* pvParameters); // 解锁执行任务

// 密码固定预设2个
const char* const correct_pwd_1 = "123456";
const char* const correct_pwd_2 = "654321";

// 按键反馈函数
void key_feedback(void) {
    D1 = 0;  // LED亮
    delay_ms(50);
    D1 = 1;  // LED灭
}

// 键盘模式激活/关闭函数
void keyboard_mode_enable(void) {
    keyboard_mode_active = 1;
    // 挂起其他解锁任务（让这些任务不自动解锁）
    if(finger_task_handle) vTaskSuspend(finger_task_handle);
    if(pwd_task_handle) vTaskSuspend(pwd_task_handle);
    // 注意：不挂起RFID任务，因为解锁执行任务也需要用
    printf("[Keyboard] Mode activated!\r\n");
}

void keyboard_mode_disable(void) {
    keyboard_mode_active = 0;
    current_mode = 0;
    // 恢复其他解锁任务
    if(finger_task_handle) vTaskResume(finger_task_handle);
    if(pwd_task_handle) vTaskResume(pwd_task_handle);
    printf("[Keyboard] Mode deactivated!\r\n");
}

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
    keyMenuQueue = xQueueCreate(16, sizeof(char));  // 新增键盘菜单队列
    unlockReqQueue = xQueueCreate(5, sizeof(UnlockRequest));  // 解锁请求队列
    unlockResultQueue = xQueueCreate(5, sizeof(UnlockResult)); // 解锁结果队列
    oledMutex = xSemaphoreCreateMutex();
    as608Mutex = xSemaphoreCreateMutex();
    rc522Mutex = xSemaphoreCreateMutex();

    // 进入临界区
    taskENTER_CRITICAL();

    /* 创建指纹识别任务 */
    xTaskCreate(vFingerTask, "FingerTask", 512, NULL, 3, &finger_task_handle);      
    /* 创建密码键盘任务 */
    xTaskCreate(vPwdTask, "PwdTask", 512, NULL, 3, &pwd_task_handle);      
    /* 创建按键扫描任务 */
    xTaskCreate(vKeyTask, "KeyTask", 512, NULL, 4, &key_task_handle);        
    /* 创建键盘菜单任务（优先级更高） */
    xTaskCreate(vKeyMenuTask, "KeyMenuTask", 512, NULL, 5, &keyMenu_task_handle);        
    /* 创建OLED显示任务 */
    xTaskCreate(vOledTask, "OledTask", 512, NULL, 2, &oled_task_handle);
    /* 创建RFID读卡任务 */
    xTaskCreate(vRfidTask, "RfidTask", 512, NULL, 3, &rfid_task_handle);
    /* 创建串口指令处理任务 */
    xTaskCreate(vUartCmdTask, "UartCmdTask", 256, NULL, 3, &uart_cmd_task_handle);
    /* 创建解锁执行任务 */
    xTaskCreate(vUnlockExecTask, "UnlockExecTask", 512, NULL, 3, NULL);

    // 退出临界区
    taskEXIT_CRITICAL();

    // 初始显示 - 默认显示"键盘解锁"状态
    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
        oled_clear();
        oled_show_string(0, 0, "System Ready!");
        oled_show_string(0, 2, "1-Up 2-Down");
        oled_show_string(0, 4, "#-Confirm *-Exit");
        oled_show_string(0, 6, "Wait Unlock...");
        xSemaphoreGive(oledMutex);
    }

    // 删除初始化任务
    vTaskDelete(NULL);
}

// 键盘菜单任务 - 处理键盘解锁模式
void vKeyMenuTask(void* pvParameters)
{
    char key;
    uint8_t selected_mode = 0;
    uint8_t unlocking = 0;      // 是否正在解锁
    uint8_t i;                   // 循环变量
    UnlockRequest unlock_req;
    UnlockResult unlock_result;
    
    for(;;)
    {
        // 等待键盘菜单队列的消息
        if(xQueueReceive(keyMenuQueue, &key, portMAX_DELAY) == pdTRUE) {
            key_feedback();  // 按键反馈
            
            // 按*键：退出键盘模式或取消解锁
            if(key == '*') {
                if(unlocking) {
                    printf("[Keyboard] Unlock cancelled!\r\n");
                    unlock_executing = 0;
                    unlocking = 0;
                } else {
                    keyboard_mode_disable();
                    selected_mode = 0;
                }
                continue;
            }
            
            // 激活键盘模式
            if(!keyboard_mode_active) {
                keyboard_mode_enable();
                selected_mode = 0;
            }
            
            if(unlocking) {
                // 正在执行解锁时，忽略除了*之外的其他菜单按键
                continue;
            }
            
            // 按1键：向上选择
            if(key == '1') {
                if(selected_mode > 0) {
                    selected_mode--;
                } else {
                    selected_mode = MODE_COUNT - 1;
                }
                current_mode = selected_mode;
            }
            
            // 按2键：向下选择
            if(key == '2') {
                if(selected_mode < MODE_COUNT - 1) {
                    selected_mode++;
                } else {
                    selected_mode = 0;
                }
                current_mode = selected_mode;
            }
            
            // 按#键：确认选择并执行解锁
            if(key == '#') {
                unlocking = 1;
                unlock_executing = 1;
                
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    oled_show_string(0, 0, "Selected:");
                    oled_show_string(0, 2, (char*)mode_full_names[selected_mode]);
                    oled_show_string(0, 4, "Executing...");
                    oled_show_string(0, 6, "* to cancel");
                    xSemaphoreGive(oledMutex);
                }
                
                printf("[Keyboard] Starting unlock: %s\r\n", mode_full_names[selected_mode]);
                
                unlock_req.mode = selected_mode;
                unlock_req.timeout_ms = 10000;
                xQueueSend(unlockReqQueue, &unlock_req, portMAX_DELAY);
                
                if(xQueueReceive(unlockResultQueue, &unlock_result, pdMS_TO_TICKS(12000)) == pdTRUE) {
                    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                        oled_clear();
                        if(unlock_result.success) {
                            oled_show_string(0, 2, "SUCCESS!");
                            printf("[Keyboard] Unlock SUCCESS! Mode: %s\r\n", mode_full_names[selected_mode]);
                            D1 = 0;
                            vTaskDelay(pdMS_TO_TICKS(2000));
                            D1 = 1;
                        } else {
                            oled_show_string(0, 2, "FAILED!");
                            printf("[Keyboard] Unlock FAILED! Mode: %s\r\n", mode_full_names[selected_mode]);
                            vTaskDelay(pdMS_TO_TICKS(1000));
                        }
                        xSemaphoreGive(oledMutex);
                    }
                } else {
                    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                        oled_clear();
                        oled_show_string(0, 2, "TIMEOUT!");
                        xSemaphoreGive(oledMutex);
                    }
                    printf("[Keyboard] Unlock TIMEOUT! Mode: %s\r\n", mode_full_names[selected_mode]);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                
                unlocking = 0;
                unlock_executing = 0;
                
                // 清空键盘菜单队列，避免处理解锁过程中积累的按键
                {
                    char dummy_key;
                    while(xQueueReceive(keyMenuQueue, &dummy_key, 0) == pdTRUE);
                }
            }
            
            // 更新OLED显示
            if(!unlocking) {
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    oled_show_string(0, 0, "Select Mode:");
                    
                    for(i = 0; i < MODE_COUNT; i++) {
                        if(i == selected_mode) {
                            oled_show_string(0, 2 + i*2, ">");
                            oled_show_string(8, 2 + i*2, (char*)mode_names[i]);
                        } else {
                            oled_show_string(0, 2 + i*2, " ");
                            oled_show_string(8, 2 + i*2, (char*)mode_names[i]);
                        }
                    }
                    
                    xSemaphoreGive(oledMutex);
                }
            }
        }
    }
}

// 1. 指纹识别任务
void vFingerTask(void* pvParameters)
{
    uint16_t finger_id = 0;
    uint32_t cmd = 1;
    for(;;)
    {
        // 如果键盘模式激活，挂起自己
        if(keyboard_mode_active) {
            vTaskSuspend(NULL);
            continue;
        }
        
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
        // 如果键盘模式激活，挂起自己
        if(keyboard_mode_active) {
            vTaskSuspend(NULL);
            continue;
        }
        
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
            if(keyboard_mode_active) {
                // 键盘模式下，发送到键盘菜单队列
                xQueueSend(keyMenuQueue, &key_val, 10);
                // 如果正在执行解锁，同时也发送到按键队列供解锁任务使用
                if(unlock_executing) {
                    xQueueSend(keyQueue, &key_val, 10);
                }
            } else {
                // 非键盘模式下，按键同时发送到两个队列
                // 发送到按键队列（供密码解锁和键盘菜单激活使用）
                xQueueSend(keyQueue, &key_val, 10);
                // 发送到键盘菜单队列（供激活键盘模式使用）
                xQueueSend(keyMenuQueue, &key_val, 10);
            }
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
            // 如果键盘模式激活，忽略其他解锁消息
            if(keyboard_mode_active) {
                continue;
            }
            
            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                oled_clear();
                if(msg_cmd == 1)      oled_show_string(0, 0, "Finger OK!");
                else if(msg_cmd == 2) oled_show_string(0, 0, "Password OK!");
                else if(msg_cmd == 3) oled_show_string(0, 0, "RFID OK!");
                
                xSemaphoreGive(oledMutex);
                
                // 关闭成功提示
                vTaskDelay(pdMS_TO_TICKS(2000));
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    oled_show_string(0, 0, "System Ready!");
                    oled_show_string(0, 2, "1-Up 2-Down");
                    oled_show_string(0, 4, "#-Confirm *-Exit");
                    oled_show_string(0, 6, "Wait Unlock...");
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
        if(xSemaphoreTake(rc522Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(rc522_read_card(&card_id)) {
                last_card_uid = card_id;
                
                if(keyboard_mode_active) {
                    // 键盘模式下，只更新卡片ID，不自动解锁（由解锁执行任务处理）
                    printf("[RFID] Card detected (keyboard mode): %08X\r\n", card_id);
                } else {
                    // 非键盘模式下，执行自动解锁
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
            }
            xSemaphoreGive(rc522Mutex);
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

// 解锁执行任务 - 执行实际的解锁操作
void vUnlockExecTask(void* pvParameters)
{
    UnlockRequest req;
    UnlockResult res;
    uint16_t finger_id = 0;
    char input_buf[17] = {0};
    uint8_t index = 0;
    char key_char;
    uint32_t card_id = 0;
    uint32_t start_time;
    
    for(;;)
    {
        if(xQueueReceive(unlockReqQueue, &req, portMAX_DELAY) == pdTRUE) {
            res.mode = req.mode;
            res.success = 0;
            
            switch(req.mode) {
                case MODE_FINGER:
                    printf("[UnlockExec] Executing Finger unlock...\r\n");
                    start_time = xTaskGetTickCount();
                    while((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(req.timeout_ms)) {
                        if(as608_search_finger(&finger_id)) {
                            printf("[UnlockExec] Finger found! ID: %d\r\n", finger_id);
                            res.success = 1;
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(300));
                    }
                    break;
                    
                case MODE_PASSWORD:
                    printf("[UnlockExec] Executing Password unlock...\r\n");
                    memset(input_buf, 0, sizeof(input_buf));
                    index = 0;
                    start_time = xTaskGetTickCount();
                    
                    // 清空按键队列，避免处理之前的按键
                    while(xQueueReceive(keyQueue, &key_char, 0) == pdTRUE);
                    
                    while((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(req.timeout_ms)) {
                        if(xQueueReceive(keyQueue, &key_char, pdMS_TO_TICKS(100)) == pdTRUE) {
                            if(key_char == '#') {
                                input_buf[index] = '\0';
                                if(strstr(input_buf, correct_pwd_1) != NULL || 
                                   strstr(input_buf, correct_pwd_2) != NULL) {
                                    printf("[UnlockExec] Password correct!\r\n");
                                    res.success = 1;
                                } else {
                                    printf("[UnlockExec] Password incorrect!\r\n");
                                }
                                break;
                            } else if(key_char == '*') {
                                printf("[UnlockExec] Password input cancelled!\r\n");
                                break;
                            } else if(index < 16) {
                                input_buf[index++] = key_char;
                                printf("[UnlockExec] Input: %c\r\n", key_char);
                            }
                        }
                    }
                    break;
                    
                case MODE_RFID:
                    printf("[UnlockExec] Executing RFID unlock...\r\n");
                    start_time = xTaskGetTickCount();
                    while((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(req.timeout_ms)) {
                        if(xSemaphoreTake(rc522Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            if(rc522_read_card(&card_id)) {
                                last_card_uid = card_id;
                                printf("[UnlockExec] Card detected: %08X\r\n", card_id);
                                if(saved_card_uid != 0 && card_id == saved_card_uid) {
                                    printf("[UnlockExec] Card matched!\r\n");
                                    res.success = 1;
                                } else if(saved_card_uid == 0) {
                                    printf("[UnlockExec] No card saved!\r\n");
                                } else {
                                    printf("[UnlockExec] Card not matched!\r\n");
                                }
                                xSemaphoreGive(rc522Mutex);
                                break;
                            }
                            xSemaphoreGive(rc522Mutex);
                        }
                        vTaskDelay(pdMS_TO_TICKS(200));
                    }
                    break;
                    

            }
            
            xQueueSend(unlockResultQueue, &res, portMAX_DELAY);
            printf("[UnlockExec] Unlock result: mode=%d, success=%d\r\n", res.mode, res.success);
        }
    }
}

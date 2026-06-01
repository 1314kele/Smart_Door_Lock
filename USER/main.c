/*
 * ============================================================================
 * 智能门锁系统 - 主程序文件
 * ============================================================================
 * 文件名称: main.c
 * 功能描述: 基于FreeRTOS的智能门锁主控程序，实现指纹、密码、RFID三种解锁方式
 * 硬件平台: STM32F407
 * 操作系统: FreeRTOS
 * 作者: 
 * 日期: 
 * ============================================================================
 * 系统架构:
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │                      FreeRTOS 任务架构                          │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │  Priority 5: vKeyMenuTask    - 菜单导航任务（最高优先级）       │
 *   │              vManageTask     - 管理模式任务                    │
 *   │              app_task_init   - 初始化任务（一次性）             │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │  Priority 4: vKeyTask        - 按键扫描任务                    │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │  Priority 3: vFingerTask     - 指纹识别任务                    │
 *   │              vPwdTask        - 密码验证任务                    │
 *   │              vRfidTask       - RFID识别任务                    │
 *   │              vUartCmdTask    - 串口命令处理任务                 │
 *   │              vUnlockExecTask - 解锁执行任务（备用）             │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │  Priority 2: vOledTask       - OLED显示任务（最低优先级）       │
 *   └─────────────────────────────────────────────────────────────────┘
 * ============================================================================
 */

#include "includes.h"
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "oled.h"
#include "rc522.h"
#include "as608.h"
#include "keypad.h"
#include "uart.h"
#include "flash.h"

// ==================== 任务句柄定义 ====================
/*
 * 任务句柄用于任务管理（挂起/恢复/删除等操作）
 */
TaskHandle_t app_task_init_handle = NULL;     // 初始化任务句柄
TaskHandle_t finger_task_handle = NULL;       // 指纹识别任务句柄
TaskHandle_t pwd_task_handle = NULL;          // 密码验证任务句柄
TaskHandle_t key_task_handle = NULL;          // 按键扫描任务句柄
TaskHandle_t keyMenu_task_handle = NULL;      // 菜单导航任务句柄
TaskHandle_t oled_task_handle = NULL;         // OLED显示任务句柄
TaskHandle_t rfid_task_handle = NULL;         // RFID识别任务句柄
TaskHandle_t uart_cmd_task_handle = NULL;     // 串口命令任务句柄
TaskHandle_t unlockExec_task_handle = NULL;   // 解锁执行任务句柄（备用）
TaskHandle_t manage_task_handle = NULL;       // 管理模式任务句柄

// ==================== 队列定义 ====================
/*
 * 队列用于任务间通信
 */
QueueHandle_t msgQueue = NULL;                // 消息队列（解锁成功通知）
QueueHandle_t keyQueue = NULL;                // 按键队列（密码输入）
QueueHandle_t keyMenuQueue = NULL;            // 菜单按键队列
QueueHandle_t unlockReqQueue = NULL;          // 解锁请求队列（备用）
QueueHandle_t unlockResultQueue = NULL;       // 解锁结果队列（备用）
QueueHandle_t manageQueue = NULL;             // 管理模式按键队列

// ==================== 信号量定义 ====================
/*
 * 信号量用于保护共享资源，防止并发访问冲突
 */
SemaphoreHandle_t oledMutex = NULL;           // OLED显示互斥锁
SemaphoreHandle_t as608Mutex = NULL;          // 指纹模块互斥锁
SemaphoreHandle_t rc522Mutex = NULL;          // RFID模块互斥锁

// ==================== 全局状态标志位 ====================
/*
 * volatile 关键字确保变量在多任务环境下的可见性
 */
volatile uint8_t keyboard_mode_active = 0;    // 是否处于键盘输入模式（0=否，1=是）
volatile uint8_t current_mode = 0;            // 当前系统模式
volatile uint8_t unlock_executing = 0;        // 是否正在执行解锁操作
volatile uint8_t manage_mode_active = 0;      // 是否进入管理模式
volatile uint8_t manage_submenu_active = 0;   // 是否进入管理子菜单
volatile uint8_t unlock_mode_active = 0;      // 是否进入解锁模式
volatile uint8_t selected_unlock_mode = 0;    // 当前选择的解锁方式（0=无, 1=指纹, 2=密码, 3=RFID）
volatile uint8_t admin_pwd_mode_active = 0;   // 是否正在输入管理员密码

// ==================== 枚举类型定义 ====================

/*
 * 解锁方式枚举
 */
typedef enum {
    MODE_FINGER = 0,      // 指纹解锁模式
    MODE_PASSWORD = 1,    // 密码解锁模式
    MODE_RFID = 2,        // RFID解锁模式
    MODE_COUNT = 3        // 模式总数
} UnlockMode;

/*
 * 菜单状态枚举
 */
typedef enum {
    MENU_MAIN = 0,           // 主菜单
    MENU_UNLOCK = 1,         // 解锁模式选择菜单
    MENU_MANAGE_PASS = 2,    // 管理员密码输入
    MENU_MANAGE = 3,         // 管理模式主菜单
    MENU_MANAGE_FINGER = 4,  // 指纹管理子菜单
    MENU_MANAGE_PWD = 5,     // 密码管理子菜单
    MENU_MANAGE_RFID = 6     // RFID管理子菜单
} MenuState;

// ==================== 常量字符串定义 ====================

/*
 * 解锁方式名称（简短版，用于菜单显示）
 */
const char* mode_names[] = {
    "1.Finger",
    "2.Password",
    "3.RFID"
};

/*
 * 解锁方式名称（完整版，用于详细显示）
 */
const char* mode_full_names[] = {
    "Finger Mode",
    "Password Mode",
    "RFID Mode"
};

/*
 * 主菜单项
 */
const char* menu_main_items[] = {
    "1.Unlock",
    "2.Manage"
};

/*
 * 管理模式菜单项
 */
const char* menu_manage_items[] = {
    "1.Finger",
    "2.Password",
    "3.RFID"
};

// ==================== 全局变量 ====================

extern uint32_t saved_card_uid;               // 外部引用：保存的卡片UID
uint32_t last_card_uid = 0;                   // 最近读取的卡片UID

// ==================== 数据结构定义 ====================

/*
 * 解锁请求结构体（备用）
 */
typedef struct {
    uint8_t mode;          // 解锁方式
    uint32_t timeout_ms;   // 超时时间（毫秒）
} UnlockRequest;

/*
 * 解锁结果结构体（备用）
 */
typedef struct {
    uint8_t mode;          // 解锁方式
    uint8_t success;       // 是否成功（0=失败，1=成功）
} UnlockResult;

// ==================== 函数声明 ====================

void app_task_init(void* pvParameters);       // 初始化任务
void vFingerTask(void* pvParameters);        // 指纹识别任务
void vPwdTask(void* pvParameters);           // 密码验证任务
void vKeyTask(void* pvParameters);           // 按键扫描任务
void vKeyMenuTask(void* pvParameters);       // 菜单导航任务
void vOledTask(void* pvParameters);          // OLED显示任务
void vRfidTask(void* pvParameters);         // RFID识别任务
void vUartCmdTask(void* pvParameters);       // 串口命令任务
void vUnlockExecTask(void* pvParameters);    // 解锁执行任务（备用）
void vManageTask(void* pvParameters);        // 管理模式任务

// ==================== 辅助函数 ====================

/*
 * 按键反馈函数
 * 点亮D1 LED 50ms作为按键按下的视觉反馈
 */
void key_feedback(void) {
    D1 = 0;        // 点亮LED
    delay_ms(50);  // 延迟50ms
    D1 = 1;        // 熄灭LED
}

/*
 * 启用键盘模式
 * - 设置键盘模式标志
 * - 挂起指纹任务（避免冲突）
 */
void keyboard_mode_enable(void) {
    keyboard_mode_active = 1;
    if(finger_task_handle) vTaskSuspend(finger_task_handle);
    // 不挂起pwd_task，因为它需要处理键盘输入
    printf("[Keyboard] Mode activated!\r\n");
}

/*
 * 禁用键盘模式
 * - 清除键盘模式标志
 * - 恢复指纹任务
 */
void keyboard_mode_disable(void) {
    keyboard_mode_active = 0;
    current_mode = 0;
    if(finger_task_handle) vTaskResume(finger_task_handle);
    // pwd_task没有被挂起
    printf("[Keyboard] Mode deactivated!\r\n");
}

/*
 * 启用管理模式
 * - 设置管理模式标志
 * - 挂起所有解锁相关任务（指纹、密码、RFID）
 */
void manage_mode_enable(void) {
    manage_mode_active = 1;
    if(finger_task_handle) vTaskSuspend(finger_task_handle);
    if(pwd_task_handle) vTaskSuspend(pwd_task_handle);
    if(rfid_task_handle) vTaskSuspend(rfid_task_handle);
    printf("[Manage] Mode activated!\r\n");
}

/*
 * 禁用管理模式
 * - 清除管理模式标志
 * - 恢复所有解锁相关任务
 */
void manage_mode_disable(void) {
    manage_mode_active = 0;
    manage_submenu_active = 0; // 同时清除子菜单标志
    if(finger_task_handle) vTaskResume(finger_task_handle);
    if(pwd_task_handle) vTaskResume(pwd_task_handle);
    if(rfid_task_handle) vTaskResume(rfid_task_handle);
    printf("[Manage] Mode deactivated!\r\n");
}

// ==================== 主函数 ====================

/*
 * 程序入口点
 * - 硬件初始化
 * - 创建初始化任务
 * - 启动FreeRTOS调度器
 */
int main()
{
    int i;
    uint32_t *raw_addr;
    
    // 1. 系统初始化
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);  // 配置中断优先级分组
    delay_init();                                    // 延时函数初始化
    led_init();                                      // LED初始化
    keypad_init();                                   // 矩阵键盘初始化
    oled_init();                                     // OLED显示屏初始化
    rc522_init();                                    // RFID模块初始化
    as608_init();                                    // 指纹模块初始化
    flash_init();                                    // Flash存储初始化
    
    // 2. 串口初始化
    uart1_init(115200);  // UART1：调试输出
    uart2_init(57600);   // UART2：指纹模块通信
    uart3_init(115200);  // UART3：WiFi模块通信
    
    // 3. 系统启动信息
    printf("Smart Door Lock System Start!\n");
    printf("RC522 Version: 0x%02X\r\n", rc522_ver);
    
    // 4. 调试信息：打印Flash中的数据
    printf("=== Flash Init Debug ===\r\n");
    printf("Card count: %d\r\n", flash_get_card_count());
    for(i = 0; i < flash_get_card_count(); i++) {
        printf("  Card[%d]: %08X\r\n", i, flash_get_card(i));
    }
    printf("Password count: %d\r\n", flash_get_password_count());
    for(i = 0; i < flash_get_password_count(); i++) {
        printf("  Pwd[%d]: %s\r\n", i, flash_get_password(i));
    }
    
    // 5. 调试信息：直接读取Flash原始数据
    raw_addr = (uint32_t *)0x080E0000;
    printf("Raw Flash content:\r\n");
    for(i = 0; i < 10; i++) {
        printf("  [0x%08X]: 0x%08X\r\n", (uint32_t)(raw_addr + i), raw_addr[i]);
    }
    printf("========================\r\n");

    // 6. 创建初始化任务（优先级5，堆栈512字节）
    xTaskCreate((TaskFunction_t )app_task_init,   
                (const char*    )"app_task_init", 
                (uint16_t       )512,             
                (void*          )NULL,            
                (UBaseType_t    )5,               
                (TaskHandle_t*  )&app_task_init_handle); 

    // 7. 启动FreeRTOS调度器
    vTaskStartScheduler();

    // 8. 如果调度器启动失败，进入死循环
    while(1);
}

// ==================== 初始化任务 ====================

/*
 * 初始化任务（一次性执行）
 * - 创建所有队列和信号量
 * - 创建所有业务任务
 * - 显示系统就绪界面
 * - 初始化WiFi连接
 * - 最后删除自身
 */
void app_task_init(void* pvParameters)
{
    printf("app_task_init running!\r\n");

    // 1. 创建队列
    msgQueue = xQueueCreate(10, sizeof(uint32_t));          // 消息队列（10个消息）
    keyQueue = xQueueCreate(16, sizeof(char));              // 按键队列（16个按键）
    keyMenuQueue = xQueueCreate(16, sizeof(char));          // 菜单按键队列
    unlockReqQueue = xQueueCreate(5, sizeof(UnlockRequest)); // 解锁请求队列
    unlockResultQueue = xQueueCreate(5, sizeof(UnlockResult));// 解锁结果队列
    manageQueue = xQueueCreate(16, sizeof(char));           // 管理模式队列
    
    // 2. 创建信号量（互斥锁）
    oledMutex = xSemaphoreCreateMutex();   // OLED显示互斥锁
    as608Mutex = xSemaphoreCreateMutex();  // 指纹模块互斥锁
    rc522Mutex = xSemaphoreCreateMutex();  // RFID模块互斥锁

    // 3. 进入临界区（禁止任务调度）
    taskENTER_CRITICAL();

    // 4. 创建所有业务任务
    xTaskCreate(vFingerTask, "FingerTask", 512, NULL, 3, &finger_task_handle);      
    xTaskCreate(vPwdTask, "PwdTask", 512, NULL, 3, &pwd_task_handle);      
    xTaskCreate(vKeyTask, "KeyTask", 512, NULL, 4, &key_task_handle);        
    xTaskCreate(vKeyMenuTask, "KeyMenuTask", 512, NULL, 5, &keyMenu_task_handle);        
    xTaskCreate(vOledTask, "OledTask", 512, NULL, 2, &oled_task_handle);
    xTaskCreate(vRfidTask, "RfidTask", 512, NULL, 3, &rfid_task_handle);
    xTaskCreate(vUartCmdTask, "UartCmdTask", 256, NULL, 3, &uart_cmd_task_handle);
    xTaskCreate(vUnlockExecTask, "UnlockExecTask", 512, NULL, 3, &unlockExec_task_handle);
    xTaskCreate(vManageTask, "ManageTask", 512, NULL, 5, &manage_task_handle);

    // 5. 退出临界区（允许任务调度）
    taskEXIT_CRITICAL();

    // 6. 显示系统就绪界面
    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
        oled_clear();
        oled_show_string(0, 0, "System Ready!");
        oled_show_string(0, 2, "1-Unlock");
        oled_show_string(0, 4, "2-Manage");
        oled_show_string(0, 6, "Press 1 or 2");
        xSemaphoreGive(oledMutex);
    }
    
    // 7. 自动初始化WiFi连接
    wifi_auto_connect();
    
    // 8. 删除初始化任务（已完成使命）
    vTaskDelete(NULL);
}

// ==================== 菜单导航任务 ====================

/*
 * 菜单导航任务（优先级5）
 * - 处理用户按键输入
 * - 管理菜单状态切换
 * - 控制解锁流程
 * - 管理管理员密码验证
 */
void vKeyMenuTask(void* pvParameters)
{
    char key;                      // 当前按键
    char dummy;                    // 用于清空队列的临时变量
    uint8_t selected_item = 0;     // 当前选中项
    MenuState menu_state = MENU_MAIN;  // 当前菜单状态
    UnlockRequest unlock_req;      // 解锁请求（备用）
    UnlockResult unlock_result;    // 解锁结果（备用）
    uint8_t max_items = 0;         // 当前菜单最大项数
    char card_str[10];             // 卡片UID字符串
    uint8_t unlock_in_progress = 0;// 解锁是否进行中
    char admin_pwd_buf[17] = {0};  // 管理员密码输入缓冲区
    uint8_t admin_pwd_index = 0;   // 管理员密码输入索引
    
    // 任务主循环
    for(;;)
    {
        // 从菜单按键队列读取按键（阻塞等待）
        if(xQueueReceive(keyMenuQueue, &key, portMAX_DELAY) == pdTRUE) {
            // 1. 如果解锁任务正在进行中，忽略按键
            if(unlock_in_progress) {
                continue;
            }
            
            // 2. 如果已经进入管理子菜单，忽略按键（由vManageTask处理）
            if(manage_submenu_active) {
                continue;
            }
            
            // 3. 如果从管理子菜单返回，重置菜单状态
            if(menu_state >= MENU_MANAGE_FINGER) {
                menu_state = MENU_MANAGE;
            }
            
            // 4. 如果解锁模式已经退出，但menu_state还在MENU_UNLOCK，重置为MENU_MAIN
            if(menu_state == MENU_UNLOCK && !unlock_mode_active) {
                menu_state = MENU_MAIN;
            }
            
            // 5. 按键反馈
            key_feedback();
            
            // 6. 全局返回键处理（*键）
            if(key == '*') {
                menu_state = MENU_MAIN;
                keyboard_mode_disable();
                manage_mode_disable();
                selected_item = 0;
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    oled_show_string(0, 0, "System Ready!");
                    oled_show_string(0, 2, "1-Unlock");
                    oled_show_string(0, 4, "2-Manage");
                    oled_show_string(0, 6, "Press 1 or 2");
                    xSemaphoreGive(oledMutex);
                }
                continue;
            }
            
            // 7. 根据当前菜单状态处理按键
            switch(menu_state) {
                // ========== 主菜单 ==========
                case MENU_MAIN:
                    if(key == '1') {
                        // 进入解锁模式选择
                        selected_item = 0;
                        menu_state = MENU_UNLOCK;
                        unlock_mode_active = 1;
                        selected_unlock_mode = 0;
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Unlock Mode:");
                            oled_show_string(0, 2, "1.Finger");
                            oled_show_string(0, 4, "2.Password");
                            oled_show_string(0, 6, "3.RFID *-Back");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '2') {
                        // 进入管理员密码输入
                        selected_item = 0;
                        menu_state = MENU_MANAGE_PASS;
                        admin_pwd_mode_active = 1;
                        admin_pwd_index = 0;
                        memset(admin_pwd_buf, 0, sizeof(admin_pwd_buf));
                        // 清空keyQueue，避免之前的按键影响
                        while(xQueueReceive(keyQueue, &dummy, 0) == pdTRUE);
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Enter Admin Pwd:");
                            oled_show_string(0, 2, "");
                            oled_show_string(0, 4, "#-OK *-Cancel");
                            xSemaphoreGive(oledMutex);
                        }
                    }
                    break;
                    
                // ========== 管理员密码输入 ==========
                case MENU_MANAGE_PASS:
                    if(key == '*') {
                        // 取消，返回主菜单
                        admin_pwd_mode_active = 0;
                        menu_state = MENU_MAIN;
                        admin_pwd_index = 0;
                        memset(admin_pwd_buf, 0, sizeof(admin_pwd_buf));
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "System Ready!");
                            oled_show_string(0, 2, "1-Unlock");
                            oled_show_string(0, 4, "2-Manage");
                            oled_show_string(0, 6, "Press 1 or 2");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '#') {
                        // 验证密码
                        admin_pwd_buf[admin_pwd_index] = '\0';
                        if(strcmp(admin_pwd_buf, flash_get_admin_password()) == 0) {
                            // 密码正确，进入管理模式
                            admin_pwd_mode_active = 0;
                            menu_state = MENU_MANAGE;
                            manage_mode_enable();
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                oled_show_string(0, 0, "Manage Mode:");
                                oled_show_string(0, 2, "1.Finger");
                                oled_show_string(0, 4, "2.Password");
                                oled_show_string(0, 6, "3.RFID *-Back");
                                xSemaphoreGive(oledMutex);
                            }
                        } else {
                            // 密码错误
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                oled_show_string(0, 0, "Wrong Password!");
                                xSemaphoreGive(oledMutex);
                            }
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            // 重新输入密码
                            admin_pwd_index = 0;
                            memset(admin_pwd_buf, 0, sizeof(admin_pwd_buf));
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                oled_show_string(0, 0, "Enter Admin Pwd:");
                                oled_show_string(0, 2, "");
                                oled_show_string(0, 4, "#-OK *-Cancel");
                                xSemaphoreGive(oledMutex);
                            }
                        }
                    } else if(key >= '0' && key <= '9' && admin_pwd_index < 16) {
                        // 输入数字
                        admin_pwd_buf[admin_pwd_index++] = key;
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Enter Admin Pwd:");
                            oled_show_string(0, 2, admin_pwd_buf);
                            oled_show_string(0, 4, "#-OK *-Cancel");
                            xSemaphoreGive(oledMutex);
                        }
                    }
                    break;
                    
                // ========== 解锁模式选择 ==========
                case MENU_UNLOCK:
                    // 如果正在键盘输入模式，忽略菜单按键
                    if(keyboard_mode_active) {
                        break;
                    }
                    
                    if(key == '1') {
                        // 选择指纹解锁
                        selected_unlock_mode = 1;
                        unlock_mode_active = 1;
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Finger Unlock");
                            oled_show_string(0, 2, "Place finger...");
                            oled_show_string(0, 6, "* to cancel");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '2') {
                        // 选择密码解锁
                        printf("[vKeyMenuTask] Selected password unlock mode\r\n");
                        selected_unlock_mode = 2;
                        unlock_mode_active = 1;
                        keyboard_mode_enable();
                        // 清空keyQueue，避免之前的按键影响
                        {
                            char dummy;
                            while(xQueueReceive(keyQueue, &dummy, 0) == pdTRUE);
                        }
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Password Unlock");
                            oled_show_string(0, 2, "Input password:");
                            oled_show_string(0, 6, "#-OK *-Cancel");
                            xSemaphoreGive(oledMutex);
                        }
                        // 唤醒密码任务
                        if(pwd_task_handle) {
                            vTaskResume(pwd_task_handle);
                        }
                    } else if(key == '3') {
                        // 选择RFID解锁
                        selected_unlock_mode = 3;
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "RFID Unlock");
                            oled_show_string(0, 2, "Scan card...");
                            oled_show_string(0, 6, "* to cancel");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '*') {
                        // 取消解锁，返回主菜单
                        unlock_mode_active = 0;
                        selected_unlock_mode = 0;
                        menu_state = MENU_MAIN;
                        keyboard_mode_disable();
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "System Ready!");
                            oled_show_string(0, 2, "1-Unlock");
                            oled_show_string(0, 4, "2-Manage");
                            oled_show_string(0, 6, "Press 1 or 2");
                            xSemaphoreGive(oledMutex);
                        }
                    }
                    break;
                    
                // ========== 管理模式主菜单 ==========
                case MENU_MANAGE:
                    if(key == '1') {
                        // 进入指纹管理
                        menu_state = MENU_MANAGE_FINGER;
                        selected_item = 0;
                        manage_submenu_active = 1;
                        // 发送初始化命令告诉vManageTask
                        key = '@';
                        xQueueSend(manageQueue, &key, portMAX_DELAY);
                        key = '1';
                        xQueueSend(manageQueue, &key, portMAX_DELAY);
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Finger Manage:");
                            oled_show_string(0, 2, "Sel: 0");
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "3-Up 4-Down");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '2') {
                        // 进入密码管理
                        menu_state = MENU_MANAGE_PWD;
                        selected_item = 0;
                        max_items = flash_get_password_count();
                        manage_submenu_active = 1;
                        key = '@';
                        xQueueSend(manageQueue, &key, portMAX_DELAY);
                        key = '2';
                        xQueueSend(manageQueue, &key, portMAX_DELAY);
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Pwd Manage:");
                            if(max_items > 0) {
                                oled_show_string(0, 2, flash_get_password(0));
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "3-Up 4-Down");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '3') {
                        // 进入RFID管理
                        menu_state = MENU_MANAGE_RFID;
                        selected_item = 0;
                        max_items = flash_get_card_count();
                        manage_submenu_active = 1;
                        key = '@';
                        xQueueSend(manageQueue, &key, portMAX_DELAY);
                        key = '3';
                        xQueueSend(manageQueue, &key, portMAX_DELAY);
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "RFID Manage:");
                            if(max_items > 0) {
                                sprintf(card_str, "%08X", flash_get_card(0));
                                oled_show_string(0, 2, card_str);
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "3-Up 4-Down");
                            xSemaphoreGive(oledMutex);
                        }
                    }
                    break;
                    
                // ========== 管理子菜单（转发给vManageTask处理） ==========
                case MENU_MANAGE_FINGER:
                case MENU_MANAGE_PWD:
                case MENU_MANAGE_RFID:
                    xQueueSend(manageQueue, &key, portMAX_DELAY);
                    break;
            }
        }
    }
}

// ==================== 管理模式任务 ====================

/*
 * 管理模式任务（优先级5）
 * - 处理指纹/密码/RFID的添加和删除
 * - 管理子菜单导航
 */
void vManageTask(void* pvParameters)
{
    char key;                          // 当前按键
    uint8_t selected_index = 0;        // 当前选中的项目索引
    uint8_t max_items = 0;             // 当前菜单最大项目数
    MenuState current_manage_menu = MENU_MANAGE_FINGER; // 当前管理子菜单
    char pwd_buffer[17] = {0};         // 密码输入缓冲区
    uint8_t pwd_index = 0;             // 密码输入索引
    uint8_t enrolling = 0;             // 是否正在录入状态
    uint32_t card_id = 0;              // 卡片ID
    char hex_str[10];                  // 十六进制字符串缓冲区
    char confirm_key;                  // 确认按键
    char finger_info[20];              // 指纹信息字符串
    char card_str[10];                 // 卡片信息字符串
    uint8_t idx;                       // 临时索引
    
    // 任务主循环
    for(;;)
    {
        // 从管理队列读取按键（阻塞等待）
        if(xQueueReceive(manageQueue, &key, portMAX_DELAY) == pdTRUE) {
            // 1. 处理初始化命令 '@'
            if(key == '@') {
                // 读取下一个字符确定当前菜单
                if(xQueueReceive(manageQueue, &key, pdMS_TO_TICKS(100)) == pdTRUE) {
                    switch(key) {
                        case '1':
                            current_manage_menu = MENU_MANAGE_FINGER;
                            break;
                        case '2':
                            current_manage_menu = MENU_MANAGE_PWD;
                            break;
                        case '3':
                            current_manage_menu = MENU_MANAGE_RFID;
                            break;
                    }
                }
                continue; // 初始化命令，不处理其他逻辑
            }
            
            // 2. 按键反馈
            key_feedback();
            
            // 3. 返回键处理（*键）
            if(key == '*') {
                if(enrolling) {
                    // 如果正在录入，先退出录入状态
                    enrolling = 0;
                    pwd_index = 0;
                    memset(pwd_buffer, 0, sizeof(pwd_buffer));
                    // 重新显示子菜单
                    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                        oled_clear();
                        if(current_manage_menu == MENU_MANAGE_FINGER) {
                            oled_show_string(0, 0, "Finger Manage:");
                            oled_show_string(0, 2, "Sel: 0");
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "*-Back");
                        } else if(current_manage_menu == MENU_MANAGE_PWD) {
                            max_items = flash_get_password_count();
                            oled_show_string(0, 0, "Pwd Manage:");
                            if(max_items > 0) {
                                oled_show_string(0, 2, flash_get_password(0));
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "*-Back");
                        } else if(current_manage_menu == MENU_MANAGE_RFID) {
                            max_items = flash_get_card_count();
                            oled_show_string(0, 0, "RFID Manage:");
                            if(max_items > 0) {
                                sprintf(card_str, "%08X", flash_get_card(0));
                                oled_show_string(0, 2, card_str);
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "*-Back");
                        }
                        xSemaphoreGive(oledMutex);
                    }
                } else {
                    // 如果不在录入状态，返回到管理主菜单
                    enrolling = 0;
                    pwd_index = 0;
                    memset(pwd_buffer, 0, sizeof(pwd_buffer));
                    selected_index = 0;
                    manage_submenu_active = 0;
                    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                        oled_clear();
                        oled_show_string(0, 0, "Manage Mode:");
                        oled_show_string(0, 2, "1.Finger");
                        oled_show_string(0, 4, "2.Password");
                        oled_show_string(0, 6, "3.RFID *-Back");
                        xSemaphoreGive(oledMutex);
                    }
                }
                continue;
            }
            
            // 4. 录入状态处理
            if(enrolling) {
                if(current_manage_menu == MENU_MANAGE_PWD) {
                    // 密码录入
                    if(key == '#') {
                        pwd_buffer[pwd_index] = '\0';
                        if(pwd_index >= 4) {
                            if(flash_add_password(pwd_buffer)) {
                                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                    oled_clear();
                                    oled_show_string(0, 2, "Saved!");
                                    xSemaphoreGive(oledMutex);
                                }
                                wifi_notify("PASSWORD:ADD:SUCCESS");
                            } else {
                                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                    oled_clear();
                                    oled_show_string(0, 2, "Full!");
                                    xSemaphoreGive(oledMutex);
                                }
                            }
                        } else {
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                oled_show_string(0, 2, "Too Short!");
                                xSemaphoreGive(oledMutex);
                            }
                        }
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        enrolling = 0;
                        pwd_index = 0;
                        memset(pwd_buffer, 0, sizeof(pwd_buffer));
                    } else if(key >= '0' && key <= '9' && pwd_index < 16) {
                        pwd_buffer[pwd_index++] = key;
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Input PWD:");
                            oled_show_string(0, 2, pwd_buffer);
                            oled_show_string(0, 4, "Press # to save");
                            xSemaphoreGive(oledMutex);
                        }
                    }
                } else if(current_manage_menu == MENU_MANAGE_RFID) {
                    // RFID录入
                    card_id = 0;
                    if(xSemaphoreTake(rc522Mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                        if(rc522_read_card(&card_id)) {
                            if(flash_add_card(card_id)) {
                                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                    oled_clear();
                                    oled_show_string(0, 0, "Card Saved!");
                                    sprintf(hex_str, "%08X", card_id);
                                    oled_show_string(0, 2, hex_str);
                                    xSemaphoreGive(oledMutex);
                                }
                                wifi_notify("RFID:ADD:SUCCESS");
                            } else {
                                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                    oled_clear();
                                    oled_show_string(0, 2, "Full!");
                                    xSemaphoreGive(oledMutex);
                                }
                            }
                        } else {
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                oled_show_string(0, 2, "No Card!");
                                xSemaphoreGive(oledMutex);
                            }
                        }
                        xSemaphoreGive(rc522Mutex);
                    }
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    enrolling = 0;
                } else if(current_manage_menu == MENU_MANAGE_FINGER) {
                    // 指纹录入
                    if(as608_enroll_finger(selected_index)) {
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 2, "Enroll OK!");
                            xSemaphoreGive(oledMutex);
                        }
                        wifi_notify("FINGER:ADD:SUCCESS");
                    } else {
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 2, "Enroll Fail!");
                            xSemaphoreGive(oledMutex);
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    enrolling = 0;
                }
            } else {
                // 5. 非录入状态处理
                // 获取当前菜单的最大项目数
                switch(current_manage_menu) {
                    case MENU_MANAGE_FINGER:
                        max_items = 10;  // 指纹最多10个
                        break;
                    case MENU_MANAGE_PWD:
                        max_items = flash_get_password_count();
                        break;
                    case MENU_MANAGE_RFID:
                        max_items = flash_get_card_count();
                        break;
                }
                
                if(key == '1') {
                    // 删除操作
                    if(max_items > 0) {
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Delete? 1=Yes");
                            xSemaphoreGive(oledMutex);
                        }
                        // 清空队列中残留的按键
                        {
                            char dummy;
                            while(xQueueReceive(manageQueue, &dummy, 0) == pdTRUE);
                        }
                        // 等待用户确认
                        if(xQueueReceive(manageQueue, &confirm_key, pdMS_TO_TICKS(5000)) == pdTRUE) {
                            if(confirm_key == '1') {
                                switch(current_manage_menu) {
                                    case MENU_MANAGE_FINGER:
                                        printf("[Manage] Deleting finger ID=%d\r\n", selected_index);
                                        if(as608_delete_finger(selected_index)) {
                                            wifi_notify("FINGER:DELETE:SUCCESS");
                                        } else {
                                            printf("[Manage] Delete finger failed!\r\n");
                                        }
                                        break;
                                    case MENU_MANAGE_PWD:
                                        flash_delete_password(selected_index);
                                        wifi_notify("PASSWORD:DELETE:SUCCESS");
                                        break;
                                    case MENU_MANAGE_RFID:
                                        flash_delete_card(selected_index);
                                        wifi_notify("RFID:DELETE:SUCCESS");
                                        break;
                                }
                                if(selected_index >= max_items - 1) {
                                    selected_index = 0;
                                }
                                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                    oled_clear();
                                    oled_show_string(0, 2, "Deleted!");
                                    xSemaphoreGive(oledMutex);
                                }
                            }
                        } else {
                            // 超时，返回子菜单
                            printf("[Manage] Delete timeout, returning...\r\n");
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                    }
                } else if(key == '2') {
                    // 添加操作
                    enrolling = 1;
                    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                        oled_clear();
                        switch(current_manage_menu) {
                            case MENU_MANAGE_FINGER:
                                oled_show_string(0, 0, "Place finger...");
                                break;
                            case MENU_MANAGE_PWD:
                                oled_show_string(0, 0, "Input PWD:");
                                oled_show_string(0, 2, "");
                                oled_show_string(0, 4, "Press # to save");
                                break;
                            case MENU_MANAGE_RFID:
                                oled_show_string(0, 0, "Scan card...");
                                break;
                        }
                        xSemaphoreGive(oledMutex);
                    }
                } else if(key == '3') {
                    // 向上选择
                    if(selected_index > 0) {
                        selected_index--;
                    }
                } else if(key == '4') {
                    // 向下选择
                    if(current_manage_menu == MENU_MANAGE_FINGER) {
                        if(selected_index < 9) {
                            selected_index++;
                        }
                    } else {
                        if(selected_index < max_items - 1) {
                            selected_index++;
                        }
                    }
                } else if(key >= '1' && key <= '9') {
                    // 直接选择数字1-9
                    idx = key - '1';
                    if(current_manage_menu == MENU_MANAGE_FINGER) {
                        if(idx < 10) {
                            selected_index = idx;
                        }
                    } else if(current_manage_menu == MENU_MANAGE_PWD) {
                        if(idx < max_items) {
                            selected_index = idx;
                        }
                    } else if(current_manage_menu == MENU_MANAGE_RFID) {
                        if(idx < max_items) {
                            selected_index = idx;
                        }
                    }
                }
            }
            
            // 6. 更新显示（非录入状态）
            if(!enrolling) {
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    switch(current_manage_menu) {
                        case MENU_MANAGE_FINGER:
                            oled_show_string(0, 0, "Finger Manage:");
                            sprintf(finger_info, "Sel: %d", selected_index);
                            oled_show_string(0, 2, finger_info);
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "3-Up 4-Down");
                            break;
                            
                        case MENU_MANAGE_PWD:
                            oled_show_string(0, 0, "Password Manage:");
                            if(max_items > 0) {
                                oled_show_string(0, 2, flash_get_password(selected_index));
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "3-Up 4-Down");
                            break;
                            
                        case MENU_MANAGE_RFID:
                            oled_show_string(0, 0, "RFID Manage:");
                            if(max_items > 0) {
                                sprintf(card_str, "%08X", flash_get_card(selected_index));
                                oled_show_string(0, 2, card_str);
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "3-Up 4-Down");
                            break;
                    }
                    xSemaphoreGive(oledMutex);
                }
            }
        }
    }
}

// ==================== 指纹识别任务 ====================

/*
 * 指纹识别任务（优先级3）
 * - 扫描指纹传感器
 * - 匹配指纹库中的指纹
 * - 匹配成功则解锁
 */
void vFingerTask(void* pvParameters)
{
    uint16_t finger_id = 0;   // 匹配到的指纹ID
    uint32_t cmd = 1;         // 解锁成功消息（1=指纹解锁）
    
    // 任务主循环
    for(;;)
    {
        // 如果在键盘模式或管理模式，挂起任务
        if(keyboard_mode_active || manage_mode_active) {
            vTaskSuspend(NULL);
            continue;
        }
        
        // 只有在选择了指纹解锁后才扫描指纹
        if(unlock_mode_active && selected_unlock_mode == 1) {
            if (as608_search_finger(&finger_id)) {
                printf("Finger Unlock! ID: %d\r\n", finger_id);
                xQueueSend(msgQueue, &cmd, 10);  // 发送解锁成功消息
                D1 = 0;                          // 点亮LED表示解锁成功
                delay_ms(2000);                  // 保持2秒
                D1 = 1;                          // 熄灭LED
                selected_unlock_mode = 0;         // 清除选择
                unlock_mode_active = 0;           // 退出解锁模式
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));  // 500ms扫描间隔
    }
}

// ==================== 密码验证任务 ====================

/*
 * 密码验证任务（优先级3）
 * - 接收键盘输入
 * - 验证密码是否匹配
 * - 匹配成功则解锁
 */
void vPwdTask(void* pvParameters)
{
    char input_buf[17] = {0};  // 密码输入缓冲区（最多16位）
    uint8_t index = 0;         // 输入索引
    char key_char;             // 当前按键
    uint32_t cmd = 2;          // 解锁成功消息（2=密码解锁）
    uint8_t matched;           // 是否匹配
    uint8_t count;             // 密码数量
    uint8_t i;                 // 循环索引
    char* pwd;                 // 当前对比的密码

    // 任务主循环
    for(;;)
    {
        // 只有在选择了密码解锁且键盘模式激活时才处理
        if(!keyboard_mode_active || !unlock_mode_active || selected_unlock_mode != 2) {
            vTaskSuspend(NULL);        // 挂起任务等待唤醒
            memset(input_buf, 0, sizeof(input_buf));  // 恢复时清空缓冲区
            index = 0;
            continue;
        }
        
        // 等待按键输入
        if(xQueueReceive(keyQueue, &key_char, portMAX_DELAY) == pdTRUE) {
            if (key_char == '*') {
                // 取消密码输入
                index = 0;
                memset(input_buf, 0, sizeof(input_buf));
                selected_unlock_mode = 0;
                keyboard_mode_disable();
                // 返回解锁模式菜单
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    oled_show_string(0, 0, "Unlock Mode:");
                    oled_show_string(0, 2, "1.Finger");
                    oled_show_string(0, 4, "2.Password");
                    oled_show_string(0, 6, "3.RFID *-Back");
                    xSemaphoreGive(oledMutex);
                }
            } else if (key_char == '#') {
                // 验证密码
                input_buf[index] = '\0';
                matched = 0;
                count = flash_get_password_count();
                for(i = 0; i < count; i++) {
                    pwd = flash_get_password(i);
                    if(strstr(input_buf, pwd) != NULL) {  // 检查输入是否包含密码
                        matched = 1;
                        break;
                    }
                }
                
                if(matched) {
                    printf("Password Unlock!\r\n");
                    selected_unlock_mode = 0;
                    unlock_mode_active = 0;
                    keyboard_mode_disable();
                    xQueueSend(msgQueue, &cmd, 10);  // 发送解锁成功消息
                    D1 = 0;                          // 点亮LED
                    vTaskDelay(pdMS_TO_TICKS(2000)); // 保持2秒
                    D1 = 1;                          // 熄灭LED
                } else {
                    printf("Password Error!\r\n");
                }
                
                memset(input_buf, 0, sizeof(input_buf));
                index = 0;
            } else {
                // 输入数字
                if(index < 16) {
                    input_buf[index++] = key_char;
                }
            }
        }
    }
}

// ==================== 按键扫描任务 ====================

/*
 * 按键扫描任务（优先级4）
 * - 扫描矩阵键盘
 * - 根据当前模式分发按键到不同队列
 */
void vKeyTask(void* pvParameters)
{
    char key_val;  // 扫描到的按键值
    
    // 任务主循环
    for(;;)
    {
        key_val = keypad_scan();  // 扫描按键
        if(key_val != 0) {
            // 根据当前模式分发按键
            if(manage_mode_active) {
                if(manage_submenu_active) {
                    // 管理子菜单：发送到manageQueue
                    xQueueSend(manageQueue, &key_val, 10);
                } else {
                    // 管理主菜单：发送到keyMenuQueue
                    xQueueSend(keyMenuQueue, &key_val, 10);
                }
            } else if(admin_pwd_mode_active) {
                // 管理员密码输入：发送到keyMenuQueue
                xQueueSend(keyMenuQueue, &key_val, 10);
            } else if(keyboard_mode_active) {
                // 键盘输入模式：发送到keyQueue（密码输入）
                xQueueSend(keyQueue, &key_val, 10);
            } else {
                // 默认：同时发送到两个队列
                xQueueSend(keyQueue, &key_val, 10);
                xQueueSend(keyMenuQueue, &key_val, 10);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms扫描间隔
    }
}

// ==================== OLED显示任务 ====================

/*
 * OLED显示任务（优先级2）
 * - 接收解锁成功消息
 * - 显示解锁成功提示
 * - 恢复显示系统就绪界面
 */
void vOledTask(void* pvParameters)
{
    uint32_t msg_cmd;  // 消息命令
    
    // 任务主循环
    for(;;)
    {
        // 等待解锁成功消息
        if(xQueueReceive(msgQueue, &msg_cmd, portMAX_DELAY) == pdTRUE) {
            // 如果在键盘模式或管理模式，忽略消息
            if(keyboard_mode_active || manage_mode_active) {
                continue;
            }
            
            // 显示解锁成功信息
            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                oled_clear();
                if(msg_cmd == 1)      oled_show_string(0, 0, "Finger OK!");
                else if(msg_cmd == 2) oled_show_string(0, 0, "Password OK!");
                else if(msg_cmd == 3) oled_show_string(0, 0, "RFID OK!");
                
                xSemaphoreGive(oledMutex);
                
                vTaskDelay(pdMS_TO_TICKS(2000));  // 显示2秒
                
                // 恢复系统就绪界面
                if(!keyboard_mode_active && !manage_mode_active) {
                    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                        oled_clear();
                        oled_show_string(0, 0, "System Ready!");
                        oled_show_string(0, 2, "1-Unlock");
                        oled_show_string(0, 4, "2-Manage");
                        oled_show_string(0, 6, "Press 1 or 2");
                        xSemaphoreGive(oledMutex);
                    }
                }
            }
        }
    }
}

// ==================== RFID识别任务 ====================

/*
 * RFID识别任务（优先级3）
 * - 扫描RFID卡片
 * - 匹配卡片库中的卡片
 * - 匹配成功则解锁
 */
void vRfidTask(void* pvParameters)
{
    uint32_t card_id = 0;   // 读取到的卡片ID
    uint32_t cmd = 3;       // 解锁成功消息（3=RFID解锁）
    uint8_t matched;        // 是否匹配
    uint8_t count;          // 卡片数量
    uint8_t i;              // 循环索引
    
    // 任务主循环
    for(;;)
    {
        // 如果在管理模式，挂起任务
        if(manage_mode_active) {
            vTaskSuspend(NULL);
            continue;
        }
        
        // 获取RFID模块互斥锁
        if(xSemaphoreTake(rc522Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(rc522_read_card(&card_id)) {
                last_card_uid = card_id;
                
                // 只有在选择了RFID解锁后才进行验证
                if(unlock_mode_active && selected_unlock_mode == 3) {
                    matched = 0;
                    count = flash_get_card_count();
                    for(i = 0; i < count; i++) {
                        if(card_id == flash_get_card(i)) {
                            matched = 1;
                            break;
                        }
                    }
                    
                    if(matched) {
                        printf("RFID Unlock! Card: %08X\r\n", card_id);
                        xQueueSend(msgQueue, &cmd, 10);  // 发送解锁成功消息
                        D1 = 0;                          // 点亮LED
                        vTaskDelay(pdMS_TO_TICKS(2000)); // 保持2秒
                        D1 = 1;                          // 熄灭LED
                        selected_unlock_mode = 0;         // 清除选择
                        unlock_mode_active = 0;           // 退出解锁模式
                    } else {
                        printf("RFID Denied! Unknown card.\r\n");
                    }
                }
            }
            xSemaphoreGive(rc522Mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(300));  // 300ms扫描间隔
    }
}

// ==================== 串口命令任务 ====================

/*
 * 串口命令任务（优先级3）
 * - 解析串口命令
 * - 处理WiFi相关命令
 */
void vUartCmdTask(void* pvParameters)
{
    // 任务主循环
    for(;;)
    {
        parse_cmd();    // 解析普通串口命令
        parse_bl_cmd(); // 解析蓝牙/其他命令
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms处理间隔
    }
}

// ==================== 解锁执行任务（备用） ====================

/*
 * 解锁执行任务（优先级3）
 * - 备用的集中式解锁处理
 * - 支持超时机制
 */
void vUnlockExecTask(void* pvParameters)
{
    UnlockRequest req;     // 解锁请求
    UnlockResult res;      // 解锁结果
    uint16_t finger_id = 0;
    char input_buf[17] = {0};
    uint8_t index = 0;
    char key_char;
    uint32_t card_id = 0;
    uint32_t start_time;
    uint8_t matched;
    uint8_t count;
    uint8_t i;
    char* pwd;
    
    // 任务主循环
    for(;;)
    {
        // 等待解锁请求
        if(xQueueReceive(unlockReqQueue, &req, portMAX_DELAY) == pdTRUE) {
            res.mode = req.mode;
            res.success = 0;
            
            // 根据解锁方式执行不同的处理
            switch(req.mode) {
                case MODE_FINGER:
                    // 指纹解锁（带超时）
                    start_time = xTaskGetTickCount();
                    while((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(req.timeout_ms)) {
                        if(as608_search_finger(&finger_id)) {
                            res.success = 1;
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(300));
                    }
                    break;
                    
                case MODE_PASSWORD:
                    // 密码解锁（带超时）
                    memset(input_buf, 0, sizeof(input_buf));
                    index = 0;
                    start_time = xTaskGetTickCount();
                    
                    // 清空按键队列
                    while(xQueueReceive(keyQueue, &key_char, 0) == pdTRUE);
                    
                    while((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(req.timeout_ms)) {
                        if(xQueueReceive(keyQueue, &key_char, pdMS_TO_TICKS(100)) == pdTRUE) {
                            if(key_char == '#') {
                                input_buf[index] = '\0';
                                matched = 0;
                                count = flash_get_password_count();
                                for(i = 0; i < count; i++) {
                                    pwd = flash_get_password(i);
                                    if(strstr(input_buf, pwd) != NULL) {
                                        matched = 1;
                                        break;
                                    }
                                }
                                res.success = matched;
                                break;
                            } else if(key_char == '*') {
                                break;
                            } else if(index < 16) {
                                input_buf[index++] = key_char;
                            }
                        }
                    }
                    break;
                    
                case MODE_RFID:
                    // RFID解锁（带超时）
                    start_time = xTaskGetTickCount();
                    while((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(req.timeout_ms)) {
                        if(xSemaphoreTake(rc522Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            if(rc522_read_card(&card_id)) {
                                last_card_uid = card_id;
                                matched = 0;
                                count = flash_get_card_count();
                                for(i = 0; i < count; i++) {
                                    if(card_id == flash_get_card(i)) {
                                        matched = 1;
                                        break;
                                    }
                                }
                                res.success = matched;
                                xSemaphoreGive(rc522Mutex);
                                break;
                            }
                            xSemaphoreGive(rc522Mutex);
                        }
                        vTaskDelay(pdMS_TO_TICKS(200));
                    }
                    break;
            }
            
            // 发送解锁结果
            xQueueSend(unlockResultQueue, &res, portMAX_DELAY);
        }
    }
}

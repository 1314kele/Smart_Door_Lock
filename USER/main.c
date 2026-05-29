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

TaskHandle_t app_task_init_handle = NULL;
TaskHandle_t finger_task_handle = NULL;
TaskHandle_t pwd_task_handle = NULL;
TaskHandle_t key_task_handle = NULL;
TaskHandle_t keyMenu_task_handle = NULL;
TaskHandle_t oled_task_handle = NULL;
TaskHandle_t rfid_task_handle = NULL;
TaskHandle_t uart_cmd_task_handle = NULL;
TaskHandle_t unlockExec_task_handle = NULL;
TaskHandle_t manage_task_handle = NULL;

QueueHandle_t msgQueue = NULL;
QueueHandle_t keyQueue = NULL;
QueueHandle_t keyMenuQueue = NULL;
QueueHandle_t unlockReqQueue = NULL;
QueueHandle_t unlockResultQueue = NULL;
QueueHandle_t manageQueue = NULL;
SemaphoreHandle_t oledMutex = NULL;
SemaphoreHandle_t as608Mutex = NULL;
SemaphoreHandle_t rc522Mutex = NULL;

volatile uint8_t keyboard_mode_active = 0;
volatile uint8_t current_mode = 0;
volatile uint8_t unlock_executing = 0;
volatile uint8_t manage_mode_active = 0;

typedef enum {
    MODE_FINGER = 0,
    MODE_PASSWORD = 1,
    MODE_RFID = 2,
    MODE_COUNT = 3
} UnlockMode;

typedef enum {
    MENU_MAIN = 0,
    MENU_UNLOCK = 1,
    MENU_MANAGE = 2,
    MENU_MANAGE_FINGER = 3,
    MENU_MANAGE_PWD = 4,
    MENU_MANAGE_RFID = 5
} MenuState;

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

const char* menu_main_items[] = {
    "1.Unlock",
    "2.Manage"
};

const char* menu_manage_items[] = {
    "1.Finger",
    "2.Password",
    "3.RFID"
};

extern uint32_t saved_card_uid;
uint32_t last_card_uid = 0;

typedef struct {
    uint8_t mode;
    uint32_t timeout_ms;
} UnlockRequest;

typedef struct {
    uint8_t mode;
    uint8_t success;
} UnlockResult;

void app_task_init(void* pvParameters);
void vFingerTask(void* pvParameters);
void vPwdTask(void* pvParameters);
void vKeyTask(void* pvParameters);
void vKeyMenuTask(void* pvParameters);
void vOledTask(void* pvParameters);
void vRfidTask(void* pvParameters);
void vUartCmdTask(void* pvParameters);
void vUnlockExecTask(void* pvParameters);
void vManageTask(void* pvParameters);

void key_feedback(void) {
    D1 = 0;
    delay_ms(50);
    D1 = 1;
}

void keyboard_mode_enable(void) {
    keyboard_mode_active = 1;
    if(finger_task_handle) vTaskSuspend(finger_task_handle);
    if(pwd_task_handle) vTaskSuspend(pwd_task_handle);
    printf("[Keyboard] Mode activated!\r\n");
}

void keyboard_mode_disable(void) {
    keyboard_mode_active = 0;
    current_mode = 0;
    if(finger_task_handle) vTaskResume(finger_task_handle);
    if(pwd_task_handle) vTaskResume(pwd_task_handle);
    printf("[Keyboard] Mode deactivated!\r\n");
}

void manage_mode_enable(void) {
    manage_mode_active = 1;
    if(finger_task_handle) vTaskSuspend(finger_task_handle);
    if(pwd_task_handle) vTaskSuspend(pwd_task_handle);
    if(rfid_task_handle) vTaskSuspend(rfid_task_handle);
    printf("[Manage] Mode activated!\r\n");
}

void manage_mode_disable(void) {
    manage_mode_active = 0;
    if(finger_task_handle) vTaskResume(finger_task_handle);
    if(pwd_task_handle) vTaskResume(pwd_task_handle);
    if(rfid_task_handle) vTaskResume(rfid_task_handle);
    printf("[Manage] Mode deactivated!\r\n");
}

int main()
{
    int i;
    uint32_t *raw_addr;
    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    delay_init();
    led_init();
    keypad_init();
    oled_init();
    rc522_init();
    as608_init();
    flash_init();
    
    uart1_init(115200);
    uart2_init(57600);
    uart3_init(115200);
    
    printf("Smart Door Lock System Start!\n");
    printf("RC522 Version: 0x%02X\r\n", rc522_ver);
    
    // 调试：打印Flash中的数据
    printf("=== Flash Init Debug ===\r\n");
    printf("Card count: %d\r\n", flash_get_card_count());
    for(i = 0; i < flash_get_card_count(); i++) {
        printf("  Card[%d]: %08X\r\n", i, flash_get_card(i));
    }
    printf("Password count: %d\r\n", flash_get_password_count());
    for(i = 0; i < flash_get_password_count(); i++) {
        printf("  Pwd[%d]: %s\r\n", i, flash_get_password(i));
    }
    
    // 直接读取Flash原始数据看看
    raw_addr = (uint32_t *)0x080E0000;
    printf("Raw Flash content:\r\n");
    for(i = 0; i < 10; i++) {
        printf("  [0x%08X]: 0x%08X\r\n", (uint32_t)(raw_addr + i), raw_addr[i]);
    }
    printf("========================\r\n");

    xTaskCreate((TaskFunction_t )app_task_init,   
                (const char*    )"app_task_init", 
                (uint16_t       )512,             
                (void*          )NULL,            
                (UBaseType_t    )5,               
                (TaskHandle_t*  )&app_task_init_handle); 

    vTaskStartScheduler();

    while(1);
}

void app_task_init(void* pvParameters)
{
    printf("app_task_init running!\r\n");

    msgQueue = xQueueCreate(10, sizeof(uint32_t)); 
    keyQueue = xQueueCreate(16, sizeof(char)); 
    keyMenuQueue = xQueueCreate(16, sizeof(char));
    unlockReqQueue = xQueueCreate(5, sizeof(UnlockRequest));
    unlockResultQueue = xQueueCreate(5, sizeof(UnlockResult));
    manageQueue = xQueueCreate(16, sizeof(char));
    oledMutex = xSemaphoreCreateMutex();
    as608Mutex = xSemaphoreCreateMutex();
    rc522Mutex = xSemaphoreCreateMutex();

    taskENTER_CRITICAL();

    xTaskCreate(vFingerTask, "FingerTask", 512, NULL, 3, &finger_task_handle);      
    xTaskCreate(vPwdTask, "PwdTask", 512, NULL, 3, &pwd_task_handle);      
    xTaskCreate(vKeyTask, "KeyTask", 512, NULL, 4, &key_task_handle);        
    xTaskCreate(vKeyMenuTask, "KeyMenuTask", 512, NULL, 5, &keyMenu_task_handle);        
    xTaskCreate(vOledTask, "OledTask", 512, NULL, 2, &oled_task_handle);
    xTaskCreate(vRfidTask, "RfidTask", 512, NULL, 3, &rfid_task_handle);
    xTaskCreate(vUartCmdTask, "UartCmdTask", 256, NULL, 3, &uart_cmd_task_handle);
    xTaskCreate(vUnlockExecTask, "UnlockExecTask", 512, NULL, 3, &unlockExec_task_handle);
    xTaskCreate(vManageTask, "ManageTask", 512, NULL, 5, &manage_task_handle);

    taskEXIT_CRITICAL();

    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
        oled_clear();
        oled_show_string(0, 0, "System Ready!");
        oled_show_string(0, 2, "1-Unlock");
        oled_show_string(0, 4, "2-Manage");
        oled_show_string(0, 6, "Press 1 or 2");
        xSemaphoreGive(oledMutex);
    }

    vTaskDelete(NULL);
}

void vKeyMenuTask(void* pvParameters)
{
    char key;
    uint8_t selected_item = 0;
    MenuState menu_state = MENU_MAIN;
    UnlockRequest unlock_req;
    UnlockResult unlock_result;
    uint8_t max_items = 0;
    char card_str[10];
    uint8_t unlock_in_progress = 0;
    
    for(;;)
    {
        if(xQueueReceive(keyMenuQueue, &key, portMAX_DELAY) == pdTRUE) {
            // 如果解锁任务正在进行中，忽略按键
            if(unlock_in_progress) {
                continue;
            }
            
            key_feedback();
            
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
            
            switch(menu_state) {
                case MENU_MAIN:
                    if(key == '1') {
                        selected_item = 0;
                        menu_state = MENU_UNLOCK;
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Unlock Mode:");
                            oled_show_string(0, 2, "1.Finger");
                            oled_show_string(0, 4, "2.Password");
                            oled_show_string(0, 6, "3.RFID *-Back");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '2') {
                        selected_item = 0;
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
                    }
                    break;
                    
                case MENU_UNLOCK:
                    if(key == '1') {
                        unlock_in_progress = 1; // 标记解锁任务开始
                        keyboard_mode_enable(); // 只有真正开始解锁任务时才启用键盘模式
                        unlock_req.mode = MODE_FINGER;
                        unlock_req.timeout_ms = 10000;
                        xQueueSend(unlockReqQueue, &unlock_req, portMAX_DELAY);
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Finger Unlock");
                            oled_show_string(0, 2, "Place finger...");
                            oled_show_string(0, 6, "* to cancel");
                            xSemaphoreGive(oledMutex);
                        }
                        if(xQueueReceive(unlockResultQueue, &unlock_result, pdMS_TO_TICKS(12000)) == pdTRUE) {
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                if(unlock_result.success) {
                                    oled_show_string(0, 2, "SUCCESS!");
                                    D1 = 0;
                                    vTaskDelay(pdMS_TO_TICKS(2000));
                                    D1 = 1;
                                } else {
                                    oled_show_string(0, 2, "FAILED!");
                                    vTaskDelay(pdMS_TO_TICKS(1000));
                                }
                                oled_show_string(0, 0, "System Ready!");
                                oled_show_string(0, 2, "1-Unlock");
                                oled_show_string(0, 4, "2-Manage");
                                oled_show_string(0, 6, "Press 1 or 2");
                                xSemaphoreGive(oledMutex);
                            }
                        }
                        menu_state = MENU_MAIN;
                        keyboard_mode_disable();
                        unlock_in_progress = 0; // 标记解锁任务结束
                    } else if(key == '2') {
                        unlock_in_progress = 1; // 标记解锁任务开始
                        keyboard_mode_enable(); // 只有真正开始解锁任务时才启用键盘模式
                        unlock_req.mode = MODE_PASSWORD;
                        unlock_req.timeout_ms = 10000;
                        xQueueSend(unlockReqQueue, &unlock_req, portMAX_DELAY);
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Password Unlock");
                            oled_show_string(0, 2, "Input password:");
                            oled_show_string(0, 6, "#-OK *-Cancel");
                            xSemaphoreGive(oledMutex);
                        }
                        if(xQueueReceive(unlockResultQueue, &unlock_result, pdMS_TO_TICKS(12000)) == pdTRUE) {
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                if(unlock_result.success) {
                                    oled_show_string(0, 2, "SUCCESS!");
                                    D1 = 0;
                                    vTaskDelay(pdMS_TO_TICKS(2000));
                                    D1 = 1;
                                } else {
                                    oled_show_string(0, 2, "FAILED!");
                                    vTaskDelay(pdMS_TO_TICKS(1000));
                                }
                                oled_show_string(0, 0, "System Ready!");
                                oled_show_string(0, 2, "1-Unlock");
                                oled_show_string(0, 4, "2-Manage");
                                oled_show_string(0, 6, "Press 1 or 2");
                                xSemaphoreGive(oledMutex);
                            }
                        }
                        menu_state = MENU_MAIN;
                        keyboard_mode_disable();
                        unlock_in_progress = 0; // 标记解锁任务结束
                    } else if(key == '3') {
                        unlock_in_progress = 1; // 标记解锁任务开始
                        keyboard_mode_enable(); // 只有真正开始解锁任务时才启用键盘模式
                        unlock_req.mode = MODE_RFID;
                        unlock_req.timeout_ms = 10000;
                        xQueueSend(unlockReqQueue, &unlock_req, portMAX_DELAY);
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "RFID Unlock");
                            oled_show_string(0, 2, "Scan card...");
                            oled_show_string(0, 6, "* to cancel");
                            xSemaphoreGive(oledMutex);
                        }
                        if(xQueueReceive(unlockResultQueue, &unlock_result, pdMS_TO_TICKS(12000)) == pdTRUE) {
                            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                oled_clear();
                                if(unlock_result.success) {
                                    oled_show_string(0, 2, "SUCCESS!");
                                    D1 = 0;
                                    vTaskDelay(pdMS_TO_TICKS(2000));
                                    D1 = 1;
                                } else {
                                    oled_show_string(0, 2, "FAILED!");
                                    vTaskDelay(pdMS_TO_TICKS(1000));
                                }
                                oled_show_string(0, 0, "System Ready!");
                                oled_show_string(0, 2, "1-Unlock");
                                oled_show_string(0, 4, "2-Manage");
                                oled_show_string(0, 6, "Press 1 or 2");
                                xSemaphoreGive(oledMutex);
                            }
                        }
                        menu_state = MENU_MAIN;
                        keyboard_mode_disable();
                        unlock_in_progress = 0; // 标记解锁任务结束
                    }
                    break;
                    
                case MENU_MANAGE:
                    if(key == '1') {
                        menu_state = MENU_MANAGE_FINGER;
                        selected_item = 0;
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Finger Manage:");
                            oled_show_string(0, 2, "Sel: 0");
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "*-Back");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '2') {
                        menu_state = MENU_MANAGE_PWD;
                        selected_item = 0;
                        max_items = flash_get_password_count();
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Pwd Manage:");
                            if(max_items > 0) {
                                oled_show_string(0, 2, flash_get_password(0));
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "*-Back");
                            xSemaphoreGive(oledMutex);
                        }
                    } else if(key == '3') {
                        menu_state = MENU_MANAGE_RFID;
                        selected_item = 0;
                        max_items = flash_get_card_count();
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
                            oled_show_string(0, 6, "*-Back");
                            xSemaphoreGive(oledMutex);
                        }
                    }
                    break;
                    
                case MENU_MANAGE_FINGER:
                case MENU_MANAGE_PWD:
                case MENU_MANAGE_RFID:
                    xQueueSend(manageQueue, &key, portMAX_DELAY);
                    break;
            }
        }
    }
}

void vManageTask(void* pvParameters)
{
    char key;
    uint8_t selected_index = 0;
    uint8_t max_items = 0;
    MenuState current_manage_menu = MENU_MANAGE_FINGER;
    char pwd_buffer[17] = {0};
    uint8_t pwd_index = 0;
    uint8_t enrolling = 0;
    uint32_t card_id = 0;
    char hex_str[10];
    char confirm_key;
    char finger_info[20];
    char card_str[10];
    uint8_t idx;
    
    for(;;)
    {
        if(xQueueReceive(manageQueue, &key, portMAX_DELAY) == pdTRUE) {
            key_feedback();
            
            if(key == '*') {
                enrolling = 0;
                pwd_index = 0;
                memset(pwd_buffer, 0, sizeof(pwd_buffer));
                selected_index = 0;
                manage_mode_disable();
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
            
            if(enrolling) {
                if(current_manage_menu == MENU_MANAGE_PWD) {
                    if(key == '#') {
                        pwd_buffer[pwd_index] = '\0';
                        if(pwd_index >= 4) {
                            if(flash_add_password(pwd_buffer)) {
                                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                                    oled_clear();
                                    oled_show_string(0, 2, "Saved!");
                                    xSemaphoreGive(oledMutex);
                                }
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
                    if(as608_enroll_finger(selected_index)) {
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 2, "Enroll OK!");
                            xSemaphoreGive(oledMutex);
                        }
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
                switch(current_manage_menu) {
                    case MENU_MANAGE_FINGER:
                        max_items = 10;
                        break;
                    case MENU_MANAGE_PWD:
                        max_items = flash_get_password_count();
                        break;
                    case MENU_MANAGE_RFID:
                        max_items = flash_get_card_count();
                        break;
                }
                
                if(key == '1') {
                    if(max_items > 0) {
                        if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                            oled_clear();
                            oled_show_string(0, 0, "Delete? 1=Yes");
                            xSemaphoreGive(oledMutex);
                        }
                        if(xQueueReceive(manageQueue, &confirm_key, pdMS_TO_TICKS(5000)) == pdTRUE) {
                            if(confirm_key == '1') {
                                switch(current_manage_menu) {
                                    case MENU_MANAGE_FINGER:
                                        as608_delete_finger(selected_index);
                                        break;
                                    case MENU_MANAGE_PWD:
                                        flash_delete_password(selected_index);
                                        break;
                                    case MENU_MANAGE_RFID:
                                        flash_delete_card(selected_index);
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
                            vTaskDelay(pdMS_TO_TICKS(500));
                        }
                    }
                } else if(key == '2') {
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
                } else if(key >= '1' && key <= '9') {
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
            
            if(!enrolling) {
                if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                    oled_clear();
                    switch(current_manage_menu) {
                        case MENU_MANAGE_FINGER:
                            oled_show_string(0, 0, "Finger Manage:");
                            sprintf(finger_info, "Sel: %d", selected_index);
                            oled_show_string(0, 2, finger_info);
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "*-Back");
                            break;
                            
                        case MENU_MANAGE_PWD:
                            oled_show_string(0, 0, "Password Manage:");
                            if(max_items > 0) {
                                oled_show_string(0, 2, flash_get_password(selected_index));
                            } else {
                                oled_show_string(0, 2, "Empty");
                            }
                            oled_show_string(0, 4, "1-Del 2-Add");
                            oled_show_string(0, 6, "*-Back");
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
                            oled_show_string(0, 6, "*-Back");
                            break;
                    }
                    xSemaphoreGive(oledMutex);
                }
            }
        }
    }
}

void vFingerTask(void* pvParameters)
{
    uint16_t finger_id = 0;
    uint32_t cmd = 1;
    for(;;)
    {
        if(keyboard_mode_active || manage_mode_active) {
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

void vPwdTask(void* pvParameters)
{
    char input_buf[17] = {0};
    uint8_t index = 0;
    char key_char;
    uint32_t cmd = 2;
    uint8_t matched;
    uint8_t count;
    uint8_t i;
    char* pwd;

    for(;;)
    {
        if(keyboard_mode_active || manage_mode_active) {
            vTaskSuspend(NULL);
            continue;
        }
        
        if(xQueueReceive(keyQueue, &key_char, portMAX_DELAY) == pdTRUE) {
            if (key_char == '#') {
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
                
                if(matched) {
                    printf("Password Unlock!\r\n");
                    xQueueSend(msgQueue, &cmd, 10);
                    D1 = 0; 
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    D1 = 1;
                } else {
                    printf("Password Error!\r\n");
                }
                
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

void vKeyTask(void* pvParameters)
{
    char key_val;
    for(;;)
    {
        key_val = keypad_scan();
        if(key_val != 0) {
            if(manage_mode_active) {
                xQueueSend(keyMenuQueue, &key_val, 10);
                xQueueSend(manageQueue, &key_val, 10);
            } else if(keyboard_mode_active) {
                // 在键盘模式下，只发送到keyQueue用于解锁任务
                xQueueSend(keyQueue, &key_val, 10);
            } else {
                xQueueSend(keyQueue, &key_val, 10);
                xQueueSend(keyMenuQueue, &key_val, 10);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vOledTask(void* pvParameters)
{
    uint32_t msg_cmd;
    for(;;)
    {
        if(xQueueReceive(msgQueue, &msg_cmd, portMAX_DELAY) == pdTRUE) {
            if(keyboard_mode_active || manage_mode_active) {
                continue;
            }
            
            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                oled_clear();
                if(msg_cmd == 1)      oled_show_string(0, 0, "Finger OK!");
                else if(msg_cmd == 2) oled_show_string(0, 0, "Password OK!");
                else if(msg_cmd == 3) oled_show_string(0, 0, "RFID OK!");
                
                xSemaphoreGive(oledMutex);
                
                vTaskDelay(pdMS_TO_TICKS(2000));
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

void vRfidTask(void* pvParameters)
{
    uint32_t card_id = 0;
    uint32_t cmd = 3;
    uint8_t matched;
    uint8_t count;
    uint8_t i;
    
    for(;;)
    {
        if(manage_mode_active) {
            vTaskSuspend(NULL);
            continue;
        }
        
        if(xSemaphoreTake(rc522Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(rc522_read_card(&card_id)) {
                last_card_uid = card_id;
                
                if(keyboard_mode_active) {
                    printf("[RFID] Card detected (keyboard mode): %08X\r\n", card_id);
                } else {
                    printf("RFID Card: %08X\r\n", card_id);
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
    uint8_t matched;
    uint8_t count;
    uint8_t i;
    char* pwd;
    
    for(;;)
    {
        if(xQueueReceive(unlockReqQueue, &req, portMAX_DELAY) == pdTRUE) {
            res.mode = req.mode;
            res.success = 0;
            
            switch(req.mode) {
                case MODE_FINGER:
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
                    memset(input_buf, 0, sizeof(input_buf));
                    index = 0;
                    start_time = xTaskGetTickCount();
                    
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
                    start_time = xTaskGetTickCount();
                    while((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(req.timeout_ms)) {
                        if(xSemaphoreTake(rc522Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            if(rc522_read_card(&card_id)) {
                                last_card_uid = card_id;
                                printf("[RFID Unlock] Scanned card: %08X\r\n", card_id);
                                matched = 0;
                                count = flash_get_card_count();
                                printf("[RFID Unlock] Saved cards count: %d\r\n", count);
                                for(i = 0; i < count; i++) {
                                    uint32_t saved_card = flash_get_card(i);
                                    printf("[RFID Unlock]   Comparing: %08X vs %08X\r\n", card_id, saved_card);
                                    if(card_id == saved_card) {
                                        matched = 1;
                                        break;
                                    }
                                }
                                res.success = matched;
                                printf("[RFID Unlock] Result: %s\r\n", matched ? "SUCCESS" : "FAILED");
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
        }
    }
}
#include "includes.h"
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// Ӳ������ͷ�ļ�
#include "oled.h"
#include "rc522.h"
#include "as608.h"
#include "keypad.h"
#include "uart.h"
#include "flash.h"

// ������
TaskHandle_t app_task_init_handle = NULL;
TaskHandle_t finger_task_handle = NULL;
TaskHandle_t pwd_task_handle = NULL;
TaskHandle_t key_task_handle = NULL;
TaskHandle_t oled_task_handle = NULL;
TaskHandle_t rfid_task_handle = NULL;
TaskHandle_t uart_cmd_task_handle = NULL;

// �����뻥����
QueueHandle_t msgQueue = NULL;      // ��Ϣ���У����ڴ��ݿ���ָ���ʾָ���
QueueHandle_t keyQueue = NULL;      // �������У������ռ����ļ�ֵ
SemaphoreHandle_t oledMutex = NULL; // OLED��ʾ�Ļ���������ֹ������ͬʱд��Ļ
SemaphoreHandle_t as608Mutex = NULL;// AS608ģ�黥��������ֹ��USART3����

uint32_t last_card_uid = 0;
uint32_t saved_card_uid = 0;

// ����������
void app_task_init(void* pvParameters);
void vFingerTask(void* pvParameters);
void vPwdTask(void* pvParameters);
void vKeyTask(void* pvParameters);
void vOledTask(void* pvParameters);
void vRfidTask(void* pvParameters);
void vUartCmdTask(void* pvParameters);

// �����ĵ�Ԥ��2��Ĭ�Ϲ̶�����
const char* const correct_pwd_1 = "123456";
const char* const correct_pwd_2 = "654321";

int main()
{
    // �ж����ȼ����� 4:0
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    delay_init();

    // Ӳ����ʼ��
    led_init();
    //beep_init();
    keypad_init();   // 4x4������̳�ʼ��
    oled_init();     // I2C OLED��ʼ��
    rc522_init();    // SPI RC522��ʼ��
    as608_init();    // ����ָ�Ƴ�ʼ��
    flash_init();    // Flash�洢��ʼ��
    saved_card_uid = flash_read_card_uid();
    
    // ��ʼ�����ڣ�����/���ԣ�
    uart1_init(115200); // 串口控制台
    uart2_init(57600);   // 指纹模块 (AS608波特率-商家资料)
    uart3_init(115200);  // WiFi模块
    
    printf("Smart Door Lock System Start!\n");
    printf("RC522 Version: 0x%02X\r\n", rc522_ver);

    /* ����app_task_init���� */
    xTaskCreate((TaskFunction_t )app_task_init,   
                (const char*    )"app_task_init", 
                (uint16_t       )512,             
                (void*          )NULL,            
                (UBaseType_t    )5,               
                (TaskHandle_t*  )&app_task_init_handle); 

    /* ����������� */
    vTaskStartScheduler();

    while(1);
}

void app_task_init(void* pvParameters)
{
    printf("app_task_init running!\r\n");

    // ���������뻥����
    msgQueue = xQueueCreate(10, sizeof(uint32_t)); 
    keyQueue = xQueueCreate(16, sizeof(char)); 
    oledMutex = xSemaphoreCreateMutex();
    as608Mutex = xSemaphoreCreateMutex();

    // �����ٽ���
    taskENTER_CRITICAL();

    /* ����ָ������ */
    xTaskCreate(vFingerTask, "FingerTask", 512, NULL, 3, &finger_task_handle);      
    /* �������봦������ */
    xTaskCreate(vPwdTask, "PwdTask", 512, NULL, 3, &pwd_task_handle);      
    /* ��������ɨ������ */
    xTaskCreate(vKeyTask, "KeyTask", 512, NULL, 4, &key_task_handle);        
    /* ����OLED��ʾ���� */
    xTaskCreate(vOledTask, "OledTask", 512, NULL, 2, &oled_task_handle);
    /* ����RFIDˢ������ */
    xTaskCreate(vRfidTask, "RfidTask", 512, NULL, 3, &rfid_task_handle);
    /* ���ڴ��ڲ���ָ������ */
    xTaskCreate(vUartCmdTask, "UartCmdTask", 256, NULL, 3, &uart_cmd_task_handle);

    // �˳��ٽ���
    taskEXIT_CRITICAL();

    // ������ʾ
    if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
        oled_clear();
        oled_show_string(0, 0, "Wait Unlock...");
        xSemaphoreGive(oledMutex);
    }

    // ɾ����������
    vTaskDelete(NULL);
}

// 1. ָ�ƴ�������
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

// 2. ���봦������֧����λ���룩
void vPwdTask(void* pvParameters)
{
    char input_buf[17] = {0}; // ���16λ�ӽ�����
    uint8_t index = 0;
    char key_char;
    uint32_t cmd = 2; // 2�������뿪���ɹ�����

    for(;;)
    {
        // ����ͨ�� keyQueue �����İ����ַ�
        if(xQueueReceive(keyQueue, &key_char, portMAX_DELAY) == pdTRUE) {
            
            // ����ȷ�ϼ���������"#")
            if (key_char == '#') {
                input_buf[index] = '\0'; // ��β
                
                // ����������������У�Ѱ�������� 6 λ��ȷ����(��λ����ԭ��)
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
                
                // ������뻺��
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

// 3. ����ɨ������
void vKeyTask(void* pvParameters)
{
    char key_val;
    for(;;)
    {
        // �ռ�����ֵ
        key_val = keypad_scan();
        if(key_val != 0) {
            // ���͵��������
            xQueueSend(keyQueue, &key_val, 10);
        }
        // ��������������
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// 4. OLED��ʾ����
void vOledTask(void* pvParameters)
{
    uint32_t msg_cmd;
    for(;;)
    {
        // �ȴ� msgQueue �п�����Ϣ����ʵʱ��ʾ
        if(xQueueReceive(msgQueue, &msg_cmd, portMAX_DELAY) == pdTRUE) {
            if(xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                oled_clear();
                if(msg_cmd == 1)      oled_show_string(0, 0, "Finger OK!");
                else if(msg_cmd == 2) oled_show_string(0, 0, "Pwd OK!");
                else if(msg_cmd == 3) oled_show_string(0, 0, "RFID OK!");
                else if(msg_cmd == 4) oled_show_string(0, 0, "BLE OK!");
                
                xSemaphoreGive(oledMutex);
                
                // �ع�������
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

// 5. RFID ����
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


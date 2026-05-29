#include "as608.h"
#include "delay.h"
#include "stm32f4xx_usart.h"
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "mqtt.h"

extern SemaphoreHandle_t as608Mutex;
uint8_t fingerprint_enrolling = 0;

static void as608_send_cmd(uint8_t *buf, uint16_t len) {
    uint16_t i;
    for(i = 0; i < len; i++) {
        while(USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, buf[i]);
    }
    while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

static uint8_t as608_receve_reply(uint8_t *reply, uint16_t timeout_ms, uint16_t min_len) {
    uint16_t i = 0;
    volatile uint32_t t;
    
    // 清空接收缓冲区
    for(t = 0; t < 1000; t++) {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET) {
            USART_ReceiveData(USART2);
        } else {
            break;
        }
    }
    
    // 接收数据
    for(t = 0; t < (uint32_t)timeout_ms * 20000; t++) {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET) {
            reply[i++] = USART_ReceiveData(USART2);
            if(i >= min_len) break;
        }
    }
    return i;
}

void as608_init(void) {
    delay_ms(200);
}

uint8_t as608_read_params(void) {
    uint8_t reply[16] = {0};
    uint8_t cmd_read_para[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x26,0x00,0x2A};
    
    xSemaphoreTake(as608Mutex, portMAX_DELAY);
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

uint8_t as608_search_finger(uint16_t *finger_id) {
    uint8_t reply[32];
    uint8_t ret;
    uint8_t cmd_get_img[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x01,0x00,0x05};
    uint8_t cmd_gen_char[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x01,0x00,0x08};
    uint8_t cmd_search[]   = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x08,0x04,0x01,0x00,0x00,0x03,0xE7,0x00,0xF8};
    BaseType_t mutex_taken = pdFALSE;

    // 先获取锁，再检查状态，防止竞争条件
    if(xSemaphoreTake(as608Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    mutex_taken = pdTRUE;
    
    // 获取锁后再次检查是否在录入
    if(fingerprint_enrolling) {
        goto exit;
    }

    as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
    ret = as608_receve_reply(reply, 500, 12);
    if(ret < 10 || reply[9] != 0x00) {
        goto exit;
    }

    as608_send_cmd(cmd_gen_char, sizeof(cmd_gen_char));
    ret = as608_receve_reply(reply, 500, 12);
    if(ret < 10 || reply[9] != 0x00) {
        goto exit;
    }

    as608_send_cmd(cmd_search, sizeof(cmd_search));
    ret = as608_receve_reply(reply, 500, 16);
    if(ret < 10 || reply[9] != 0x00) {
        goto exit;
    }

    *finger_id = (reply[10] << 8) | reply[11];
    printf("[Finger] Match! ID=%d score=%d\r\n", *finger_id, (reply[14]<<8)|reply[15]);
    
exit:
    if(mutex_taken == pdTRUE) {
        xSemaphoreGive(as608Mutex);
    }
    return (ret >= 12 && reply[9] == 0x00) ? 1 : 0;
}

uint8_t as608_enroll_finger(uint16_t finger_id) {
    uint8_t reply[32] = {0};
    uint8_t ret;
    uint8_t cmd_get_img[]    = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x01,0x00,0x05};
    uint8_t cmd_gen_char1[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x01,0x00,0x08};
    uint8_t cmd_gen_char2[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x02,0x00,0x09};
    uint8_t cmd_reg_model[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x05,0x00,0x09};
    uint8_t cmd_store[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x06,0x06,0x01,0x00,0x00,0x00,0x00};
    uint8_t result = 0;
    uint16_t attempt = 0;

    if(xSemaphoreTake(as608Mutex, portMAX_DELAY) != pdTRUE) {
        printf("[Enroll] Failed to get mutex!\r\n");
        return 0;
    }
    
    fingerprint_enrolling = 1;

    cmd_store[11] = (finger_id >> 8) & 0xFF;
    cmd_store[12] = finger_id & 0xFF;
    cmd_store[14] = 0x01 + 0x00 + 0x06 + 0x06 + 0x01 + cmd_store[11] + cmd_store[12];

    printf("[Enroll] ID=%d - Step 1/4: Place finger on sensor...\r\n", finger_id);
    attempt = 0;
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
        ret = as608_receve_reply(reply, 1000, 12);
        if(ret == 0) {
            printf("[Enroll] GetImage1: no response from module!\r\n");
            goto exit;
        }
        if(ret >= 12 && reply[9] == 0x00) break;
        if(ret >= 12 && reply[9] == 0x02) {
            if(attempt++ % 10 == 0) printf("[Enroll] Waiting for finger...\r\n");
            continue;
        }
        printf("[Enroll] GetImage1 fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Step 1 OK - Remove finger...\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("[Enroll] Step 2/4: Generating template...\r\n");
    as608_send_cmd(cmd_gen_char1, sizeof(cmd_gen_char1));
    ret = as608_receve_reply(reply, 1000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] GenChar1 fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Step 2 OK - Place finger again...\r\n");

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

    as608_send_cmd(cmd_gen_char2, sizeof(cmd_gen_char2));
    ret = as608_receve_reply(reply, 1000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] GenChar2 fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Step 4/4: Merging templates...\r\n");

    as608_send_cmd(cmd_reg_model, sizeof(cmd_reg_model));
    ret = as608_receve_reply(reply, 2000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] RegModel fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] Storing fingerprint...\r\n");

    as608_send_cmd(cmd_store, sizeof(cmd_store));
    ret = as608_receve_reply(reply, 1000, 12);
    if(ret < 10 || reply[9] != 0x00) {
        printf("[Enroll] Store fail: code=0x%02X\r\n", reply[9]);
        goto exit;
    }
    printf("[Enroll] SUCCESS! Finger ID=%d enrolled!\r\n", finger_id);
    result = 1;
    
    // 发送MQTT通知
    mqtt_publish_finger_result(FINGER_OP_ADD, finger_id, 1);
    
exit:
    fingerprint_enrolling = 0;
    xSemaphoreGive(as608Mutex);
    return result;
}

void as608_clear_all(void) {
    uint8_t reply[16];
    uint8_t cmd_empty[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x0D,0x00,0x11};

    if(xSemaphoreTake(as608Mutex, portMAX_DELAY) != pdTRUE) {
        printf("[Finger] Clear: failed to get mutex\r\n");
        return;
    }
    
    as608_send_cmd(cmd_empty, sizeof(cmd_empty));
    if(as608_receve_reply(reply, 200, 12) >= 12 && reply[9] == 0x00) {
        printf("[Finger] All fingerprints cleared!\r\n");
        mqtt_publish_message("{\"operation\":\"clear_all\",\"success\":1}");
    } else {
        printf("[Finger] Clear fail: code=0x%02X\r\n", reply[9]);
        mqtt_publish_message("{\"operation\":\"clear_all\",\"success\":0}");
    }
    
    xSemaphoreGive(as608Mutex);
}

uint8_t as608_delete_finger(uint16_t finger_id) {
    uint8_t reply[16];
    uint8_t cmd_delete[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x05,0x0C,0x01,0x00,0x00,0x00,0x00};
    uint8_t result = 0;
    
    if(xSemaphoreTake(as608Mutex, portMAX_DELAY) != pdTRUE) {
        printf("[Delete] Failed to get mutex!\r\n");
        return 0;
    }
    
    cmd_delete[11] = (finger_id >> 8) & 0xFF;
    cmd_delete[12] = finger_id & 0xFF;
    cmd_delete[14] = 0x01 + 0x00 + 0x05 + 0x0C + 0x01 + cmd_delete[11] + cmd_delete[12];
    
    as608_send_cmd(cmd_delete, sizeof(cmd_delete));
    if(as608_receve_reply(reply, 1000, 12) >= 12 && reply[9] == 0x00) {
        printf("[Delete] Finger ID=%d deleted!\r\n", finger_id);
        result = 1;
        mqtt_publish_finger_result(FINGER_OP_DELETE, finger_id, 1);
    } else {
        printf("[Delete] Fail! code=0x%02X\r\n", reply[9]);
        mqtt_publish_finger_result(FINGER_OP_DELETE, finger_id, 0);
    }
    
    xSemaphoreGive(as608Mutex);
    return result;
}

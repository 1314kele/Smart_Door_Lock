#include "as608.h"
#include "delay.h"
#include "stm32f4xx_usart.h"
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"

extern SemaphoreHandle_t as608Mutex;

static void as608_send_cmd(uint8_t *buf, uint16_t len) {
    uint16_t i;
    for(i = 0; i < len; i++) {
        while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
        USART_SendData(USART3, buf[i]);
    }
    while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
}

static uint8_t as608_receve_reply(uint8_t *reply, uint16_t timeout_ms, uint16_t min_len) {
    uint16_t i = 0;
    volatile uint32_t t;
    for(t = 0; t < (uint32_t)timeout_ms * 21000; t++) {
        if(USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET) {
            reply[i++] = USART_ReceiveData(USART3);
            if(i >= min_len) break;
        }
    }
    return i;
}

void as608_init(void) {
    delay_ms(200);
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
    uint8_t reply[16];
    uint8_t ret;
    uint8_t cmd_get_img[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x01,0x00,0x05};
    uint8_t cmd_gen_char[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x01,0x00,0x08};
    uint8_t cmd_search[]   = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x08,0x04,0x01,0x00,0x00,0x03,0xE7,0x00,0xF8};

    if(xSemaphoreTake(as608Mutex, 0) != pdTRUE) return 0;

    as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
    ret = as608_receve_reply(reply, 500, 12);
    if(ret < 12 || reply[9] != 0x00) {
        xSemaphoreGive(as608Mutex);
        return 0;
    }

    as608_send_cmd(cmd_gen_char, sizeof(cmd_gen_char));
    ret = as608_receve_reply(reply, 200, 12);
    if(ret < 12 || reply[9] != 0x00) {
        printf("[Finger] GenChar fail: ret=%d code=0x%02X\r\n", ret, reply[9]);
        xSemaphoreGive(as608Mutex);
        return 0;
    }

    as608_send_cmd(cmd_search, sizeof(cmd_search));
    ret = as608_receve_reply(reply, 200, 16);
    if(ret < 16 || reply[9] != 0x00) {
        xSemaphoreGive(as608Mutex);
        return 0;
    }

    *finger_id = (reply[10] << 8) | reply[11];
    printf("[Finger] Match! ID=%d score=%d\r\n", *finger_id, (reply[14]<<8)|reply[15]);
    xSemaphoreGive(as608Mutex);
    return 1;
}

uint8_t as608_enroll_finger(uint16_t finger_id) {
    uint8_t reply[16] = {0};
    uint8_t ret;
    uint8_t cmd_get_img[]    = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x01,0x00,0x05};
    uint8_t cmd_gen_char1[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x01,0x00,0x08};
    uint8_t cmd_gen_char2[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x04,0x02,0x02,0x00,0x09};
    uint8_t cmd_reg_model[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x05,0x00,0x09};
    uint8_t cmd_store[]  = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x06,0x06,0x01,0x00,0x00,0x00,0x00};

    xSemaphoreTake(as608Mutex, portMAX_DELAY);
    cmd_store[11] = (finger_id >> 8) & 0xFF;
    cmd_store[12] = finger_id & 0xFF;
    cmd_store[14] = 0x01 + 0x00 + 0x06 + 0x06 + 0x01 + cmd_store[11] + cmd_store[12];

    printf("[Enroll] ID=%d - place finger...\r\n", finger_id);
    while(1) {
        as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
        ret = as608_receve_reply(reply, 1000, 12);
        if(ret == 0) {
            printf("[Enroll] GetImage1: no response from module!\r\n");
            xSemaphoreGive(as608Mutex);
            return 0;
        }
        if(ret >= 12 && reply[9] == 0x00) break;
        if(ret >= 12 && reply[9] == 0x02) continue;
        printf("[Enroll] GetImage1 fail: ret=%d code=0x%02X\r\n", ret, reply[9]);
        xSemaphoreGive(as608Mutex);
        return 0;
    }
    printf("[Enroll] Image1 OK, gen char...\r\n");

    while(1) {
        as608_send_cmd(cmd_gen_char1, sizeof(cmd_gen_char1));
        ret = as608_receve_reply(reply, 500, 12);
        if(ret >= 12 && reply[9] == 0x00) break;
        printf("[Enroll] GenChar1 retry... (ret=%d code=0x%02X)\r\n", ret, reply[9]);
    }
    printf("[Enroll] GenChar1 OK, place finger again...\r\n");

    while(1) {
        as608_send_cmd(cmd_get_img, sizeof(cmd_get_img));
        ret = as608_receve_reply(reply, 1000, 12);
        if(ret == 0) {
            printf("[Enroll] GetImage2: no response from module!\r\n");
            xSemaphoreGive(as608Mutex);
            return 0;
        }
        if(ret >= 12 && reply[9] == 0x00) break;
        if(ret >= 12 && reply[9] == 0x02) continue;
        printf("[Enroll] GetImage2 fail: ret=%d code=0x%02X\r\n", ret, reply[9]);
        xSemaphoreGive(as608Mutex);
        return 0;
    }
    printf("[Enroll] Image2 OK, gen char...\r\n");

    while(1) {
        as608_send_cmd(cmd_gen_char2, sizeof(cmd_gen_char2));
        ret = as608_receve_reply(reply, 500, 12);
        if(ret >= 12 && reply[9] == 0x00) break;
        printf("[Enroll] GenChar2 retry... (ret=%d code=0x%02X)\r\n", ret, reply[9]);
    }
    printf("[Enroll] GenChar2 OK, reg model...\r\n");

    as608_send_cmd(cmd_reg_model, sizeof(cmd_reg_model));
    ret = as608_receve_reply(reply, 200, 12);
    if(ret < 12 || reply[9] != 0x00) {
        printf("[Enroll] RegModel fail: ret=%d code=0x%02X\r\n", ret, reply[9]);
        xSemaphoreGive(as608Mutex);
        return 0;
    }
    printf("[Enroll] RegModel OK, store...\r\n");

    as608_send_cmd(cmd_store, sizeof(cmd_store));
    ret = as608_receve_reply(reply, 200, 12);
    if(ret < 12 || reply[9] != 0x00) {
        printf("[Enroll] Store fail: ret=%d code=0x%02X\r\n", ret, reply[9]);
        xSemaphoreGive(as608Mutex);
        return 0;
    }
    printf("[Enroll] Finger ID=%d enrolled successfully!\r\n", finger_id);
    xSemaphoreGive(as608Mutex);
    return 1;
}

void as608_clear_all(void) {
    uint8_t reply[16];
    uint8_t cmd_empty[] = {0xEF,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x03,0x0D,0x00,0x11};

    as608_send_cmd(cmd_empty, sizeof(cmd_empty));
    if(as608_receve_reply(reply, 200, 12) >= 12 && reply[9] == 0x00) {
        printf("[Finger] All fingerprints cleared!\r\n");
    } else {
        printf("[Finger] Clear fail: code=0x%02X\r\n", reply[9]);
    }
}

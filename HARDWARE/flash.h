#ifndef __FLASH_H
#define __FLASH_H
#include "stm32f4xx.h"

#define FLASH_SAVE_ADDR  0x080E0000
#define FLASH_MAGIC      0xCAFE0001

#define MAX_PASSWORDS    5
#define MAX_CARDS        5
#define PWD_LENGTH       8

typedef struct {
    uint32_t magic;
    char passwords[MAX_PASSWORDS][PWD_LENGTH + 1];
    uint32_t cards[MAX_CARDS];
    uint8_t password_count;
    uint8_t card_count;
    char admin_password[PWD_LENGTH + 1];
} flash_data_t;

void flash_init(void);

// 密码管理
uint8_t flash_get_password_count(void);
char* flash_get_password(uint8_t index);
uint8_t flash_add_password(const char* pwd);
uint8_t flash_delete_password(uint8_t index);
void flash_clear_all_passwords(void);

// 卡号管理
uint8_t flash_get_card_count(void);
uint32_t flash_get_card(uint8_t index);
uint8_t flash_add_card(uint32_t uid);
uint8_t flash_delete_card(uint8_t index);
void flash_clear_all_cards(void);

extern uint32_t saved_card_uid;
uint32_t flash_read_card_uid(void);
void flash_save_card_uid(uint32_t uid);

// 管理密码
char* flash_get_admin_password(void);
void flash_set_admin_password(const char* pwd);

#endif

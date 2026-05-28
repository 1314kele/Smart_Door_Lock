#ifndef __FLASH_H
#define __FLASH_H
#include "stm32f4xx.h"

#define FLASH_SAVE_ADDR  0x080E0000
#define FLASH_MAGIC      0xCAFE0001

typedef struct {
    uint32_t magic;
    uint32_t card_uid;
} flash_data_t;

void flash_init(void);
uint32_t flash_read_card_uid(void);
void flash_save_card_uid(uint32_t uid);

#endif

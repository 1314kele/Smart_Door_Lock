#ifndef __RC522_H
#define __RC522_H
#include "stm32f4xx.h"

void rc522_init(void);
uint8_t rc522_read_card(uint32_t *card_id);
extern uint8_t rc522_ver;

#endif


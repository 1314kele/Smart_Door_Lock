#ifndef __OLED_H
#define __OLED_H
#include "stm32f4xx.h"

void oled_init(void);
void oled_clear(void);
void oled_show_string(uint8_t x, uint8_t y, char *str);

#endif


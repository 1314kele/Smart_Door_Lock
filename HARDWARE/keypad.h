#ifndef __KEYPAD_H
#define __KEYPAD_H
#include "stm32f4xx.h"

void keypad_init(void);
uint8_t keypad_scan(void);

#endif


#ifndef __AS608_H
#define __AS608_H
#include "stm32f4xx.h"

void as608_init(void);
uint8_t as608_handshake(void);
uint8_t as608_search_finger(uint16_t *finger_id);
uint8_t as608_enroll_finger(uint16_t finger_id);
void as608_clear_all(void);

#endif

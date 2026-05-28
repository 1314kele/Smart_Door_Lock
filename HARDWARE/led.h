#ifndef _LED_H_
#define _LED_H_

#include "sys.h"

#define D1 PFout(9)
#define D2 PFout(10)
#define D3 PEout(13)
#define D4 PEout(14)

void delay(unsigned int ms);
void led_init(void);

#endif

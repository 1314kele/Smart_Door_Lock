#ifndef _SR04_H_
#define _SR04_H_

#include "sys.h"

#define TRIG PEout(6)
#define ECHO PAin(8)

void sr04_init(void);
int get_distance(void);

#endif

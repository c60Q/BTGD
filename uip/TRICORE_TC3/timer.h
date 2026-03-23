#ifndef TIMER_PORT_H
#define TIMER_PORT_H

#include "boot_types.h"

void TimerInit(void);
void TimerReset(void);
void TimerUpdate(void);
boot_int32u TimerGet(void);

#endif
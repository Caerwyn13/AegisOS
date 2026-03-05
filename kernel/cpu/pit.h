#ifndef PIT_H
#define PIT_H

#include "types.h"

void pit_init(uint32_t frequency);
uint32_t pit_ticks();
void pit_sleep(uint32_t ms);

#endif // PIT_H
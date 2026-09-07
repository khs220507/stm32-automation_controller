#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

void timebase_init(void);
uint32_t timebase_now_us(void);
uint32_t timebase_elapsed_us(uint32_t start_time_us);
void timebase_delay_us(uint32_t delay_us);

#endif

#ifndef MARS_OS_TIMER_H
#define MARS_OS_TIMER_H

#include <stdint.h>

void timer_init(uint32_t hz);
void timer_isr(void);
uint64_t timer_get_ticks(void);
uint32_t timer_get_ticks32(void);
uint32_t timer_get_hz(void);
uint32_t timer_get_uptime_seconds(void);

#endif

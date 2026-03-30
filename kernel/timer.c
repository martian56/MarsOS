#include <stdint.h>

#include "io.h"
#include "scheduler.h"
#include "timer.h"

#define PIT_INPUT_HZ 1193182u

static volatile uint64_t timer_ticks;
static uint32_t timer_hz = 100u;

static uint64_t timer_read_ticks_atomic(void) {
    uint32_t eflags;
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("pushfl; popl %0; cli" : "=r"(eflags) : : "memory");
    lo = (uint32_t)timer_ticks;
    hi = (uint32_t)(timer_ticks >> 32);
    __asm__ volatile("pushl %0; popfl" : : "r"(eflags) : "memory", "cc");

    return ((uint64_t)hi << 32) | lo;
}

void timer_init(uint32_t hz) {
    uint32_t divisor;

    if (hz == 0) {
        hz = 100;
    }

    timer_hz = hz;
    timer_ticks = 0;

    divisor = PIT_INPUT_HZ / hz;
    if (divisor == 0) {
        divisor = 1;
    }

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFFu));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFFu));
}

void timer_isr(void) {
    timer_ticks++;
    scheduler_on_tick();
}

uint64_t timer_get_ticks(void) { return timer_read_ticks_atomic(); }

uint32_t timer_get_ticks32(void) { return (uint32_t)timer_read_ticks_atomic(); }

uint32_t timer_get_hz(void) { return timer_hz; }

uint32_t timer_get_uptime_seconds(void) {
    const uint32_t ticks32 = (uint32_t)timer_read_ticks_atomic();
    return ticks32 / timer_hz;
}

#include <stdint.h>

#include "io.h"
#include "serial.h"

#define COM1 0x3F8

static int serial_ready;

static int serial_can_transmit(void) { return (inb(COM1 + 5) & 0x20u) != 0; }

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    serial_ready = 1;
}

void serial_putc(char c) {
    if (!serial_ready) {
        return;
    }

    while (!serial_can_transmit()) {
    }

    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s != '\0') {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

void serial_put_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";

    serial_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> shift) & 0xF]);
    }
}

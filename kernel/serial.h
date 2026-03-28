#ifndef MARS_OS_SERIAL_H
#define MARS_OS_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_put_hex32(uint32_t value);

#endif

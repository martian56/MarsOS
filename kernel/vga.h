#ifndef MARS_OS_VGA_H
#define MARS_OS_VGA_H

#include <stdint.h>

void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_put_hex32(uint32_t value);
void vga_put_hex64(uint64_t value);
void vga_put_dec32(uint32_t value);
void vga_backspace(void);

#endif

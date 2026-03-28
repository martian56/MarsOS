#include "vga.h"

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static const uint8_t vga_color = 0x07;
static uint32_t cursor_row;
static uint32_t cursor_col;

static void vga_scroll(void) {
    for (uint32_t row = 1; row < VGA_HEIGHT; row++) {
        for (uint32_t col = 0; col < VGA_WIDTH; col++) {
            vga[(row - 1) * VGA_WIDTH + col] = vga[row * VGA_WIDTH + col];
        }
    }

    for (uint32_t col = 0; col < VGA_WIDTH; col++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = ((uint16_t)vga_color << 8) | ' ';
    }
}

static void vga_newline(void) {
    cursor_col = 0;
    cursor_row++;

    if (cursor_row >= VGA_HEIGHT) {
        vga_scroll();
        cursor_row = VGA_HEIGHT - 1;
    }
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_newline();
        return;
    }

    if (c == '\b') {
        vga_backspace();
        return;
    }

    if (cursor_col >= VGA_WIDTH) {
        vga_newline();
    }

    const uint32_t idx = cursor_row * VGA_WIDTH + cursor_col;
    vga[idx] = ((uint16_t)vga_color << 8) | (uint8_t)c;
    cursor_col++;
}

void vga_puts(const char *s) {
    while (*s != '\0') {
        vga_putc(*s++);
    }
}

void vga_put_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";

    vga_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        vga_putc(digits[(value >> shift) & 0xF]);
    }
}

void vga_put_hex64(uint64_t value) {
    vga_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t nibble = (uint8_t)((value >> shift) & 0xFu);
        vga_putc("0123456789ABCDEF"[nibble]);
    }
}

void vga_put_dec32(uint32_t value) {
    char buf[10];
    int i = 0;

    if (value == 0) {
        vga_putc('0');
        return;
    }

    while (value > 0) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        vga_putc(buf[--i]);
    }
}

void vga_clear(void) {
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = ((uint16_t)vga_color << 8) | ' ';
    }
    cursor_row = 0;
    cursor_col = 0;
}

void vga_backspace(void) {
    if (cursor_col == 0) {
        if (cursor_row == 0) {
            return;
        }
        cursor_row--;
        cursor_col = VGA_WIDTH;
    }

    cursor_col--;
    vga[cursor_row * VGA_WIDTH + cursor_col] = ((uint16_t)vga_color << 8) | ' ';
}

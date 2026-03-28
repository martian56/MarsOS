#include <stdint.h>

#include "io.h"
#include "keyboard.h"

#define KEYBOARD_BUFFER_SIZE 256u

static const char keymap[128] = {
    [0x02] = '1',  [0x03] = '2',  [0x04] = '3',  [0x05] = '4',  [0x06] = '5', [0x07] = '6',
    [0x08] = '7',  [0x09] = '8',  [0x0A] = '9',  [0x0B] = '0',  [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'q',  [0x11] = 'w',  [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't',  [0x15] = 'y',  [0x16] = 'u',  [0x17] = 'i',  [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[',  [0x1B] = ']',  [0x1C] = '\n', [0x1E] = 'a',  [0x1F] = 's', [0x20] = 'd',
    [0x21] = 'f',  [0x22] = 'g',  [0x23] = 'h',  [0x24] = 'j',  [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';',  [0x28] = '\'', [0x29] = '`',  [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x',
    [0x2E] = 'c',  [0x2F] = 'v',  [0x30] = 'b',  [0x31] = 'n',  [0x32] = 'm', [0x33] = ',',
    [0x34] = '.',  [0x35] = '/',  [0x39] = ' '};

static const char keymap_shift[128] = {
    [0x02] = '!',  [0x03] = '@',  [0x04] = '#',  [0x05] = '$', [0x06] = '%', [0x07] = '^',
    [0x08] = '&',  [0x09] = '*',  [0x0A] = '(',  [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'Q',  [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T',  [0x15] = 'Y',  [0x16] = 'U',  [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{',  [0x1B] = '}',  [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
    [0x21] = 'F',  [0x22] = 'G',  [0x23] = 'H',  [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':',  [0x28] = '"',  [0x29] = '~',  [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X',
    [0x2E] = 'C',  [0x2F] = 'V',  [0x30] = 'B',  [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>',  [0x35] = '?',  [0x39] = ' '};

static uint8_t shift;
static uint8_t extended;
static volatile uint8_t buffer_head;
static volatile uint8_t buffer_tail;
static char buffer[KEYBOARD_BUFFER_SIZE];

static void keyboard_buffer_push(char c) {
    const uint8_t next = (uint8_t)(buffer_head + 1u);

    if (next == buffer_tail) {
        return;
    }

    buffer[buffer_head] = c;
    buffer_head = next;
}

static void keyboard_handle_scancode(uint8_t scancode) {
    char c = 0;

    if (scancode == 0xE0) {
        extended = 1;
        return;
    }

    if (extended != 0) {
        extended = 0;

        if ((scancode & 0x80u) != 0) {
            return;
        }

        if (scancode == 0x48) {
            keyboard_buffer_push(KEY_SPECIAL_UP);
        } else if (scancode == 0x50) {
            keyboard_buffer_push(KEY_SPECIAL_DOWN);
        } else if (scancode == 0x4B) {
            keyboard_buffer_push(KEY_SPECIAL_LEFT);
        } else if (scancode == 0x4D) {
            keyboard_buffer_push(KEY_SPECIAL_RIGHT);
        } else if (scancode == 0x53) {
            keyboard_buffer_push(KEY_SPECIAL_DELETE);
        }
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift = 0;
        return;
    }

    if ((scancode & 0x80u) != 0) {
        return;
    }

    if (shift != 0) {
        c = keymap_shift[scancode];
    } else {
        c = keymap[scancode];
    }

    if (c != 0) {
        keyboard_buffer_push(c);
    }
}

void keyboard_isr(void) {
    if ((inb(0x64) & 1u) == 0) {
        return;
    }

    keyboard_handle_scancode(inb(0x60));
}

void keyboard_buffer_reset(void) {
    shift = 0;
    extended = 0;
    buffer_head = 0;
    buffer_tail = 0;
}

char keyboard_try_getchar(void) {
    char c;

    if (buffer_head == buffer_tail) {
        return 0;
    }

    c = buffer[buffer_tail];
    buffer_tail = (uint8_t)(buffer_tail + 1u);
    return c;
}

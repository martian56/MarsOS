#ifndef MARS_OS_KEYBOARD_H
#define MARS_OS_KEYBOARD_H

#define KEY_SPECIAL_UP ((char)0x11)
#define KEY_SPECIAL_DOWN ((char)0x12)
#define KEY_SPECIAL_LEFT ((char)0x13)
#define KEY_SPECIAL_RIGHT ((char)0x14)
#define KEY_SPECIAL_DELETE ((char)0x15)

void keyboard_isr(void);
void keyboard_buffer_reset(void);
char keyboard_try_getchar(void);

#endif

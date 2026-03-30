#ifndef MARS_OS_SYSCALL_H
#define MARS_OS_SYSCALL_H

#include <stdint.h>

uint32_t syscall_dispatch(uint32_t *frame);

#endif
#ifndef MARS_OS_GDT_H
#define MARS_OS_GDT_H

#include <stdint.h>

void gdt_init(uint32_t kernel_stack_top);
void gdt_set_kernel_stack(uint32_t kernel_stack_top);

#endif
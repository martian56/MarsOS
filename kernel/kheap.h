#ifndef MARS_OS_KHEAP_H
#define MARS_OS_KHEAP_H

#include <stddef.h>
#include <stdint.h>

int kheap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
uint32_t kheap_total_bytes(void);
uint32_t kheap_used_bytes(void);

#endif
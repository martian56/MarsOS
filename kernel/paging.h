#ifndef MARS_OS_PAGING_H
#define MARS_OS_PAGING_H

#include <stdint.h>

#define PAGING_FLAG_PRESENT 0x001u
#define PAGING_FLAG_RW 0x002u

void paging_init(void);
int paging_enabled(void);
int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
int paging_unmap_page(uint32_t virt);
int paging_translate(uint32_t virt, uint32_t *phys_out);
void paging_handle_page_fault(uint32_t error_code);

#endif

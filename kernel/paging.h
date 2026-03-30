#ifndef MARS_OS_PAGING_H
#define MARS_OS_PAGING_H

#include <stdint.h>

#define PAGING_FLAG_PRESENT 0x001u
#define PAGING_FLAG_RW 0x002u
#define PAGING_FLAG_USER 0x004u

void paging_init(void);
int paging_enabled(void);
int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
int paging_unmap_page(uint32_t virt);
int paging_translate(uint32_t virt, uint32_t *phys_out);
int paging_user_accessible(uint32_t virt, uint32_t size);
uint32_t paging_kernel_directory_phys(void);
uint32_t paging_current_directory_phys(void);
int paging_clone_kernel_directory(uint32_t *out_phys);
void paging_switch_directory(uint32_t directory_phys);
void paging_handle_page_fault(uint32_t error_code);

#endif

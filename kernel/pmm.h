#ifndef MARS_OS_PMM_H
#define MARS_OS_PMM_H

#include <stdint.h>

#include "multiboot.h"

void pmm_init(const multiboot_info_t *mbi, uint32_t kernel_end);
uint32_t pmm_alloc_frame(void);
int pmm_free_frame(uint32_t frame);
uint32_t pmm_total_frames(void);
uint32_t pmm_used_frames(void);
uint32_t pmm_free_frames(void);
int pmm_ready(void);

#endif

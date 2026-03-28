#include <stdint.h>

#include "multiboot.h"
#include "pmm.h"

#define PAGE_SIZE 4096u
#define PMM_MAX_REGIONS 32u
#define PMM_FREE_STACK_MAX 4096u

static uint32_t pmm_region_start[PMM_MAX_REGIONS];
static uint32_t pmm_region_end[PMM_MAX_REGIONS];
static uint32_t pmm_region_count;
static uint32_t pmm_region_index;
static uint32_t pmm_next;
static uint32_t pmm_total;
static uint32_t pmm_used;
static uint32_t pmm_free_stack[PMM_FREE_STACK_MAX];
static uint32_t pmm_free_stack_size;
static int pmm_initialized;

static uint32_t align_up_4k(uint32_t value) {
    return (value + (PAGE_SIZE - 1u)) & ~(PAGE_SIZE - 1u);
}

static int pmm_frame_in_regions(uint32_t frame) {
    for (uint32_t i = 0; i < pmm_region_count; i++) {
        if (frame >= pmm_region_start[i] && frame < pmm_region_end[i]) {
            return 1;
        }
    }
    return 0;
}

void pmm_init(const multiboot_info_t *mbi, uint32_t kernel_end) {
    if ((mbi->flags & MULTIBOOT_INFO_MMAP_FLAG) == 0) {
        pmm_initialized = 0;
        return;
    }

    const uint32_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
    multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)mbi->mmap_addr;
    const uint32_t required_start = align_up_4k(kernel_end);

    pmm_initialized = 0;
    pmm_region_count = 0;
    pmm_region_index = 0;
    pmm_next = 0;
    pmm_total = 0;
    pmm_used = 0;
    pmm_free_stack_size = 0;

    while ((uint32_t)entry < mmap_end) {
        if (entry->type == 1u) {
            uint64_t region_start_64 = entry->addr;
            uint64_t region_end_64 = entry->addr + entry->len;

            if (region_start_64 < 0x100000u) {
                region_start_64 = 0x100000u;
            }
            if (region_end_64 > 0xFFFFFFFFu) {
                region_end_64 = 0xFFFFFFFFu;
            }

            if (region_start_64 < region_end_64) {
                uint32_t region_start = align_up_4k((uint32_t)region_start_64);
                uint32_t region_end = (uint32_t)region_end_64 & ~(PAGE_SIZE - 1u);

                if (region_start < required_start) {
                    region_start = align_up_4k(required_start);
                }

                if (region_start < region_end && pmm_region_count < PMM_MAX_REGIONS) {
                    pmm_region_start[pmm_region_count] = region_start;
                    pmm_region_end[pmm_region_count] = region_end;
                    pmm_total += (region_end - region_start) / PAGE_SIZE;
                    pmm_region_count++;
                }
            }
        }

        entry = (multiboot_mmap_entry_t *)((uint32_t)entry + entry->size + sizeof(entry->size));
    }

    if (pmm_region_count == 0) {
        pmm_initialized = 0;
        return;
    }

    pmm_region_index = 0;
    pmm_next = pmm_region_start[0];
    pmm_initialized = 1;
}

uint32_t pmm_alloc_frame(void) {
    uint32_t frame;

    if (!pmm_initialized) {
        return 0;
    }

    if (pmm_free_stack_size > 0) {
        pmm_free_stack_size--;
        pmm_used++;
        return pmm_free_stack[pmm_free_stack_size];
    }

    while (pmm_region_index < pmm_region_count) {
        if (pmm_next < pmm_region_end[pmm_region_index]) {
            frame = pmm_next;
            pmm_next += PAGE_SIZE;
            pmm_used++;
            return frame;
        }

        pmm_region_index++;
        if (pmm_region_index < pmm_region_count) {
            pmm_next = pmm_region_start[pmm_region_index];
        }
    }

    return 0;
}

int pmm_free_frame(uint32_t frame) {
    if (!pmm_initialized) {
        return 0;
    }

    if ((frame & (PAGE_SIZE - 1u)) != 0) {
        return 0;
    }

    if (!pmm_frame_in_regions(frame)) {
        return 0;
    }

    if (pmm_used == 0) {
        return 0;
    }

    if (pmm_free_stack_size >= PMM_FREE_STACK_MAX) {
        return 0;
    }

    pmm_free_stack[pmm_free_stack_size] = frame;
    pmm_free_stack_size++;
    pmm_used--;
    return 1;
}

uint32_t pmm_total_frames(void) {
    if (!pmm_initialized) {
        return 0;
    }
    return pmm_total;
}

uint32_t pmm_used_frames(void) {
    if (!pmm_initialized) {
        return 0;
    }
    return pmm_used;
}

uint32_t pmm_free_frames(void) {
    if (!pmm_initialized || pmm_used >= pmm_total) {
        return 0;
    }
    return pmm_total - pmm_used;
}

int pmm_ready(void) {
    return pmm_initialized;
}

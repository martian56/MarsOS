#include <stdint.h>

#include "multiboot.h"
#include "pmm.h"

#define PAGE_SIZE 4096u
#define PMM_MAX_REGIONS 32u
#define PMM_MAX_FRAMES (1024u * 1024u)
#define PMM_BITMAP_WORDS (PMM_MAX_FRAMES / 32u)

static uint32_t pmm_region_start[PMM_MAX_REGIONS];
static uint32_t pmm_region_end[PMM_MAX_REGIONS];
static uint32_t pmm_region_count;
static uint32_t pmm_total;
static uint32_t pmm_used;
static uint32_t pmm_bitmap[PMM_BITMAP_WORDS];
static uint32_t pmm_hint_frame;
static uint32_t pmm_highest_end;
static int pmm_initialized;

static uint32_t align_up_4k(uint32_t value) {
    return (value + (PAGE_SIZE - 1u)) & ~(PAGE_SIZE - 1u);
}

static uint32_t align_down_4k(uint32_t value) { return value & ~(PAGE_SIZE - 1u); }

static int pmm_bit_is_set(uint32_t frame_index) {
    return (pmm_bitmap[frame_index >> 5] & (1u << (frame_index & 31u))) != 0;
}

static void pmm_set_bit(uint32_t frame_index) {
    pmm_bitmap[frame_index >> 5] |= (1u << (frame_index & 31u));
}

static void pmm_clear_bit(uint32_t frame_index) {
    pmm_bitmap[frame_index >> 5] &= ~(1u << (frame_index & 31u));
}

static int pmm_frame_in_regions(uint32_t frame) {
    for (uint32_t i = 0; i < pmm_region_count; i++) {
        if (frame >= pmm_region_start[i] && frame < pmm_region_end[i]) {
            return 1;
        }
    }
    return 0;
}

static void pmm_mark_region_usable(uint32_t start, uint32_t end) {
    uint32_t frame = align_up_4k(start);
    const uint32_t limit = align_down_4k(end);

    while (frame < limit) {
        const uint32_t frame_index = frame / PAGE_SIZE;
        if (frame_index >= PMM_MAX_FRAMES) {
            break;
        }

        if (pmm_bit_is_set(frame_index)) {
            pmm_clear_bit(frame_index);
            pmm_total++;
        }
        frame += PAGE_SIZE;
    }
}

static void pmm_reserve_range(uint32_t start, uint32_t end) {
    uint32_t frame;
    const uint32_t start_aligned = align_down_4k(start);
    const uint32_t end_aligned = align_up_4k(end);

    if (start_aligned >= end_aligned) {
        return;
    }

    frame = start_aligned;
    while (frame < end_aligned) {
        const uint32_t frame_index = frame / PAGE_SIZE;
        if (frame_index >= PMM_MAX_FRAMES) {
            break;
        }

        if (pmm_frame_in_regions(frame) && !pmm_bit_is_set(frame_index)) {
            pmm_set_bit(frame_index);
            pmm_used++;
        }
        frame += PAGE_SIZE;
    }
}

static void pmm_reserve_multiboot_data(const multiboot_info_t *mbi) {
    pmm_reserve_range((uint32_t)mbi, (uint32_t)mbi + sizeof(*mbi));

    if ((mbi->flags & MULTIBOOT_INFO_MMAP_FLAG) != 0) {
        pmm_reserve_range(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);
    }

    if (mbi->cmdline != 0) {
        const char *cmdline = (const char *)mbi->cmdline;
        uint32_t len = 0;
        while (len < 4096u) {
            if (cmdline[len] == '\0') {
                len++;
                break;
            }
            len++;
        }
        if (len == 0) {
            len = 1;
        }
        pmm_reserve_range(mbi->cmdline, mbi->cmdline + len);
    }

    if (mbi->mods_count > 0 && mbi->mods_addr != 0) {
        const uint32_t mods_bytes = mbi->mods_count * (uint32_t)sizeof(multiboot_module_t);
        multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;

        pmm_reserve_range(mbi->mods_addr, mbi->mods_addr + mods_bytes);

        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            if (mods[i].mod_end > mods[i].mod_start) {
                pmm_reserve_range(mods[i].mod_start, mods[i].mod_end);
            }
            if (mods[i].string != 0) {
                const char *mod_name = (const char *)mods[i].string;
                uint32_t len = 0;
                while (len < 4096u) {
                    if (mod_name[len] == '\0') {
                        len++;
                        break;
                    }
                    len++;
                }
                if (len == 0) {
                    len = 1;
                }
                pmm_reserve_range(mods[i].string, mods[i].string + len);
            }
        }
    }
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
    pmm_total = 0;
    pmm_used = 0;
    pmm_hint_frame = 0;
    pmm_highest_end = 0;

    for (uint32_t i = 0; i < PMM_BITMAP_WORDS; i++) {
        pmm_bitmap[i] = 0xFFFFFFFFu;
    }

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
                    if (region_end > pmm_highest_end) {
                        pmm_highest_end = region_end;
                    }
                    pmm_region_count++;
                    pmm_mark_region_usable(region_start, region_end);
                }
            }
        }

        entry = (multiboot_mmap_entry_t *)((uint32_t)entry + entry->size + sizeof(entry->size));
    }

    if (pmm_region_count == 0) {
        pmm_initialized = 0;
        return;
    }

    pmm_reserve_multiboot_data(mbi);
    pmm_initialized = 1;
}

uint32_t pmm_alloc_frame(void) {
    uint32_t checked = 0;
    uint32_t frame_index = pmm_hint_frame;

    if (!pmm_initialized) {
        return 0;
    }

    if (pmm_used >= pmm_total) {
        return 0;
    }

    while (checked < PMM_MAX_FRAMES) {
        if (!pmm_bit_is_set(frame_index)) {
            const uint32_t frame = frame_index * PAGE_SIZE;
            if (pmm_frame_in_regions(frame)) {
                pmm_set_bit(frame_index);
                pmm_hint_frame = (frame_index + 1u) % PMM_MAX_FRAMES;
                pmm_used++;
                return frame;
            }
        }

        frame_index++;
        if (frame_index >= PMM_MAX_FRAMES) {
            frame_index = 0;
        }
        checked++;
    }

    return 0;
}

int pmm_free_frame(uint32_t frame) {
    const uint32_t frame_addr = frame & ~(PAGE_SIZE - 1u);
    const uint32_t frame_index = frame_addr / PAGE_SIZE;

    if (!pmm_initialized) {
        return 0;
    }

    if ((frame & (PAGE_SIZE - 1u)) != 0) {
        return 0;
    }

    if (frame_index >= PMM_MAX_FRAMES) {
        return 0;
    }

    if (!pmm_frame_in_regions(frame_addr)) {
        return 0;
    }

    if (!pmm_bit_is_set(frame_index)) {
        return 0;
    }

    pmm_clear_bit(frame_index);
    if (frame_index < pmm_hint_frame) {
        pmm_hint_frame = frame_index;
    }
    if (pmm_used > 0) {
        pmm_used--;
    }
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

uint32_t pmm_highest_address(void) {
    if (!pmm_initialized) {
        return 0;
    }
    return pmm_highest_end;
}

int pmm_ready(void) { return pmm_initialized; }

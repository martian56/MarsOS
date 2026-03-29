#include <stdint.h>

#include "paging.h"
#include "serial.h"
#include "vga.h"

#define IDENTITY_TABLES 4u
#define TABLE_POOL_SIZE 32u

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[IDENTITY_TABLES][1024] __attribute__((aligned(4096)));
static uint32_t extra_tables[TABLE_POOL_SIZE][1024] __attribute__((aligned(4096)));
static uint32_t next_extra_table;
static int paging_is_enabled;

static uint32_t *paging_alloc_table(void) {
    uint32_t *table;

    if (next_extra_table >= TABLE_POOL_SIZE) {
        return 0;
    }

    table = &extra_tables[next_extra_table][0];
    next_extra_table++;

    for (uint32_t i = 0; i < 1024u; i++) {
        table[i] = 0;
    }

    return table;
}

static void paging_invalidate(uint32_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void paging_init(void) {
    for (uint32_t i = 0; i < 1024u; i++) {
        page_directory[i] = 0;
    }

    next_extra_table = 0;

    for (uint32_t t = 0; t < IDENTITY_TABLES; t++) {
        for (uint32_t e = 0; e < 1024u; e++) {
            const uint32_t phys = ((t * 1024u) + e) * 4096u;
            page_tables[t][e] = phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
        }

        page_directory[t] = ((uint32_t)page_tables[t]) | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(&page_directory[0]) : "memory");

    {
        uint32_t cr0;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x80000000u;
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }

    paging_is_enabled = 1;
}

int paging_enabled(void) { return paging_is_enabled; }

int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    const uint32_t pd_index = (virt >> 22) & 0x3FFu;
    const uint32_t pt_index = (virt >> 12) & 0x3FFu;
    uint32_t *table;

    if ((flags & PAGING_FLAG_PRESENT) == 0) {
        flags |= PAGING_FLAG_PRESENT;
    }

    if ((page_directory[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        table = paging_alloc_table();
        if (table == 0) {
            return 0;
        }

        page_directory[pd_index] = ((uint32_t)table) | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
    }

    table = (uint32_t *)(page_directory[pd_index] & 0xFFFFF000u);
    table[pt_index] = (phys & 0xFFFFF000u) | (flags & 0xFFFu);
    paging_invalidate(virt);
    return 1;
}

int paging_unmap_page(uint32_t virt) {
    const uint32_t pd_index = (virt >> 22) & 0x3FFu;
    const uint32_t pt_index = (virt >> 12) & 0x3FFu;
    uint32_t *table;

    if ((page_directory[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    table = (uint32_t *)(page_directory[pd_index] & 0xFFFFF000u);
    if ((table[pt_index] & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    table[pt_index] = 0;
    paging_invalidate(virt);
    return 1;
}

int paging_translate(uint32_t virt, uint32_t *phys_out) {
    const uint32_t pd_index = (virt >> 22) & 0x3FFu;
    const uint32_t pt_index = (virt >> 12) & 0x3FFu;
    uint32_t *table;
    uint32_t pte;

    if ((page_directory[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    table = (uint32_t *)(page_directory[pd_index] & 0xFFFFF000u);
    pte = table[pt_index];
    if ((pte & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    *phys_out = (pte & 0xFFFFF000u) | (virt & 0xFFFu);
    return 1;
}

void paging_handle_page_fault(uint32_t error_code) {
    uint32_t cr2;

    __asm__ volatile("cli");
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    vga_puts("\n\nKERNEL PANIC: page fault\n");
    vga_puts("cr2=");
    vga_put_hex32(cr2);
    vga_puts(" err=");
    vga_put_hex32(error_code);
    vga_putc('\n');

    serial_puts("KERNEL PANIC: page fault cr2=");
    serial_put_hex32(cr2);
    serial_puts(" err=");
    serial_put_hex32(error_code);
    serial_puts("\n");

    while (1) {
        __asm__ volatile("hlt");
    }
}

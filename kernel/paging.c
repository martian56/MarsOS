#include <stdint.h>

#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "serial.h"
#include "vga.h"

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static int paging_is_enabled;
static uint32_t paging_kernel_dir_phys;
static uint32_t paging_current_dir_phys;

static uint32_t *paging_active_directory(void) {
    if (paging_current_dir_phys == 0) {
        return &page_directory[0];
    }

    return (uint32_t *)paging_current_dir_phys;
}

static uint32_t align_up_4k(uint32_t value) { return (value + 0xFFFu) & ~0xFFFu; }

static uint32_t *paging_alloc_table(uint32_t *table_phys_out) {
    const uint32_t frame = pmm_alloc_frame();
    uint32_t *table;

    if (frame == 0) {
        return 0;
    }

    table = (uint32_t *)frame;

    for (uint32_t i = 0; i < 1024u; i++) {
        table[i] = 0;
    }

    *table_phys_out = frame;

    return table;
}

static void paging_invalidate(uint32_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void paging_init(void) {
    uint32_t identity_end;
    uint32_t pd_count;

    for (uint32_t i = 0; i < 1024u; i++) {
        page_directory[i] = 0;
    }

    if (!pmm_ready()) {
        paging_is_enabled = 0;
        return;
    }

    identity_end = align_up_4k(pmm_highest_address());
    if (identity_end < (4u * 1024u * 1024u)) {
        identity_end = 4u * 1024u * 1024u;
    }

    pd_count = (identity_end + 0x3FFFFFu) >> 22;
    if (pd_count > 1024u) {
        pd_count = 1024u;
        identity_end = 0xFFFFFFFFu;
    }

    for (uint32_t pd = 0; pd < pd_count; pd++) {
        uint32_t table_phys;
        uint32_t *table = paging_alloc_table(&table_phys);

        if (table == 0) {
            paging_is_enabled = 0;
            return;
        }

        for (uint32_t e = 0; e < 1024u; e++) {
            const uint32_t phys = (pd << 22) | (e << 12);
            if (phys < identity_end) {
                table[e] = phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
            } else {
                table[e] = 0;
            }
        }

        page_directory[pd] = table_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(&page_directory[0]) : "memory");

    {
        uint32_t cr0;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x80000000u;
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }

    paging_kernel_dir_phys = (uint32_t)&page_directory[0];
    paging_current_dir_phys = paging_kernel_dir_phys;
    paging_is_enabled = 1;
}

int paging_enabled(void) { return paging_is_enabled; }

int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    const uint32_t pd_index = (virt >> 22) & 0x3FFu;
    const uint32_t pt_index = (virt >> 12) & 0x3FFu;
    uint32_t *directory;
    uint32_t *table;

    if (!paging_is_enabled) {
        return 0;
    }

    if ((virt & 0xFFFu) != 0 || (phys & 0xFFFu) != 0) {
        return 0;
    }

    if ((flags & PAGING_FLAG_PRESENT) == 0) {
        flags |= PAGING_FLAG_PRESENT;
    }

    directory = paging_active_directory();

    if ((directory[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        uint32_t table_phys;
        uint32_t pde_flags = PAGING_FLAG_PRESENT | PAGING_FLAG_RW;

        table = paging_alloc_table(&table_phys);
        if (table == 0) {
            return 0;
        }

        if ((flags & PAGING_FLAG_USER) != 0) {
            pde_flags |= PAGING_FLAG_USER;
        }

        directory[pd_index] = table_phys | pde_flags;
    } else if ((flags & PAGING_FLAG_USER) != 0) {
        directory[pd_index] |= PAGING_FLAG_USER;
    }

    table = (uint32_t *)(directory[pd_index] & 0xFFFFF000u);
    if ((table[pt_index] & PAGING_FLAG_PRESENT) != 0) {
        return 0;
    }

    table[pt_index] = (phys & 0xFFFFF000u) | (flags & 0xFFFu);
    paging_invalidate(virt);
    return 1;
}

int paging_unmap_page(uint32_t virt) {
    const uint32_t pd_index = (virt >> 22) & 0x3FFu;
    const uint32_t pt_index = (virt >> 12) & 0x3FFu;
    uint32_t *directory;
    uint32_t *table;

    if (!paging_is_enabled) {
        return 0;
    }

    if ((virt & 0xFFFu) != 0) {
        return 0;
    }

    directory = paging_active_directory();

    if ((directory[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    table = (uint32_t *)(directory[pd_index] & 0xFFFFF000u);
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
    uint32_t *directory;
    uint32_t *table;
    uint32_t pte;

    if (!paging_is_enabled || phys_out == 0) {
        return 0;
    }

    directory = paging_active_directory();

    if ((directory[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    table = (uint32_t *)(directory[pd_index] & 0xFFFFF000u);
    pte = table[pt_index];
    if ((pte & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    *phys_out = (pte & 0xFFFFF000u) | (virt & 0xFFFu);
    return 1;
}

int paging_user_accessible(uint32_t virt, uint32_t size) {
    uint32_t start;
    uint32_t end;
    uint32_t page;
    uint32_t *directory;

    if (!paging_is_enabled || size == 0u) {
        return 0;
    }

    start = virt;
    if (start > UINT32_MAX - (size - 1u)) {
        return 0;
    }

    end = start + size - 1u;
    page = start & 0xFFFFF000u;
    directory = paging_active_directory();

    while (1) {
        const uint32_t pd_index = (page >> 22) & 0x3FFu;
        const uint32_t pt_index = (page >> 12) & 0x3FFu;
        uint32_t pde;
        uint32_t *table;
        uint32_t pte;

        pde = directory[pd_index];
        if ((pde & (PAGING_FLAG_PRESENT | PAGING_FLAG_USER)) !=
            (PAGING_FLAG_PRESENT | PAGING_FLAG_USER)) {
            return 0;
        }

        table = (uint32_t *)(pde & 0xFFFFF000u);
        pte = table[pt_index];
        if ((pte & (PAGING_FLAG_PRESENT | PAGING_FLAG_USER)) !=
            (PAGING_FLAG_PRESENT | PAGING_FLAG_USER)) {
            return 0;
        }

        if (page >= (end & 0xFFFFF000u)) {
            break;
        }

        page += 0x1000u;
    }

    return 1;
}

uint32_t paging_kernel_directory_phys(void) { return paging_kernel_dir_phys; }

uint32_t paging_current_directory_phys(void) {
    if (!paging_is_enabled) {
        return 0;
    }

    return paging_current_dir_phys;
}

int paging_clone_kernel_directory(uint32_t *out_phys) {
    uint32_t frame;
    uint32_t *new_dir;
    uint32_t *kernel_dir;

    if (!paging_is_enabled || out_phys == 0) {
        return 0;
    }

    frame = pmm_alloc_frame();
    if (frame == 0) {
        return 0;
    }

    new_dir = (uint32_t *)frame;
    kernel_dir = (uint32_t *)paging_kernel_dir_phys;

    for (uint32_t i = 0; i < 1024u; i++) {
        uint32_t pde = kernel_dir[i];

        /*
         * User-space PDEs must not be shared across address spaces.
         * Keep only kernel/global mappings in the cloned directory.
         */
        if ((pde & PAGING_FLAG_USER) != 0) {
            new_dir[i] = 0;
        } else {
            new_dir[i] = pde;
        }
    }

    *out_phys = frame;
    return 1;
}

void paging_switch_directory(uint32_t directory_phys) {
    if (!paging_is_enabled || directory_phys == 0) {
        return;
    }

    if ((directory_phys & 0xFFFu) != 0) {
        return;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(directory_phys) : "memory");
    paging_current_dir_phys = directory_phys;
}

void paging_handle_page_fault(uint32_t error_code) {
    uint32_t cr2;

    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    if ((error_code & 0x4u) != 0u && scheduler_current_task_id() != 0u) {
        vga_puts("\nuser page fault: task terminated\n");
        vga_puts("cr2=");
        vga_put_hex32(cr2);
        vga_puts(" err=");
        vga_put_hex32(error_code);
        vga_putc('\n');

        serial_puts("user page fault: task terminated cr2=");
        serial_put_hex32(cr2);
        serial_puts(" err=");
        serial_put_hex32(error_code);
        serial_puts("\n");

        scheduler_exit_current();
        return;
    }

    __asm__ volatile("cli");

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

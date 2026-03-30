#include <stdint.h>

#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern void gdt_flush(struct gdt_ptr *ptr, uint32_t data_selector);
extern void tss_flush(uint32_t tss_selector);

static struct gdt_entry gdt[6];
static struct gdt_ptr gdt_descriptor;
static struct tss_entry tss;

static void gdt_set_entry(uint32_t index, uint32_t base, uint32_t limit, uint8_t access,
                          uint8_t flags) {
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFFu);
    gdt[index].base_low = (uint16_t)(base & 0xFFFFu);
    gdt[index].base_mid = (uint8_t)((base >> 16) & 0xFFu);
    gdt[index].access = access;
    gdt[index].granularity = (uint8_t)(((limit >> 16) & 0x0Fu) | (flags & 0xF0u));
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFFu);
}

void gdt_set_kernel_stack(uint32_t kernel_stack_top) { tss.esp0 = kernel_stack_top; }

void gdt_init(uint32_t kernel_stack_top) {
    for (uint32_t i = 0; i < 6u; i++) {
        gdt_set_entry(i, 0, 0, 0, 0);
    }

    gdt_set_entry(1, 0, 0xFFFFFu, 0x9Au, 0xC0u);
    gdt_set_entry(2, 0, 0xFFFFFu, 0x92u, 0xC0u);
    gdt_set_entry(3, 0, 0xFFFFFu, 0xFAu, 0xC0u);
    gdt_set_entry(4, 0, 0xFFFFFu, 0xF2u, 0xC0u);

    for (uint32_t i = 0; i < sizeof(tss); i++) {
        ((uint8_t *)&tss)[i] = 0;
    }

    tss.ss0 = 0x10u;
    tss.esp0 = kernel_stack_top;
    tss.cs = 0x0Bu;
    tss.ss = 0x13u;
    tss.ds = 0x13u;
    tss.es = 0x13u;
    tss.fs = 0x13u;
    tss.gs = 0x13u;
    tss.iomap_base = sizeof(struct tss_entry);

    gdt_set_entry(5, (uint32_t)&tss, sizeof(struct tss_entry) - 1u, 0x89u, 0x00u);

    gdt_descriptor.limit = sizeof(gdt) - 1u;
    gdt_descriptor.base = (uint32_t)&gdt[0];

    gdt_flush(&gdt_descriptor, 0x10u);
    tss_flush(0x28u);
}

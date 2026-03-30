#include <stdint.h>

#include "io.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"

#define IDT_ENTRIES 256u
#define IDT_TYPE_INTERRUPT_GATE 0x8Eu
#define IDT_TYPE_TRAP_GATE_USER 0xEFu

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern void irq1_stub(void);
extern void irq0_stub(void);
extern void irq_master_stub(void);
extern void irq_slave_stub(void);
extern void isr_fault_stub(void);
extern void page_fault_stub(void);
extern void syscall_stub(void);
extern void usermode_exit_stub(void);
extern void keyboard_buffer_reset(void);

static struct idt_entry idt[IDT_ENTRIES];
static uint16_t idt_code_selector;

static void idt_set_gate_attr(uint8_t vector, void (*handler)(void), uint8_t type_attr) {
    const uint32_t addr = (uint32_t)handler;

    idt[vector].offset_low = (uint16_t)(addr & 0xFFFFu);
    idt[vector].selector = idt_code_selector;
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_high = (uint16_t)((addr >> 16) & 0xFFFFu);
}

static void idt_set_gate(uint8_t vector, void (*handler)(void)) {
    idt_set_gate_attr(vector, handler, IDT_TYPE_INTERRUPT_GATE);
}

static void idt_load(void) {
    struct idt_ptr ptr;

    ptr.limit = (uint16_t)(sizeof(idt) - 1u);
    ptr.base = (uint32_t)&idt[0];

    __asm__ volatile("lidt %0" : : "m"(ptr));
}

static void pic_remap_and_mask(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void interrupts_init(void) {
    __asm__ volatile("cli");
    __asm__ volatile("mov %%cs, %0" : "=r"(idt_code_selector));

    for (uint32_t i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    for (uint8_t vec = 0x00; vec <= 0x1F; vec++) {
        idt_set_gate(vec, isr_fault_stub);
    }
    idt_set_gate(0x0E, page_fault_stub);

    for (uint8_t vec = 0x20; vec <= 0x27; vec++) {
        idt_set_gate(vec, irq_master_stub);
    }
    for (uint8_t vec = 0x28; vec <= 0x2F; vec++) {
        idt_set_gate(vec, irq_slave_stub);
    }
    idt_set_gate_attr(0x80, syscall_stub, IDT_TYPE_TRAP_GATE_USER);
    idt_set_gate_attr(0x81, usermode_exit_stub, IDT_TYPE_TRAP_GATE_USER);

    keyboard_buffer_reset();
    idt_set_gate(0x20, irq0_stub);
    idt_set_gate(0x21, irq1_stub);
    idt_load();
    pic_remap_and_mask();

    __asm__ volatile("sti");
}

void interrupts_fault_panic(void) {
    __asm__ volatile("cli");
    vga_puts("\n\nKERNEL PANIC: CPU exception\n");
    vga_puts("System halted\n");
    serial_puts("KERNEL PANIC: CPU exception\n");
    serial_puts("System halted\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}

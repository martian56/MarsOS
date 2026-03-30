#include <stdint.h>

#include "gdt.h"
#include "paging.h"
#include "pmm.h"
#include "usermode.h"

#define USER_CODE_VADDR 0x40000000u
#define USER_STACK_TOP 0x40002000u

extern void usermode_switch(uint32_t entry, uint32_t user_stack_top);

static int prepared;
volatile uint32_t usermode_saved_kernel_esp;

int usermode_prepare(void) {
    uint32_t code_frame;
    uint32_t stack_frame;
    int code_mapped = 0;
    volatile uint8_t *code;

    if (prepared) {
        return 1;
    }

    code_frame = pmm_alloc_frame();
    stack_frame = pmm_alloc_frame();

    if (code_frame == 0 || stack_frame == 0) {
        if (code_frame != 0) {
            pmm_free_frame(code_frame);
        }
        if (stack_frame != 0) {
            pmm_free_frame(stack_frame);
        }
        return 0;
    }

    if (!paging_map_page(USER_CODE_VADDR, code_frame,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER)) {
        pmm_free_frame(stack_frame);
        pmm_free_frame(code_frame);
        return 0;
    }
    code_mapped = 1;

    if (!paging_map_page(USER_STACK_TOP - 0x1000u, stack_frame,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER)) {
        if (code_mapped) {
            paging_unmap_page(USER_CODE_VADDR);
        }
        pmm_free_frame(stack_frame);
        pmm_free_frame(code_frame);
        return 0;
    }

    code = (volatile uint8_t *)USER_CODE_VADDR;

    code[0] = 0xB8; // mov eax, 17
    code[1] = 17;
    code[2] = 0;
    code[3] = 0;
    code[4] = 0;
    code[5] = 0xCD; // int 0x80
    code[6] = 0x80;
    code[7] = 0xCD; // int 0x80
    code[8] = 0x80;
    code[9] = 0xCD; // int 0x81
    code[10] = 0x81;
    code[11] = 0xEB; // jmp -2 (should be unreachable)
    code[12] = 0xFE;

    prepared = 1;
    return 1;
}

int usermode_is_prepared(void) { return prepared; }

void usermode_enter_test(void) {
    uint32_t kernel_stack_top;

    if (!prepared) {
        return;
    }

    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_stack_top));
    gdt_set_kernel_stack(kernel_stack_top);
    usermode_switch(USER_CODE_VADDR, USER_STACK_TOP - 16u);
}

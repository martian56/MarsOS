.section .multiboot,"a"
.align 4
.long 0x1BADB002
.long 0x0
.long -(0x1BADB002)

.section .text
.global _start
.type _start, @function
.extern kernel_main
_start:
    mov $stack_top, %esp
    push %ebx
    push %eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

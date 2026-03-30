.section .text
.global gdt_flush
.global tss_flush
.type gdt_flush, @function
.type tss_flush, @function

gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)

    mov 8(%esp), %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    ljmp $0x08, $.flush_cs
.flush_cs:
    ret

tss_flush:
    mov 4(%esp), %eax
    ltr %ax
    ret

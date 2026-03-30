.section .text
.global irq0_stub
.global irq1_stub
.global irq_master_stub
.global irq_slave_stub
.global isr_fault_stub
.global page_fault_stub
.global syscall_stub
.type irq0_stub, @function
.type irq1_stub, @function
.type irq_master_stub, @function
.type irq_slave_stub, @function
.type isr_fault_stub, @function
.type page_fault_stub, @function
.type syscall_stub, @function
.extern keyboard_isr
.extern timer_isr
.extern interrupts_fault_panic
.extern paging_handle_page_fault
.extern syscall_dispatch

page_fault_stub:
    cld
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    mov 48(%esp), %eax
    push %eax
    call paging_handle_page_fault
    add $4, %esp
    popa
    pop %gs
    pop %fs
    pop %es
    pop %ds
    add $4, %esp
    iret

isr_fault_stub:
    cld
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    call interrupts_fault_panic
.fault_hang:
    cli
    hlt
    jmp .fault_hang

irq_master_stub:
    cld
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    movb $0x20, %al
    outb %al, $0x20
    popa
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

irq_slave_stub:
    cld
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    movb $0x20, %al
    outb %al, $0xA0
    outb %al, $0x20
    popa
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

irq0_stub:
    cld
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    call timer_isr
    movb $0x20, %al
    outb %al, $0x20
    popa
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

irq1_stub:
    cld
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    call keyboard_isr
    movb $0x20, %al
    outb %al, $0x20
    popa
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

syscall_stub:
    cld
    push %ds
    push %es
    push %fs
    push %gs
    pusha
    mov %esp, %eax
    push %eax
    call syscall_dispatch
    add $4, %esp
    mov %eax, 28(%esp)
    popa
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

.section .text
.global irq0_stub
.global irq1_stub
.global irq_master_stub
.global irq_slave_stub
.global isr_fault_stub
.global page_fault_stub
.type irq0_stub, @function
.type irq1_stub, @function
.type irq_master_stub, @function
.type irq_slave_stub, @function
.type isr_fault_stub, @function
.type page_fault_stub, @function
.extern keyboard_isr
.extern timer_isr
.extern interrupts_fault_panic
.extern paging_handle_page_fault

page_fault_stub:
    cli
    pusha
    mov 32(%esp), %eax
    push %eax
    call paging_handle_page_fault
.page_fault_hang:
    hlt
    jmp .page_fault_hang

isr_fault_stub:
    pusha
    call interrupts_fault_panic
.fault_hang:
    cli
    hlt
    jmp .fault_hang

irq_master_stub:
    pusha
    movb $0x20, %al
    outb %al, $0x20
    popa
    iret

irq_slave_stub:
    pusha
    movb $0x20, %al
    outb %al, $0xA0
    outb %al, $0x20
    popa
    iret

irq0_stub:
    pusha
    call timer_isr
    movb $0x20, %al
    outb %al, $0x20
    popa
    iret

irq1_stub:
    pusha
    call keyboard_isr
    movb $0x20, %al
    outb %al, $0x20
    popa
    iret

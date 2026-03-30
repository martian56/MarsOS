.section .text
.global usermode_switch
.global usermode_exit_stub
.type usermode_switch, @function
.type usermode_exit_stub, @function

.extern usermode_saved_kernel_esp
.extern scheduler_current_task_id
.extern scheduler_exit_current

usermode_switch:
    mov %esp, %ecx
    mov %ecx, usermode_saved_kernel_esp

    mov 4(%esp), %eax
    mov 8(%esp), %edx

    mov $0x23, %cx
    mov %cx, %ds
    mov %cx, %es
    mov %cx, %fs
    mov %cx, %gs

    push $0x23
    push %edx

    pushf
    pop %ecx
    or $0x200, %ecx
    push %ecx

    push $0x1B
    push %eax
    iret

usermode_exit_stub:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    call scheduler_current_task_id
    test %eax, %eax
    jz .restore_kernel_ret

    call scheduler_exit_current
.exit_hang:
    hlt
    jmp .exit_hang

.restore_kernel_ret:

    mov usermode_saved_kernel_esp, %eax
    mov %eax, %esp
    ret

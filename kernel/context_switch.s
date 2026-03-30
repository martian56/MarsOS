.section .text
.global context_switch
.type context_switch, @function

context_switch:
    push %ebp
    push %ebx
    push %esi
    push %edi

    mov 20(%esp), %eax
    mov 24(%esp), %edx
    mov %esp, (%eax)
    mov %edx, %esp

    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

.section .text
.global _start
.type _start, @function

_start:
    mov $17, %eax
    int $0x80

    mov $17, %eax
    int $0x80

    mov $17, %eax
    int $0x80

    int $0x81

hang:
    jmp hang

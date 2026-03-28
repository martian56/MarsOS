ENTRY(_start)

SECTIONS {
    . = 1M;

    .multiboot ALIGN(4) : {
        KEEP(*(.multiboot))
    }

    .text ALIGN(16) : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(COMMON)
        *(.bss*)
    }

    __kernel_end = .;
}
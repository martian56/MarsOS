# Mars OS Learning Roadmap

This roadmap is ordered to keep each step small, testable, and educational.

## 1) Boot contract and kernel entry
- Understand what the bootloader guarantees and what your kernel must initialize.
- You now have an assembly bootstrap (`_start`) that sets the stack before calling C.

## 2) Output and diagnostics
- Build a tiny VGA text module (`putc`, `puts`, `clear`).
- Add serial logging (COM1) for debugging even when VGA is unavailable.

## 3) Interrupt foundations
- Add GDT and IDT setup.
- Remap PIC and enable timer + keyboard interrupts.
- Add a minimal panic path for faults.

## 4) Memory management
- Parse Multiboot memory map.
- Implement a physical frame allocator.
- Add paging with a simple virtual memory layout.

## 5) User/kernel boundary
- Add syscall entry and a basic scheduler.
- Transition to user mode with a minimal test task.

## Useful commands
- `make` : build ISO
- `make run` : run in QEMU
- `make check` : verify Multiboot kernel
- `make format` : apply formatting
- `make format-check` : enforce formatting in CI

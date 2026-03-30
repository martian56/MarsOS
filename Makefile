CC = gcc
LD = ld
CLANG_FORMAT ?= $(shell sh -c 'command -v clang-format || command -v clang-format-18 || command -v clang-format-17')

C_SOURCES = kernel/kernel.c kernel/vga.c kernel/keyboard.c kernel/interrupts.c kernel/timer.c kernel/pmm.c kernel/serial.c kernel/paging.c kernel/kheap.c kernel/syscall.c kernel/scheduler.c kernel/vfs.c kernel/process.c kernel/exec.c kernel/ipc.c kernel/gdt.c kernel/usermode.c
ASM_SOURCES = boot/boot.s

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -c
ASFLAGS = -m32 -ffreestanding -c
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

.PHONY: all run clean check smoke format format-check

all: mars.iso

boot/boot.o: boot/boot.s
	$(CC) $(ASFLAGS) $< -o $@

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) $< -o $@

kernel/vga.o: kernel/vga.c kernel/vga.h
	$(CC) $(CFLAGS) $< -o $@

kernel/keyboard.o: kernel/keyboard.c kernel/keyboard.h kernel/io.h
	$(CC) $(CFLAGS) $< -o $@

kernel/interrupts.o: kernel/interrupts.c kernel/interrupts.h kernel/io.h
	$(CC) $(CFLAGS) $< -o $@

kernel/timer.o: kernel/timer.c kernel/timer.h kernel/io.h
	$(CC) $(CFLAGS) $< -o $@

kernel/pmm.o: kernel/pmm.c kernel/pmm.h kernel/multiboot.h
	$(CC) $(CFLAGS) $< -o $@

kernel/serial.o: kernel/serial.c kernel/serial.h kernel/io.h
	$(CC) $(CFLAGS) $< -o $@

kernel/paging.o: kernel/paging.c kernel/paging.h kernel/serial.h kernel/vga.h
	$(CC) $(CFLAGS) $< -o $@

kernel/kheap.o: kernel/kheap.c kernel/kheap.h kernel/paging.h kernel/pmm.h
	$(CC) $(CFLAGS) $< -o $@

kernel/syscall.o: kernel/syscall.c kernel/syscall.h kernel/timer.h kernel/kheap.h kernel/scheduler.h kernel/vfs.h kernel/process.h kernel/exec.h kernel/ipc.h kernel/paging.h
	$(CC) $(CFLAGS) $< -o $@

kernel/scheduler.o: kernel/scheduler.c kernel/scheduler.h kernel/kheap.h
	$(CC) $(CFLAGS) $< -o $@

kernel/vfs.o: kernel/vfs.c kernel/vfs.h kernel/kheap.h
	$(CC) $(CFLAGS) $< -o $@

kernel/process.o: kernel/process.c kernel/process.h kernel/scheduler.h
	$(CC) $(CFLAGS) $< -o $@

kernel/exec.o: kernel/exec.c kernel/exec.h kernel/process.h kernel/vfs.h kernel/scheduler.h kernel/timer.h kernel/usermode.h
	$(CC) $(CFLAGS) $< -o $@

kernel/ipc.o: kernel/ipc.c kernel/ipc.h
	$(CC) $(CFLAGS) $< -o $@

kernel/gdt.o: kernel/gdt.c kernel/gdt.h
	$(CC) $(CFLAGS) $< -o $@

kernel/usermode.o: kernel/usermode.c kernel/usermode.h kernel/gdt.h kernel/paging.h kernel/pmm.h
	$(CC) $(CFLAGS) $< -o $@

kernel/interrupts_asm.o: kernel/interrupts_asm.s
	$(CC) $(ASFLAGS) $< -o $@

kernel/context_switch.o: kernel/context_switch.s
	$(CC) $(ASFLAGS) $< -o $@

kernel/gdt_asm.o: kernel/gdt_asm.s
	$(CC) $(ASFLAGS) $< -o $@

kernel/usermode_asm.o: kernel/usermode_asm.s
	$(CC) $(ASFLAGS) $< -o $@

kernel.bin: boot/boot.o kernel/kernel.o kernel/vga.o kernel/keyboard.o kernel/interrupts.o kernel/timer.o kernel/pmm.o kernel/serial.o kernel/paging.o kernel/kheap.o kernel/syscall.o kernel/scheduler.o kernel/vfs.o kernel/process.o kernel/exec.o kernel/ipc.o kernel/gdt.o kernel/usermode.o kernel/interrupts_asm.o kernel/context_switch.o kernel/gdt_asm.o kernel/usermode_asm.o
	$(LD) $(LDFLAGS) boot/boot.o kernel/kernel.o kernel/vga.o kernel/keyboard.o kernel/interrupts.o kernel/timer.o kernel/pmm.o kernel/serial.o kernel/paging.o kernel/kheap.o kernel/syscall.o kernel/scheduler.o kernel/vfs.o kernel/process.o kernel/exec.o kernel/ipc.o kernel/gdt.o kernel/usermode.o kernel/interrupts_asm.o kernel/context_switch.o kernel/gdt_asm.o kernel/usermode_asm.o -o $@

mars.iso: kernel.bin
	mkdir -p iso/boot
	cp kernel.bin iso/boot/
	grub-mkrescue -o $@ iso

check: kernel.bin
	grub-file --is-x86-multiboot kernel.bin

smoke: mars.iso
	rm -f .smoke.log
	timeout 20s qemu-system-i386 -cdrom mars.iso -display none -serial file:.smoke.log -monitor none || true
	grep -q "selftest: PASS" .smoke.log
	@echo "smoke: PASS"

format:
	@test -n "$(CLANG_FORMAT)" || (echo "clang-format is required (try: sudo apt install clang-format)" && exit 1)
	$(CLANG_FORMAT) -i $(C_SOURCES)

format-check:
	@test -n "$(CLANG_FORMAT)" || (echo "clang-format is required (try: sudo apt install clang-format)" && exit 1)
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)

run: mars.iso
	qemu-system-x86_64 -cdrom mars.iso

clean:
	rm -f boot/boot.o kernel/kernel.o kernel/vga.o kernel/keyboard.o kernel/interrupts.o kernel/timer.o kernel/pmm.o kernel/serial.o kernel/paging.o kernel/kheap.o kernel/syscall.o kernel/scheduler.o kernel/vfs.o kernel/process.o kernel/exec.o kernel/ipc.o kernel/gdt.o kernel/usermode.o kernel/interrupts_asm.o kernel/context_switch.o kernel/gdt_asm.o kernel/usermode_asm.o kernel.bin mars.iso iso/boot/kernel.bin
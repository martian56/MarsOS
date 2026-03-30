# MarsOS Architecture

## Overview

MarsOS is a 32-bit x86 kernel booted via Multiboot/GRUB and packaged as a bootable ISO.

Core design goals:

- deterministic build and smoke validation
- explicit safety checks at user/kernel boundaries
- practical observability for runtime fault triage

## Boot Flow

1. GRUB loads kernel image.
2. Early assembly sets execution environment.
3. Kernel initializes:
   - VGA + serial output
   - PMM and paging
   - kernel heap
   - IDT/PIC/timer/keyboard interrupts
   - scheduler/process manager
   - syscall layer, IPC, VFS
4. Boot selftest runs and enforces safety invariants.
5. Interactive shell loop starts.

## Subsystems

- `boot/`: bootstrap assembly
- `kernel/interrupts*`: IDT/PIC setup, IRQ handlers, fault stubs
- `kernel/pmm.*`: physical frame tracking
- `kernel/paging.*`: virtual memory mapping and page-fault handling
- `kernel/kheap.*`: kernel heap allocator
- `kernel/scheduler.*`: cooperative/preemptive task switching
- `kernel/process.*`: process metadata and user-space setup
- `kernel/syscall.*`: syscall dispatch and user pointer validation
- `kernel/vfs.*`: in-memory file storage and app payloads
- `kernel/exec.*`: program registry and spawn paths

## User Mode and App Loading

User processes run with isolated user mappings and syscall restrictions.

Loader behavior:

- prefers VFS-backed `MARSHEX` app content
- supports strict ELF32 `ET_EXEC` parsing
- validates PT_LOAD segment constraints and entrypoint placement
- rejects malformed image content instead of silently executing it

## Validation Strategy

Primary local gates:

- `make format-check`
- `make check`
- `make smoke`

`make smoke` enforces serial-log invariants from boot selftest, including:

- user ping delta behavior
- malformed-image rejection markers
- syscall overlong-input rejection marker
- process lifecycle stress and leak markers

## Release Governance

Release process is workflow-driven and VERSION-gated:

- `VERSION` is source of truth
- tag must be `v<VERSION>` (base semver match)
- release workflow runs format/build/check/smoke before publishing assets

See [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) for release criteria.

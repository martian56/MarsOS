# MarsOS

[![CI](https://img.shields.io/github/actions/workflow/status/martian56/MarsOS/ci.yml?branch=main&label=CI&logo=githubactions)](https://github.com/martian56/MarsOS/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/martian56/MarsOS?display_name=tag&logo=github)](https://github.com/martian56/MarsOS/releases)
[![License](https://img.shields.io/github/license/martian56/MarsOS)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-x86_32-0A66C2?logo=intel)](https://en.wikipedia.org/wiki/X86)
[![Build](https://img.shields.io/badge/build-make-427819?logo=gnu)](Makefile)

MarsOS is a 32-bit x86 hobby operating system focused on kernel fundamentals, safety hardening, and reproducible build and release workflows.

It is designed as a practical learning codebase for:

- low-level boot and interrupt setup
- memory management and process lifecycle
- user/kernel transitions and syscall boundaries
- automated smoke testing for kernel regression control

## Highlights

- Multiboot + GRUB boot pipeline (`kernel.bin` -> `mars.iso`)
- Interrupt-driven keyboard and timer IRQ handling
- Paging, PMM, and a small kernel heap allocator
- Kernel scheduler with user and kernel process support
- Syscall boundary checks with user pointer validation
- In-memory VFS and packaged user-app loading
- ELF32 user image loading with strict validation policy
- Boot-time selftest + QEMU smoke checks enforced by `make smoke`
- GitHub CI and release workflows

## Repository Layout

- `boot/`: early boot assembly
- `kernel/`: core kernel subsystems
- `user/`: sample user-space app sources/artifacts
- `tools/`: host-side packaging helpers
- `iso/`: ISO image layout and GRUB config
- `.github/workflows/`: CI and release automation

## Quick Start

### Prerequisites (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
  gcc-multilib \
  binutils \
  grub-pc-bin \
  xorriso \
  mtools \
  qemu-system-x86 \
  clang-format
```

### Build and Run

```bash
make clean && make -j"$(nproc)"
make run
```

### Validation Gates

```bash
make format-check
make check
make smoke
```

`make smoke` validates required selftest invariants from serial output, including process lifecycle and loader hardening markers.

## User App Packaging Pipeline

MarsOS supports building and embedding user app images.

```bash
make userapps
```

Additional conversion helper:

```bash
make apphex BIN=path/to/app.elf OUT=user/my_app.marshex
```

## Release Process

1. Update `VERSION` (for example `0.1.0`).
2. Ensure local gates pass (`make format-check && make smoke`).
3. Push a tag matching `v<VERSION>` (for example `v0.1.0`) or run the Release workflow manually.

The release workflow enforces:

- tag format and target resolution
- `VERSION` consistency with tag version
- successful build, multiboot check, and smoke run

Release assets include `mars.iso`, `kernel.bin`, `VERSION`, `RELEASE_CHECKLIST.md`, `.smoke.log`, and `SHA256SUMS`.

## Community Standards

- Code of Conduct: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)
- Contributing Guide: [CONTRIBUTING.md](CONTRIBUTING.md)
- Security Policy: [SECURITY.md](SECURITY.md)
- Pull Request Template: [.github/pull_request_template.md](.github/pull_request_template.md)
- Issue Templates: [.github/ISSUE_TEMPLATE](.github/ISSUE_TEMPLATE)
- Release Checklist: [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)

## License

Distributed under the MIT License. See [LICENSE](LICENSE).

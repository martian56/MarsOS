# Contributing to MarsOS

Thanks for contributing to MarsOS.

This project is a low-level OS codebase, so correctness and reproducibility
matter more than speed.

## Before You Start

1. Read [README.md](README.md) for build/test basics.
2. Read [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
3. Search existing issues and pull requests before opening new ones.

## Development Setup

Install dependencies (Ubuntu/Debian):

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

Build and validate:

```bash
make clean && make -j"$(nproc)"
make format-check
make check
make smoke
```

## Contribution Workflow

1. Fork the repository and create a topic branch from `main`.
2. Keep changes focused and atomic.
3. Include tests or smoke-signal updates for behavioral changes.
4. Ensure all validation commands pass locally.
5. Open a pull request using the project PR template.

## Coding Guidelines

- Follow existing style and architecture patterns.
- Keep kernel changes small and auditable.
- Avoid unrelated refactors in the same PR.
- Prefer explicit safety checks at user/kernel boundaries.
- Keep comments concise and focused on non-obvious logic.

Formatting:

- `make format-check` must pass.
- Use `make format` when needed.

## Commit Message Style

Use concise, scope-first summaries, for example:

- `process: tighten ELF header validation policy`
- `syscall: enforce user-page checks for input strings`
- `ci: add release workflow for tagged builds`

## Pull Request Expectations

A good PR includes:

- Problem statement and motivation
- Clear change summary
- Risk notes for low-level behavior changes
- Validation evidence (`format-check`, `check`, `smoke`)

## Security Contributions

For security-sensitive issues, do not open a public issue first.
Follow [SECURITY.md](SECURITY.md).

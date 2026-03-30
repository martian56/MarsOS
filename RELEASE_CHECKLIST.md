# Release Checklist

Version: match this release with VERSION and tag `v<version>`.

## Pre-release validation

- [ ] `make format-check` passes.
- [ ] `make clean && make -j"$(nproc)"` passes.
- [ ] `make check` passes.
- [ ] `make smoke` passes.

## Runtime sanity

- [ ] Boot selftest reports `selftest: PASS`.
- [ ] Smoke invariants include:
  - `ping_delta=0x00000006`
  - `pid_badimg=0xFFFFFFFF`
  - `long_write=0x00000000`
  - `stress_ok=0x00000001`
  - `proc_after=0x00000001`

## Artifacts

- [ ] `mars.iso` generated.
- [ ] `kernel.bin` generated.
- [ ] `SHA256SUMS` generated and includes both artifacts.

## Release metadata

- [ ] Tag format is `vX.Y.Z` (optionally with prerelease suffix).
- [ ] Tag version matches `VERSION`.
- [ ] Release notes generated.

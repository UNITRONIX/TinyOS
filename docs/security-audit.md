# TinyOS Security Audit Notes

Current audit date: 2026-05-10.

## Scope

- Kernel-side command parsing, VFS/RAMFS/blockfs access paths, package verifier and trust-store contracts.
- Host-side image and `.tapp` helper workflow.
- Local secret-pattern scan across source, docs, manifests and scripts, excluding generated `build/` artifacts.

## Findings

- [x] VFS accepted non-canonical paths before dispatching to RAMFS/blockfs. Fix: central `validate_path()` rejects relative paths, dot segments, repeated separators, trailing separators, control/shell metacharacters and overlong path segments before lookup or write.
- [x] Syscall user pointers were validated with a range check only, which accepted unmapped addresses, kernel pages and guard pages. Fix: `kernel/memory/user_access.cpp` walks the page tables and rejects anything that is not a present, user-accessible page with the required write rights. A ring-3 test process now attempts to pass a kernel pointer and the boot log records the rejection.
- [x] The frame allocator clamped managed memory to a fixed window (16 MiB, later a 256 MiB bitmap), silently discarding RAM on larger machines. Fix: the bitmap is sized from detected memory and carved out of the highest usable region, and the identity map covers the detected range. `make test-memory-scaling` fails if the pool stops tracking installed RAM.
- [x] The bootstrap task carried a fabricated resumable context frame, so the first switch back into it jumped into a returned stack frame. Fix: the bootstrap task has no frame until it is switched away from, and the switch marks the frame valid.
- [x] Trust-store counters treated planned anchors as available anchors. Fix: only `Trusted` and `DevelopmentOnly` anchors are active for app/image policy checks; planned and revoked anchors remain visible only as metadata.
- [x] Host app key generation overwrote existing key material. Fix: `keygen-app` now reuses complete valid key pairs and refuses partial key material.
- [x] Host app public-key trust receipts hashed PEM bytes. Fix: `trust-app` now records a canonical DER SHA-256 fingerprint and validates the public key before writing the trust manifest.
- [x] Host deployment could copy unsigned or plaintext image artifacts if invoked directly. Fix: `deploy-check` now requires encrypted `.age` transport artifacts and adjacent signature evidence unless an explicit override is set.
- [ ] Kernel still has no target-side cryptographic signature verification for `.tapp` payloads or images. Keep install gates closed for non-builtin packages until this exists.
- [~] Runtime paging now has a bootstrap policy self-test for kernel/module page flags, and user pointers are validated through a page-table walk instead of a range check. User/kernel isolation is still partial: ring 3 runs on the shared identity map, so a user mapping can still alias kernel memory until per-process address spaces land. Do not execute untrusted application payloads until that is complete.

## Secret Scan Result

No hardcoded credentials, private-key blocks, bearer tokens or API keys were found in non-generated project files. Generated development keys under `build/` are local test artifacts and should not be committed or treated as release trust anchors.
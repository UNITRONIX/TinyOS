# TinyOS Security Audit Notes

Current audit date: 2026-05-10.

## Scope

- Kernel-side command parsing, VFS/RAMFS/blockfs access paths, package verifier and trust-store contracts.
- Host-side image and `.tapp` helper workflow.
- Local secret-pattern scan across source, docs, manifests and scripts, excluding generated `build/` artifacts.

## Findings

- [x] VFS accepted non-canonical paths before dispatching to RAMFS/blockfs. Fix: central `validate_path()` rejects relative paths, dot segments, repeated separators, trailing separators, control/shell metacharacters and overlong path segments before lookup or write.
- [x] Trust-store counters treated planned anchors as available anchors. Fix: only `Trusted` and `DevelopmentOnly` anchors are active for app/image policy checks; planned and revoked anchors remain visible only as metadata.
- [x] Host app key generation overwrote existing key material. Fix: `keygen-app` now reuses complete valid key pairs and refuses partial key material.
- [x] Host app public-key trust receipts hashed PEM bytes. Fix: `trust-app` now records a canonical DER SHA-256 fingerprint and validates the public key before writing the trust manifest.
- [x] Host deployment could copy unsigned or plaintext image artifacts if invoked directly. Fix: `deploy-check` now requires encrypted `.age` transport artifacts and adjacent signature evidence unless an explicit override is set.
- [ ] Kernel still has no target-side cryptographic signature verification for `.tapp` payloads or images. Keep install gates closed for non-builtin packages until this exists.
- [ ] User/kernel isolation, runtime paging protection and permission enforcement are still scaffolds. Do not execute untrusted application payloads until these stages are complete.

## Secret Scan Result

No hardcoded credentials, private-key blocks, bearer tokens or API keys were found in non-generated project files. Generated development keys under `build/` are local test artifacts and should not be committed or treated as release trust anchors.
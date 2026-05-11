# TinyOS Security Roadmap

## Goal

This roadmap tracks a gradual security program for `TinyOS` with low overhead, strong auditability, and preparation for future kernel/user isolation.

## Security principles

1. Prefer reliability before advanced protection.
2. Keep mechanisms small, explicit and auditable.
3. Add security in layers that match kernel maturity.
4. Limit fault impact by design.
5. Prepare for future `userspace`, permissions and memory protection.

## High-priority security stages

### Stage 0 - diagnostics and stability
- unified kernel logger
- `panic()` and assertions
- serial diagnostics
- centralized fault reporting

### Stage 1 - kernel integrity
- safer memory helpers
- buffer range checks
- allocator misuse detection
- integrity checks for critical structures

### Stage 2 - memory safety groundwork
- frame allocator
- page allocator helpers
- guard regions for critical memory
- address-space preparation

### Stage 3 - controlled fault handling
- CPU exception handling
- panic policy
- watchdog and timeout scaffolds
- safer driver failure handling

### Stage 4 - kernel and user separation
- syscall boundary
- user/kernel transition contract
- safe argument validation
- protected memory boundaries

### Stage 5 - permissions and module trust
- identities and permission flags
- module metadata validation
- ABI compatibility checks
- controlled loading policy

### Stage 6 - UI and application safety
- app permission model
- restricted UI resource access
- validation of loaded assets
- resource limiting hooks

### Stage 7 - integrity and hardening
- checksum and module validation
- stack canaries
- W^X and NX where possible
- syscall filtering and resource limits

## Current TinyOS security status

- [x] unified logger and serial output
- [x] kernel warning/error counters exposed through `securityinfo`
- [x] categorized `WARN_ON` diagnostics for memory warnings
- [x] `panic()` and `ASSERT`
- [x] CPU exception diagnostics
- [x] frame allocator and kernel heap scaffold
- [x] syscall ABI scaffold
- [x] syscall argument validation scaffold
- [x] syscall boundary policy contract
- [x] syscall definition table contract
- [x] syscall filter policy scaffold
- [x] syscall resource limit policy scaffold
- [x] user transition scaffold
- [x] safer memory helpers and allocator hardening
- [x] runtime capability masks
- [x] application capability profile manifest
- [x] launch-policy dry checks
- [x] `.tapp` package manifest contract, host validation, detached signing, trust-store contract and install-gate verifier
- [x] secure image/provisioning manifest
- [x] boot module metadata validation scaffold
- [x] ELF metadata validation scaffold
- [ ] runtime paging protection
- [ ] userspace isolation
- [ ] permission model
- [~] trusted module policy

## Near-term security priorities

1. add safe memory helper functions
2. add more `WARN_ON` categories for drivers and security policy checks
3. add allocator misuse checks
4. keep paging preparation out of boot path until validated
5. validate syscall boundary inputs before real userspace arrives
6. connect launch-policy checks to the eventual process launcher
7. connect signed image verification to the eventual provisioning agent
8. connect `.tapp` detached signatures and payload hashes to real target-side cryptographic verification
9. move development trust anchors into persistent, replaceable target storage

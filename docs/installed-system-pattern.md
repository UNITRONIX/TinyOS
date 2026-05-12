# TinyOS Installed System Pattern

This document defines the target documentation pattern for TinyOS as an installable operating system. It is a product contract, not the current implementation state.

## Documentation Status Labels

Every installation-facing document should use these labels:

- `Current` - implemented and covered by boot or host tests.
- `Ready contract` - documented interface or profile shape exists, but the runtime feature is not complete.
- `Planned` - accepted direction with no stable contract yet.
- `Experimental` - available for development builds only and allowed to change.

This keeps the public docs honest while still describing the larger OS shape.

## Target System Shape

TinyOS should eventually be distributed as bootable media for virtual machines and small devices:

- `x86` / `i686` as the reference target;
- `x86_64` after the 32-bit reference path is stable;
- `aarch64` through QEMU `virt` first, then selected ARM boards;
- ISO media for virtual machines and installer boot;
- disk images for installed systems, USB drives and microSD cards;
- a terminal-first installer that can run without a GUI.

The first public install path should stay simple: boot ISO in QEMU, install TinyOS to a virtual disk, reboot from that disk, then use provisioning tools from the installed system or host.

## Installer Flow

The installer should be a terminal application with a guided workflow:

1. Select language and keyboard profile when those subsystems exist.
2. Confirm target architecture and platform profile.
3. Select install target disk and partition policy.
4. Set device name.
5. Configure network mode.
6. Create the primary user account.
7. Configure administrator access.
8. Select provisioning defaults such as encryption, signing and remote access.
9. Write the system image to disk.
10. Write install receipt, rollback metadata and first-boot profile.
11. Reboot into the installed system.

The installer should support a non-interactive profile later, but the interactive terminal flow is the baseline.

## Required Installer Inputs

The first installer profile should collect:

- `device.name` - human-readable device name and stable local hostname candidate;
- `network.mode` - `disabled`, `dhcp`, `static` or `provisioned`;
- `network.interface` - selected network device when networking exists;
- `network.address`, `network.gateway`, `network.dns` - static network fields;
- `user.name` - primary account name;
- `user.display_name` - optional display name;
- `credential.bootstrap` - temporary install secret entered by the installer user;
- `admin.mode` - `disabled`, `same-bootstrap-secret`, `separate-secret` or `key-only`;
- `provisioning.encryption` - default `required` for image creation and remote deploy;
- `provisioning.remote_access` - default `disabled`.

Example profile: `examples/install.profile`.

Host validation is available through:

```sh
scripts/tinyos-image.sh install-plan
scripts/tinyos-image.sh check-install-profile examples/install.profile
make install-profile-check
```

## Credential Policy

A shared user/admin password is acceptable only as an early development convenience or an explicit single-user device profile. The safer contract is:

- the installer may accept one bootstrap secret for a development profile;
- TinyOS should store separate salted password hashes for the user and administrator identities, even when both are derived from the same bootstrap secret;
- release profiles should prefer a separate administrator secret or key-based administrator unlock;
- plaintext passwords must never be written to profiles, receipts, logs or images;
- first boot may require changing the bootstrap secret when the install profile is marked `development`.

This gives the convenient behavior for small VM tests without making shared root credentials the long-term default.

## Installed System Layout

The installed disk should eventually contain:

```text
/system/
  kernel
  boot/
  profiles/
  trust/
  provisioning/
/users/
  <user>/
/apps/
/devices/
/logs/
/receipts/
```

The exact filesystem is not chosen yet. RAMFS remains the current development filesystem; persistent storage needs a block driver and a simple filesystem before a real disk install can exist.

## First Boot

After installation, TinyOS should boot from disk into a terminal session that shows:

- device name;
- architecture and platform profile;
- network status;
- signed image state;
- active user identity;
- provisioning readiness;
- links to diagnostics such as `requirements`, `meminfo`, `storageinfo`, `securityinfo` and `provisioninfo`.

Provisioning tools should then allow the developer to create project workspaces, configure device variants, inspect resource budgets and build custom boot media.

## Provisioning Relationship

Provisioning is not the installer itself. The split should be:

- installer: puts TinyOS onto a disk and creates first local identity/configuration;
- provisioning workbench: prepares project-specific images, variants, keys, resources and deploy artifacts;
- target provision agent: verifies signed images and activates them with rollback once persistent storage is ready.

The installed system can run provisioning tools locally, but host-side provisioning should remain supported for development machines.

## Multi-Architecture Documentation Pattern

Each architecture document should use the same table:

```text
Target: i686-pc-qemu
Status: Current
Boot media: GRUB Multiboot ISO
Install media: Planned
Disk boot: Planned
Emulator: qemu-system-i386
Minimum RAM: 32 MiB current baseline
Installer support: Planned terminal installer
Networking: Planned
Storage: RAM block scaffold current, persistent disk planned
```

Future `x86_64` and `aarch64` documents should not claim installer support until boot, storage and smoke tests exist for that target.

## Effort Estimate

This scheme is viable, but it is a large feature set. A realistic implementation requires several foundations:

- medium effort: documentation contract, installer profile format and host-side image layout;
- medium effort: terminal installer UI once the TUI controls are stable;
- high effort: persistent block storage and filesystem support;
- high effort: users, password hashing, admin policy and permission checks;
- high effort: networking and network configuration;
- high effort: x86_64 and aarch64 boot paths with repeatable tests;
- high effort: target-side image verification, activation and rollback.

The correct path is incremental: document the pattern now, add host-side profiles next, then build disk install support only after storage, users and security policies are solid.

## Milestones

1. Document the install profile and architecture support matrix.
2. Add host validation for install profiles.
3. Add a terminal installer mock that writes a receipt to RAMFS.
4. Add persistent disk image creation in host tools.
5. Add a QEMU disk boot test for the reference `i686` target.
6. Add user/admin credential storage with password hashing.
7. Add network configuration after a network device model exists.
8. Promote x86_64 and aarch64 only after boot smoke tests are repeatable.
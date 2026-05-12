# TinyOS User Management Tools

This list defines the user-facing tools needed to manage TinyOS as it grows from a small kernel shell into a self-hosted operating system. The current implementation exposes the list in the shell through `tools`, `toolinfo` and `tool <command>`, and in RAMFS as `/system/tools.txt`.

## Ready tools

- Shell: `help`, `helpui`, `helplist`, `tools`, `toolinfo`, `tool`, `aliases`, `clear`
- Files: `fileui`, `pwd`, `cd`, `files`, `fsmap`, `show`, `describe`, `mkdir`, `touch`, `chmod`, `write`, `copy`, `move`, `remove`, `fstest`, `ramfsinfo`, `vfsinfo`
- Storage and devices: `blockinfo`, `storageinfo`, `devices`, `device`, `fbinfo`
- Memory: `meminfo`, `frameinfo`, `heapinfo`, `heaptest`, `paginginfo`, `addrspaceinfo`
- Runtime and apps: `runtimeinfo`, `appinfo`, `launchinfo`, `launchcheck`, `tappinfo`, `tapps`, `tapp`, `tappcheck`, `tappverify`, `imageinfo`, `provisioninfo`, `deployinfo`, `installinfo`, `sysinfo`, `userinfo`, `elfinfo`, `modulesinfo`
- Security: `securityinfo`, `integritycheck`, `requirements`, `trustinfo`, `trust`
- UI and input: `renderinfo`, `terminalinfo`, `widgetinfo`, `uieventinfo`, `inputinfo`, `keyboardinfo`
- Scheduling and time: `schedinfo`, `taskinfo`, `contextinfo`, `timerinfo`, `uptime`
- Power and debug: `reboot`, `int3`, `panic`

## Planned tools

- Storage: `mount`
- Processes and services: `ps`, `kill`, `service`
- Security identities and policy: `useradd`
- Development and distribution: `package`, `tappinstall`, `tappremove`, `imagebuild`, `imagesign`, `imageencrypt`, `keygen`, `provisionui`, `provisioninit`, `provisionconfig`, `provisionvariant`, `provisionapi`, `provisionresources`, `remoteaccess`, `terminaltheme`, `videomode`, `deploy`, `provision`, `rollback`
- Networking: `netinfo`

## Future installer tools

- `install` should start the terminal installer from boot media.
- `installcheck` should validate the install profile without writing disks.
- `hostname` should set the installed device name once persistent configuration exists.
- `netconfig` should manage network mode, address, gateway and DNS once networking exists.
- `passwd` should rotate user or administrator credentials once password hashing exists.
- `whoami` and `id` should expose active identity after userspace identities exist.

## Host package tools

- `scripts/tinyos-image.sh install-plan` prints the planned installed-system workflow.
- `scripts/tinyos-image.sh check-install-profile` validates install-profile safety rules without writing disks.
- `scripts/tinyos-image.sh provision-plan` prints the planned project provisioning workflow.
- `scripts/tinyos-image.sh keygen-app` creates a developer app signing key pair.
- `scripts/tinyos-image.sh trust-app` records an app public key's DER fingerprint for the kernel trust-store contract.
- `scripts/tinyos-image.sh sign-app` writes a detached `.tapp.sig` signature and receipt.
- `scripts/tinyos-image.sh verify-app` checks `.tapp` policy fields and can verify a detached signature with a public key.

## Safety policy

Tools that write state or can stop the machine are marked in the kernel manifest. Future interactive shells should require explicit confirmation for high-risk commands before they reach the kernel operation layer.

Installer credential prompts must not write plaintext passwords to logs, profiles, receipts or images. A shared bootstrap secret is acceptable for development profiles only; release profiles should prefer a separate administrator secret or key-based unlock.
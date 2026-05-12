# TinyOS User Management Tools

This list defines the user-facing tools needed to manage TinyOS as it grows from a small kernel shell into a self-hosted operating system. The current implementation exposes the list in the shell through `tools`, `toolinfo` and `tool <command>`, and in RAMFS as `/system/tools.txt`.

## Ready tools

- Shell: `help`, `helpui`, `helpsearch`, `helplist`, `sysinfo`, `status`, `syscheck`, `tools`, `toolinfo`, `tool`, `aliases`, `clear`
- Files: `filemgr`, `textedit`, `fileui`, `pwd`, `cd`, `files`, `fsmap`, `show`, `describe`, `pathcheck`, `mkdir`, `touch`, `chmod`, `write`, `edit`, `copy`, `move`, `remove`, `fstest`, `ramfsinfo`, `vfsinfo`
- Storage and devices: `blockinfo`, `storageinfo`, `devices`, `device`, `fbinfo`
- Memory: `meminfo`, `frameinfo`, `heapinfo`, `heaptest`, `paginginfo`, `addrspaceinfo`
- Runtime and apps: `runtimeinfo`, `syscallinfo`, `appinfo`, `launchinfo`, `launchcheck`, `tappinfo`, `tapps`, `tapp`, `tappcheck`, `tappverify`, `imageinfo`, `provisioninfo`, `deployinfo`, `installinfo`, `installcheck`, `install`, `userinfo`, `elfinfo`, `modulesinfo`
- Security: `riskinfo`, `profileinfo`, `profilecheck`, `securityinfo`, `integritycheck`, `requirements`, `trustinfo`, `trust`
- UI and input: `renderinfo`, `terminalinfo`, `widgetinfo`, `uieventinfo`, `inputinfo`, `keyboardinfo`
- Scheduling and time: `schedinfo`, `taskinfo`, `contextinfo`, `timerinfo`, `uptime`
- Power and debug: `reboot`, `int3`, `panic`

## Planned tools

- Storage: `mount`
- Processes and services: `ps`, `kill`, `service`
- Security identities and policy: `useradd`
- Development and distribution: `package`, `tappinstall`, `tappremove`, `imagebuild`, `imagesign`, `imageencrypt`, `keygen`, `provisionui`, `provisioninit`, `provisionconfig`, `provisionvariant`, `provisionapi`, `provisionresources`, `remoteaccess`, `terminaltheme`, `videomode`, `deploy`, `provision`, `rollback`
- Networking: `netinfo`

## Installer tools

- `installcheck` validates the current installer mock preflight without writing disks.
- `install` writes a mock install receipt to `/receipts/install.receipt` in RAMFS and performs no disk writes.
- `profileinfo` shows the active RAMFS system profile from `/system/profile.txt`.
- `profilecheck` validates the active system profile policy before persistent install profiles exist.
- `hostname` should set the installed device name once persistent configuration exists.
- `netconfig` should manage network mode, address, gateway and DNS once networking exists.
- `passwd` should rotate user or administrator credentials once password hashing exists.
- `whoami` and `id` should expose active identity after userspace identities exist.

## Terminal diagnostics

- `status` prints a compact system dashboard for version, architecture, ticks, memory, VFS, tools, package and install-receipt state.
- `sysinfo` prints the TinyOS system information page: owner, author, license, version, architecture, boot profile, terminal tools and practical RAM probe range.
- `syscheck` runs non-destructive health checks across architecture, platform, memory, VFS, app/package, syscall, provisioning, system profile and terminal contracts.
- `riskinfo` lists management commands that write state or are marked high risk in the kernel tool manifest.
- `profileinfo` and `profilecheck` expose and validate the current system identity/security profile.
- `pathcheck <path>` resolves a shell path and reports validity, metadata and permissions.
- `helpsearch <text>` searches command names, usage and summaries without opening the interactive help UI.

## Terminal file tools

- `filemgr` is the two-pane terminal file manager. It can switch active panes, open directories, view files, create files/directories, open selected files in `textedit`, remove paths, copy files to the other pane and move files to the other pane.
- `textedit <path>` is the interactive RAMFS text editor. It can load an existing file, create a missing runtime file after confirmation, replace the buffer, append a line, clear the buffer, save, reload and show file metadata.
- `fileui` is the lighter single-pane RAMFS file browser. It can open directories, view files, create files/directories, edit selected writable files through `textedit`, remove paths, copy files and move paths.
- `edit <path> <text>` and `write <path> <text>` are the current low-memory text editing path for writable RAMFS files.

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
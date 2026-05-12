# TinyOS Provisioning Workbench

TinyOS provisioning should grow from an image helper into a developer workbench for preparing a complete system profile for one project, one product line or a family of IoT devices.

The workbench is a host-first feature. Kernel support should expose manifests, diagnostics and target-side verification, while host tools should create folders, keys, variants and deployable images.

Installed-system expectations are tracked in `docs/installed-system-pattern.md`. The installer creates the first bootable disk system and local identity; the provisioning workbench prepares project-specific media, variants and deployable artifacts after that baseline exists.

## Goals

- Create an isolated project workspace instead of mixing project files with the repository root.
- Make signing and encryption defaults explicit before images are built.
- Keep remote access disabled until a project chooses a transport and key policy.
- Define multiple device variants from one project profile.
- Expose the API and capability surface available to project applications.
- Estimate RAM, ROM/image size and package footprint before deploying to a target.
- Provide a colorful terminal UI for provisioning status, resource budgets and diagnostics.
- Keep VGA text mode as the safe fallback while preparing framebuffer and higher-resolution consoles.

## Developer Flow

1. Create a project workspace with a profile, app folder, device variants, key slots and deploy receipts.
2. Configure project defaults such as encryption, signing, remote access, terminal theme and API exposure.
3. Add one or more device variants with CPU, platform, memory, display, storage and feature budgets.
4. Validate app manifests and `.tapp` packages against the project capability policy.
5. Run a resource budget check that reports RAM, ROM/image size, package size and variant fit.
6. Build, sign and encrypt the image using the project defaults.
7. Deploy through SSH/SCP/SFTP only after `deploy-check` accepts signature and encryption evidence.
8. Later, verify the image on the TinyOS target and activate it with rollback metadata.

## Workspace Layout

The first host implementation should create a folder similar to this:

```text
project.tinyos/
  project.profile
  apps/
  devices/
    qemu-i386-min.profile
    qemu-i386-dev.profile
  keys/
    README.txt
  deploy/
  receipts/
  cache/
```

The folder is intentionally plain text and easy to audit. Private keys may be referenced from outside the workspace when the project requires stricter key handling.

## Profile Contract

`project.profile` should extend the current `examples/system.profile` shape with provisioning-oriented fields:

```text
profile.name=developer-secure-image
provision.workspace=project.tinyos
provision.isolation=project-folder
provision.api=manifest-only
provision.terminal=color-tui
devices.variants=qemu-i386-min,qemu-i386-dev
resources.ram_budget_mib=32
resources.rom_budget_mib=16
security.signing=required
security.encryption=required
remote.access=disabled
remote.transport=ssh-scp-now,tinylink-later
```

Encryption should default to `required` for provisioning workflows that can build or deploy images. A developer may still run local unsigned experiments, but remote deployment must remain blocked unless explicit override variables are set.

## Device Variants

Each variant should describe the smallest target it is allowed to run on:

```text
variant.name=qemu-i386-min
target.arch=i686
target.platform=pc-bios-qemu
memory.minimum_mib=24
memory.recommended_mib=64
display.mode=vga-text-80x25
storage.model=iso-plus-ram-block
features=terminal,ramfs,tapp-runtime
```

The project can then compare app packages and system features against each variant before building an image.

## Resource Diagnostics

The first resource budget command should combine host-side and kernel-side facts:

- kernel size and ISO size from the build output;
- `.tapp` package sizes and manifest capabilities;
- requested RAM budget from the selected device variant;
- current kernel memory diagnostics from `meminfo`, `frameinfo` and `heapinfo`;
- current minimum-runtime smoke result from `make test-minimal`;
- optional probe results from `make test-minimal-probe`.

The current 32 MiB baseline should stay documented until boot tests prove a lower value. Lower values such as 24 MiB and 16 MiB should be treated as probes first.

## Remote Access

Remote folder access should be project-scoped and opt-in:

- `remote.access=disabled` by default;
- `remote.access=ssh` enables host-side SSH/SCP/SFTP transport;
- remote writes must target the provisioning workspace or deploy folder, not arbitrary host paths;
- private keys stay outside the image;
- target-side TinyOS remote access comes later, after networking, users and permissions are stable.

## Terminal Experience

The provisioning terminal should use the existing text-grid renderer first:

- colored panels for project status, signing, encryption and deployment readiness;
- progress and budget bars for RAM, ROM and image size;
- device-variant comparison tables;
- warnings for plaintext deploys, missing signatures and disabled rollback;
- a later framebuffer terminal path for higher resolutions when the hardware envelope allows it.

## Implementation Slices

1. Add the workbench profile contract and planned tool names to the kernel manifests.
2. Add host `provision-plan` output so developers can see the intended workflow.
3. Add `provisioninit` to create the isolated folder and initial `project.profile`.
4. Add `provisionconfig` and `provisionvariant` to edit project defaults and target variants.
5. Add `provisionresources` to report RAM, ROM/image and package budgets.
6. Add a color TUI command after terminal color helpers are stable.
7. Add target-side verification and rollback activation only after storage and permissions mature.

## Safety Rules

- Sign before deploy.
- Encrypt remote deployment artifacts by default.
- Keep remote access disabled until configured.
- Require explicit confirmation for high-risk commands that write keys, deploy images or change remote access.
- Keep every new provisioning slice bootable and visible through diagnostics.
- Treat installer credentials as local system setup data, not provisioning profile data; profiles and receipts must never contain plaintext passwords.
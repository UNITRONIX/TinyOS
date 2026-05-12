# TinyOS Secure Image Provisioning

TinyOS should support a developer workflow where an application, system profile and file set can become a signed, optionally encrypted image that is deployed through a remote transport.

Project-oriented provisioning is tracked in `docs/provisioning-workbench.md`. That workbench extends this image pipeline with isolated project folders, device variants, resource budgets, project API exposure and a future color terminal UI.

## Current status

- Kernel manifest: `imageinfo`, `provisioninfo`, `deployinfo`
- RAMFS metadata: `/system/provisioning.txt`
- Host helper: `scripts/tinyos-image.sh`
- Kernel trust store: `trustinfo`, `trust <anchor>`, `/system/trust.txt`
- Example profile: `examples/system.profile`
- Example app manifest: `examples/app.manifest`
- Example app package: `examples/example-system-tool.tapp`
- Workbench plan: `scripts/tinyos-image.sh provision-plan`

## Pipeline

1. Prepare an isolated provisioning workspace and project profile.
2. Configure signing, encryption, device variants, project API and remote-access defaults.
3. Prepare an app bundle with a `.tapp` manifest envelope and capability list.
4. Generate or select an app signing key and record the public key's DER fingerprint with `trust-app`.
5. Sign the `.tapp` package with a detached app signature and receipt.
6. Prepare a `system.profile` with target architecture, apps, files and security policy.
7. Build a bootable TinyOS image.
8. Generate an image manifest with hashes and deployment metadata.
9. Sign the image or manifest with a developer key.
10. Encrypt the image for a deployment recipient. Provisioning profiles should default to required encryption.
11. Deploy through SSH/SCP now, and through a future TinyLink provision channel later.
12. Verify image policy and signature on the target before activation.
13. Keep rollback metadata so a target can return to the previous working slot.

## Host commands

```sh
scripts/tinyos-image.sh plan
scripts/tinyos-image.sh provision-plan
scripts/tinyos-image.sh check-profile examples/system.profile
scripts/tinyos-image.sh check-app examples/app.manifest
scripts/tinyos-image.sh pack-app examples/app.manifest build/apps/example-system-tool.tapp
scripts/tinyos-image.sh keygen-app build/keys/tapp-dev-private.pem build/keys/tapp-dev-public.pem
scripts/tinyos-image.sh trust-app build/keys/tapp-dev-public.pem
scripts/tinyos-image.sh sign-app build/apps/example-system-tool.tapp build/keys/tapp-dev-private.pem
scripts/tinyos-image.sh verify-app build/apps/example-system-tool.tapp build/keys/tapp-dev-public.pem
scripts/tinyos-image.sh build build/images/tinyos-dev.iso
scripts/tinyos-image.sh manifest build/images/tinyos-dev.iso
scripts/tinyos-image.sh keygen build/images/device.agekey
scripts/tinyos-image.sh encrypt build/images/tinyos-dev.iso build/images/device.agekey.pub
scripts/tinyos-image.sh deploy-check build/images/tinyos-dev.iso.age
scripts/tinyos-image.sh deploy build/images/tinyos-dev.iso.age user@host:/tmp/tinyos-dev.iso.age
```

## Security rules

- Sign before deploy.
- Validate `.tapp` manifests and install-gate policy fields before image build.
- Record canonical public-key fingerprints before treating app signatures as trusted.
- Keep `.tapp` signatures detached so package contents are immutable after signing.
- Keep private signing keys outside the image.
- Use per-target or per-profile encryption recipients.
- Treat encryption as required for remote provisioning profiles unless a local-only development profile explicitly opts out.
- Run `deploy-check` before remote transport; plaintext or unsigned deploys require explicit override.
- Keep remote project-folder access disabled until the profile selects SSH/SFTP or a later TinyLink transport.
- Treat SSH/SCP as host-side transport only until TinyOS has its own networking and target verifier.
- Do not activate a remotely provided image until target-side verification and rollback are implemented.
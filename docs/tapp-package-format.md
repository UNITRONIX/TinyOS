# TinyOS TAPP Package Format

`.tapp` is the TinyOS application package extension. The current implementation is a low-resource package contract: a small key-value manifest envelope that can be validated by host tools and mirrored by the kernel registry before real dynamic loading arrives.

## Goals

- Keep packages readable, inspectable and easy to generate on small hosts.
- Bind every package to a runtime, entry path and least-privilege capability list.
- Require a signature policy before remote deployment is treated as trusted.
- Leave room for later archive payloads, hashes, signatures and encrypted assets.

## Required keys

```ini
tinyos.tapp.version=0
tinyos.tapp.kind=manifest-envelope
tinyos.tapp.source_sha256=<sha256>
tinyos.tapp.signature_policy=required
tinyos.tapp.signature_state=unsigned
tinyos.tapp.payload_policy=hash-required
tinyos.tapp.payload_state=external
app.name=example-system-tool
app.version=0
app.runtime=native-cpp-elf32
app.profile=example-system-tool
app.entry=/apps/example-system-tool.elf
app.capabilities=console,file-read,clock
app.trust=developer-signed
```

The kernel currently exposes the package registry, trust store and install gate through `tappinfo`, `tapps`, `tapp <name>`, `tappcheck <name>`, `tappverify <name>`, `trustinfo` and `trust <anchor>`. The host helper validates packages, records app public-key trust metadata and creates package envelopes:

```sh
scripts/tinyos-image.sh check-app examples/app.manifest
scripts/tinyos-image.sh pack-app examples/app.manifest build/apps/example-system-tool.tapp
scripts/tinyos-image.sh keygen-app build/keys/tapp-dev-private.pem build/keys/tapp-dev-public.pem
scripts/tinyos-image.sh trust-app build/keys/tapp-dev-public.pem
scripts/tinyos-image.sh sign-app build/apps/example-system-tool.tapp build/keys/tapp-dev-private.pem
scripts/tinyos-image.sh check-app build/apps/example-system-tool.tapp
scripts/tinyos-image.sh verify-app build/apps/example-system-tool.tapp build/keys/tapp-dev-public.pem
```

`trust-app` writes a small trust manifest with the public key's canonical DER SHA-256 fingerprint and the matching kernel anchor name. `sign-app` writes a detached signature next to the package and a `.sig.receipt` file with package and signature hashes. The package file is not modified after signing, so the signature stays stable.

## Current status

- `system-shell.tapp` is launch-ready because it maps to the built-in kernel shell profile.
- `example-system-tool.tapp` has a matching planned app profile, but the kernel install gate stays closed until target-side signature and payload verification are implemented.
- `tinyos-dev-app-signing` is a development-only trust anchor used to model app package signing before persistent target storage exists.
- GUI, web-style and self-hosted toolchain packages are planned contracts.

## Security model

TinyOS treats `.tapp` packages as capability-gated application contracts. The current `tappverify` command reports the kernel-side install gate verdict and trust-store readiness. Host-side `verify-app` can already verify a detached RSA-SHA256 signature when a public key is supplied. A future target verifier must reject a package when its runtime is disabled, its requested capabilities exceed the app profile, its signing key is not present in the trust store, its signature is missing or untrusted, or its payload hash does not match the envelope.
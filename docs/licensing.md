# TinyOS Licensing Policy

TinyOS source code and project documentation are intended to be distributed under the Apache License 2.0 unless a file explicitly states otherwise.

## Project rules

- Keep TinyOS kernel, drivers, shell tools and documentation original or based only on Apache-2.0-compatible material.
- Do not copy code, comments, manuals or implementation text from Linux, GNU coreutils, BusyBox or other GPL/copyleft projects into the TinyOS source tree.
- Familiar command names may exist as compatibility aliases, but TinyOS-native names should be the primary user-facing interface.
- Prefer permissive dependencies such as Apache-2.0, MIT, BSD, ISC or zlib when dependencies become necessary.
- Record third-party code, assets and generated artifacts before distribution.

## Bootloader note

The current ISO build flow uses GRUB through `grub-mkrescue`/`grub2-mkrescue`. GRUB is an external bootloader with its own license. The TinyOS source tree can remain Apache-2.0, but distributing ISO images that bundle GRUB may require satisfying GRUB's license obligations separately.

If a fully permissive boot artifact is required later, evaluate a boot path whose bundled components are compatible with that distribution goal.

## Naming note

Directory and command names are not license inheritance by themselves. Still, TinyOS should use its own primary names for clarity and identity, while optional aliases can help users who already know Unix-style shells.

## Commercial-readiness note

Apache-2.0 allows commercial use and private redistribution, but a later commercial edition or dual-license path should keep ownership and contribution records clean. Before accepting outside contributions, decide whether the project needs a Developer Certificate of Origin, contributor agreement, or another lightweight provenance process.

Avoid importing GPL/copyleft code into kernel, drivers, shell, package tools, documentation examples, UI assets, or test fixtures unless the project intentionally accepts the distribution obligations for that artifact. When implementing familiar terminal behavior, write original code from public specifications and project design notes rather than copying another shell or operating system implementation.

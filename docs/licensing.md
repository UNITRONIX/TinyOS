# TinyOS Licensing Policy

TinyOS source code and project documentation are distributed under the **GNU General Public License version 3 (GPL-3.0-or-later)** unless a file explicitly states otherwise.

## Project ownership

- Project owner: UNITRONIX.
- Author: Krzysztof Nienartowicz.
- Copyright (C) UNITRONIX (Krzysztof Nienartowicz).
- Repository notice: `NOTICE` records the current project ownership and author metadata for source and documentation distribution.

## Project rules

- TinyOS kernel, drivers, shell tools and documentation are licensed under GPL-3.0-or-later.
- When incorporating third-party code, prefer GPL-compatible licenses (GPL, LGPL, MIT, BSD, ISC, zlib, Apache-2.0).
- Do not copy code, comments, manuals or implementation text verbatim from other operating systems without verifying license compatibility.
- Familiar command names may exist as compatibility aliases, but TinyOS-native names should be the primary user-facing interface.
- Record third-party code, assets and generated artifacts before distribution.

## Copyleft obligations

Under GPL-3.0, anyone who distributes TinyOS (or a modified version) must:

1. Provide the complete corresponding source code.
2. Include a copy of the GPL-3.0 license.
3. Preserve copyright and attribution notices.
4. License derivative works under GPL-3.0 (or later, at recipient's option).

See `LICENSE` for the full license text.

## Bootloader note

The current ISO build flow uses GRUB through `grub-mkrescue`/`grub2-mkrescue`. GRUB is an external bootloader licensed under GPL-3.0. The TinyOS source tree and distributed ISO images that bundle GRUB are compatible under GPL. When distributing ISO images, include source availability information for both TinyOS and bundled GRUB components as required by GPL.

## Naming note

Directory and command names are not license inheritance by themselves. Still, TinyOS should use its own primary names for clarity and identity, while optional aliases can help users who already know Unix-style shells.

## Contributions

Contributions to TinyOS are accepted under GPL-3.0-or-later. Before accepting outside contributions, consider whether the project needs a Developer Certificate of Origin, contributor agreement, or another lightweight provenance process.

When implementing familiar terminal behavior, write original code from public specifications and project design notes rather than copying another shell or operating system implementation.

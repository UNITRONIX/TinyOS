# GFX Terminal — przewodnik użytkownika

Framebufferowy terminal TinyOS inspirowany GitHub Copilot CLI.

## Wymagania

- QEMU z linear framebuffer: `-vga std -m 64M`
- Pełny build (nie `terminal-only-iso`)

## Uruchomienie

```bash
make run-gfxterm
```

Po boot w shellu VGA:

```
gfxterm
```

Autostart po boot (gdy FB dostępny):

```bash
make run-gfxterm-autostart
# lub: make GFX_TERM_AUTOSTART=1 iso && make run-gfxterm
```

## Skróty klawiszowe

| Klawisz | Akcja |
|---------|--------|
| Enter | Wykonaj polecenie |
| ↑ / ↓ | Historia poleceń |
| ← / → | Przesuń kursor |
| Home / End | Początek / koniec linii |
| Delete | Usuń znak pod kursorem |
| Backspace | Usuń znak przed kursorem |
| Tab | Uzupełnij polecenie |
| Shift+Tab | Przełącz tryb pickera (@ / /) |
| PgUp / PgDn | Przewiń scrollback |
| @ | Otwórz picker plików |
| / | Otwórz paletę poleceń |
| Q / Esc | Wyjście do VGA shell |

## Polecenia

W `gfxterm` działają polecenia kernel shell (`help`, `files`, `ls`, `pwd`, `status`, …). Wynik trafia do scrollback na ekranie.

| Polecenie | Opis |
|-----------|------|
| `gfxterm` | Wejście w tryb GFX (z VGA shell) |
| `gfxterminfo` | Diagnostyka sesji GFX |
| `terminaltheme` | Pokaż aktywny motyw |
| `terminaltheme copilot` | Motyw Copilot-dark |
| `terminaltheme dracula` | Motyw Dracula |
| `terminaltheme solarized` | Motyw Solarized |
| `videomode` | Pokaż tryb konsoli |
| `videomode gfx` | Uruchom GFX terminal |
| `clear` | Wyczyść scrollback (przez shell output) |

## Konfiguracja

Plik `/system/tinyos.conf` (RAMFS, edytowalny):

```
theme=copilot
videomode=auto
serial.mirror=0
```

## Test CI

```bash
make test-gfxterm-boot
```

## Architektura

Szczegóły faz implementacji: [gfx-terminal-roadmap.md](gfx-terminal-roadmap.md)

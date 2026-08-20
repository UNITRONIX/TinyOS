# Plan wdrożenia dojrzałości TinyOS

Dokument opisuje plan wdrożenia nowych funkcji TinyOS w oparciu o **obecny kod źródłowy** (v0.1.0), tak aby osiągnąć dojrzałość systemu pod kątem **funkcjonalności**, **bezpieczeństwa** i **użyteczności**.

Powiązane dokumenty:

- `docs/pending-implementation-roadmap.md` — szczegółowy backlog (~202 pozycje)
- `docs/implementation-roadmap.md` — checklista faz implementacji
- `docs/security-roadmap.md` — program bezpieczeństwa
- `docs/os-roadmap.md` — kierunek produktowy
- `docs/installed-system-pattern.md` — wzorzec zainstalowanego systemu

## 1. Obecny stan systemu

### Co działa dziś

TinyOS v0.1.0+ to bootowalny kernel `i686` z następującymi elementami produkcyjnymi lub bliskimi produkcji:

| Obszar | Stan | Kluczowe pliki |
|--------|------|----------------|
| Boot ISO (GRUB/Multiboot + Multiboot2) | Działa | `boot/multiboot.asm`, `build/grub/grub.cfg` |
| Hybrid boot disk / USB path | Działa (xorriso) | `scripts/tinyos-boot-disk.sh` |
| VGA terminal + shell | Działa | `shell/shell.cpp`, `drivers/vga.cpp` |
| PS/2 klawiatura + PIT | Działa (IRQ) | `drivers/keyboard.cpp`, `drivers/pit.cpp` |
| USB HID UHCI probe | Działa (detekcja) | `drivers/usb_hid.cpp` |
| RAMFS + VFS | Działa (RAM) | `kernel/vfs/ramfs.cpp`, `kernel/vfs/vfs.cpp` |
| ATA PIO + FAT16 | Działa (gdy blank ATA) | `drivers/ata.cpp`, `kernel/vfs/fatfs.cpp` |
| VirtIO block | Działa (QEMU) | `drivers/virtio_blk.cpp` |
| VirtIO-net contract | Detekcja | `drivers/virtio_net.cpp` |
| Paging (bootstrap) | Działa | `kernel/memory/paging.cpp` |
| GDT/TSS + ring-3 init | Działa | `arch/i686/gdt.cpp`, `kernel/user/transition.cpp` |
| Scheduler + IRQ preemption | Działa | `kernel/sched/scheduler.cpp` |
| Syscalls write/read/open/close/exit/yield/sleep | Działa | `kernel/syscall/syscall.cpp` |
| Accounts (root/user hashes) | Działa | `kernel/security/accounts.cpp` |
| Instalator | RAMFS + FAT marker | `shell/shell.cpp` `install` |

### Główne luki blokujące pełną dojrzałość bare-metal

1. **AHCI/NVMe i pełny USB HID boot-protocol** — UHCI jest tylko wykrywany; brak xHCI i masowej pamięci USB.
2. **Pełny stack sieciowy** — VirtIO-net bez TX/RX datapath i TCP/IP.
3. **Per-process address spaces / NX** — ring-3 działa na shared identity map.
4. **UEFI native path** — Multiboot2 header jest, ale brak dedykowanego EFI stub / Limine CI.
5. **Userspace poza embedded init** — shell nadal w ring 0; brak pełnego userland toolchain w obrazie.

---

## 2. Definicja dojrzałości

System uznajemy za **dojrzały funkcjonalnie**, gdy:

- kernel wykonuje wiele zadań z preempcją;
- dane przetrwają restart (block device + writable FS);
- procesy userspace uruchamiają się z ELF i komunikują przez syscalls;
- system można zainstalować na dysk i uruchomić bez ISO;
- podstawowe narzędzia procesów (`ps`, `kill`) i sieci (`netinfo`) działają.

System uznajemy za **dojrzały bezpieczeństwo**, gdy:

- pamięć użytkownika jest izolowana (per-process paging, walidacja wskaźników);
- W^X / NX egzekwowane tam, gdzie architektura pozwala;
- hasła hashowane i przechowywane bezpiecznie;
- pakiety `.tapp` i obrazy weryfikowane kryptograficznie po stronie targetu;
- wysokie ryzyko poleceń shell wymaga potwierdzenia;
- watchdog i limity zasobów syscall są egzekwowane.

System uznajemy za **dojrzały użyteczność**, gdy:

- terminal ma historię, edycję linii i tab completion;
- TTE (Terminal Environment) z paskiem statusu jest domyślnym UX;
- instalator prowadzi użytkownika przez język, klawiaturę, sieć i konta;
- first-boot summary pokazuje stan systemu;
- dokumentacja i `helpsearch` pokrywają wszystkie dostępne polecenia.

### 2.1 Brama zakończenia każdego zakresu zmian

**Każdy nowy zakres zmian** (fala planu, partia bugfixów, refaktor kernel/shell/bezpieczeństwo) **musi zakończyć się solidnymi testami stabilności i bezpieczeństwa** — zanim uznamy go za ukończony.

Obowiązkowy pakiet:

```bash
scripts/tinyos-dev.sh test-gate
```

Następnie w shellu TinyOS (niedestrukcyjnie): `syscheck`, `securityinfo`, `integritycheck`, `riskinfo`, `profilecheck`.

Szczegóły, testy dodatkowe per obszar i definicja „done”: **`docs/testing.md`** → sekcja *Change scope gate*.

Przykład dla ukończonej Fali 1: `test-gate` + markery boot (`context switch`, `guard pages`, `watchdog`) + `schedinfo` / `taskinfo` w shellu.

---

## 3. Macierz priorytetów

| Fala | Cel | Funkcjonalność | Bezpieczeństwo | Użyteczność | Priorytet |
|------|-----|----------------|----------------|-------------|-----------|
| F1 | Wielozadaniowość | ★★★ | ★ | — | P0 |
| F2 | Trwała pamięć masowa | ★★★ | ★ | — | P0 |
| F3 | Userspace + syscalls | ★★★ | ★★★ | — | P0 |
| F4 | Hardening bezpieczeństwa | ★ | ★★★ | ★ | P1 |
| F5 | UX terminala | — | ★ | ★★★ | P1 |
| F6 | System z instalacją | ★★★ | ★★ | ★★★ | P1 |
| F7 | Sieć i provisioning | ★★ | ★★ | ★ | P2 |
| F8 | Portowalność | ★★ | ★ | — | P2 |
| F9 | Aplikacje i GUI | ★★ | ★★ | ★★ | P2–P3 |

---

## 4. Fala 1 — Wielozadaniowy kernel (P0)

**Cel:** kernel faktycznie wykonuje wiele zadań z preempcją.

**Stan wyjściowy:** `arch/i686/context.cpp`, `kernel/sched/scheduler.cpp`, `kernel/task/task.cpp`.

### Zadania

| # | Zadanie | Pliki docelowe | Kryterium akceptacji |
|---|---------|----------------|----------------------|
| 1.1 | Aktywne przełączanie kontekstu i686 | `arch/i686/context.cpp`, `arch/i686/context_switch.asm` | **Zrobione** — `context_switch_available()` → `true`, `arch_context_switch` |
| 1.2 | Podłączenie round-robin do context switch | `kernel/sched/scheduler.cpp` | **Zrobione** — `yield()` / `dispatch_selected_task()` |
| 1.3 | Preemptive scheduling na tick PIT | `kernel/sched/scheduler.cpp`, `kernel/task/task.cpp`, `drivers/keyboard.cpp` | **Zrobione** — `poll_reschedule()` w idle, keyboard i co 5 ticków PIT |
| 1.4 | Guard pages na stosach kernel tasks | `kernel/memory/paging.cpp`, `kernel/task/task.cpp` | **Zrobione** — `clear_page_present()`, `install_stack_guards()`, boot self-test |
| 1.5 | Task watchdog / timeout | `kernel/sched/scheduler.cpp`, `shell/shell.cpp` | **Zrobione** — licznik ticków bez yield, `schedinfo` + klog warn |

### Testy

```bash
scripts/tinyos-dev.sh iso && scripts/tinyos-dev.sh test
# W shell: schedinfo — Preemption: enabled, Guard pages: installed, Context switches > 0
```

### Zależności

Brak — można rozpocząć natychmiast.

---

## 5. Fala 2 — Trwała pamięć masowa (P0)

**Cel:** dane przetrwają restart; VFS obsługuje zapis na block device.

**Stan wyjściowy:** `kernel/device/block.cpp`, `kernel/vfs/blockfs.cpp`, `kernel/initrd/modules.cpp`.

### Zadania

| # | Zadanie | Pliki docelowe | Kryterium akceptacji |
|---|---------|----------------|----------------------|
| 2.1 | Sterownik VirtIO block (QEMU) | `drivers/virtio_blk.cpp` (nowy) | Odczyt/zapis sektorów w QEMU `-device virtio-blk` |
| 2.2 | Filesystem read-only na block device | `kernel/vfs/blockfs.cpp` | Mount `/volumes/disk0` z realnymi plikami |
| 2.3 | Writable block-backed FS | `kernel/vfs/` (nowy backend lub rozszerzenie blockfs) | `write`, `edit` zapisują na dysk |
| 2.4 | Polecenie `mount` | `shell/shell.cpp`, `kernel/admin/tools.cpp` | `mount /volumes/disk0 /mnt` działa |
| 2.5 | Initrd module mount do VFS | `kernel/initrd/modules.cpp` | Pliki z modułu boot widoczne w `/boot/` |
| 2.6 | Generowanie obrazu `.img` | `scripts/tinyos-image.sh`, `Makefile` | `make disk-image` → boot z dysku w QEMU |
| 2.7 | Layout `/system`, `/users`, `/apps` | `docs/installed-system-pattern.md` | Struktura katalogów na trwałym FS |

### Testy

```bash
make disk-image
qemu-system-i386 -drive file=build/tinyos.img,format=raw ...
# Zapis pliku, reboot, odczyt pliku — dane zachowane
```

### Zależności

F1 zalecana (driver I/O może używać tasków), ale nie blokuje.

---

## 6. Fala 3 — Granica userspace (P0)

**Cel:** prawdziwe procesy ring-3 z syscalls; shell przeniesiony do userland.

**Stan wyjściowy:** `kernel/user/transition.cpp`, `kernel/syscall/syscall.cpp`, `kernel/elf/loader.cpp`, `kernel/memory/address_space.cpp`.

### Zadania

| # | Zadanie | Pliki docelowe | Kryterium akceptacji |
|---|---------|----------------|----------------------|
| 3.1 | Per-process address spaces | `kernel/memory/address_space.cpp` | Każdy proces ma własne tablice stron |
| 3.2 | Ring-3 entry + syscall trap (INT 0x80) | `arch/i686/interrupts.cpp`, `kernel/user/transition.cpp` | User code wywołuje syscall i wraca |
| 3.3 | Implementacja `read`, `write`, `open`, `close` | `kernel/syscall/syscall.cpp` | User process czyta/pisze przez VFS |
| 3.4 | Implementacja `spawn`, `exit` | `kernel/syscall/syscall.cpp` | Proces tworzy potomka i kończy się |
| 3.5 | ELF segment loading | `kernel/elf/loader.cpp` | ELF32 ładowany do user address space |
| 3.6 | Start `init` jako pierwszy proces user | `kernel/user/transition.cpp`, `kernel/app/launcher.cpp` | `init_launch_supported()` → `true` |
| 3.7 | Walidacja wskaźników user w syscall | `kernel/syscall/syscall.cpp` | Odrzucenie wskaźnika spoza user mappings |
| 3.8 | Port shell do userland | `apps/shell/` (nowy), `build/linker.ld` | Shell działa w ring-3 |
| 3.9 | Osobne nagłówki ABI user vs kernel | `include/tinyos/user/` (nowy) | Kernel headers niedostępne dla apps |

### Testy

```bash
make iso
# Boot → init → shell w ring-3
# spawn example-system-tool → ELF wykonany
```

### Zależności

**Wymaga F1** (scheduler) i **F2** (trwały FS dla `/system/init`).

---

## 7. Fala 4 — Hardening bezpieczeństwa (P1)

**Cel:** izolacja, zaufanie i egzekwowanie polityk bezpieczeństwa.

**Stan wyjściowy:** `kernel/security/trust.cpp`, `kernel/security/integrity.cpp`, `kernel/syscall/syscall.cpp`.

### Zadania

| # | Zadanie | Pliki docelowe | Kryterium akceptacji |
|---|---------|----------------|----------------------|
| 4.1 | Userspace memory isolation (W^X) | `kernel/memory/paging.cpp` | Code RX, data RW, brak RWX |
| 4.2 | Syscall filtering per process | `kernel/syscall/syscall.cpp` | Proces z ograniczoną maską nie może wywołać `spawn` |
| 4.3 | Syscall resource limits | `kernel/syscall/syscall.cpp` | Limit otwartych fd i pamięci egzekwowany |
| 4.4 | Password hashing (np. PBKDF2/argon2-lite) | `kernel/security/` (nowy moduł) | Hasło hashowane, nie plaintext |
| 4.5 | Credential store na trwałym FS | `kernel/security/`, `/system/credentials` | `passwd` zmienia hash w persistent store |
| 4.6 | Target-side `.tapp` verification | `kernel/app/package_verifier.cpp` | Podpis weryfikowany kryptograficznie w kernel |
| 4.7 | Target-side image signature verification | `kernel/provision/image.cpp`, `kernel/security/trust.cpp` | Aktywacja obrazu wymaga ważnego podpisu |
| 4.8 | Trust anchors w persistent storage | `kernel/security/trust.cpp` | Klucze root w `/system/trust/` |
| 4.9 | Stack canaries | `arch/i686/context.cpp`, kompilator | `-fstack-protector` lub własne canary |
| 4.10 | Potwierdzenie poleceń wysokiego ryzyka | `shell/shell.cpp` | `riskinfo` flagged commands wymagają `yes` |
| 4.11 | Debug heap poison patterns | `kernel/memory/heap.cpp` | Use-after-free wykrywany w debug build |
| 4.12 | Task watchdog enforcement | `kernel/task/task.cpp` | Zawieszony task terminowany po timeout |

### Testy

```bash
make test-boot
# securityinfo — wszystkie Stage 4–7 security-roadmap [x]
# tappverify z podpisanym pakietem → OK
# tappverify z niepodpisanym → odrzucony
```

### Zależności

**Wymaga F3** (userspace boundary).

---

## 8. Fala 5 — Użyteczność terminala (P1)

**Cel:** terminal przyjazny dla codziennej pracy.

**Stan wyjściowy:** `shell/shell.cpp`, `ui/terminal.cpp`.

### Zadania

| # | Zadanie | Pliki docelowe | Kryterium akceptacji |
|---|---------|----------------|----------------------|
| 5.1 | Historia poleceń (strzałki góra/dół) | `shell/shell.cpp` lub `apps/shell/` | Poprzednie polecenia dostępne |
| 5.2 | Tab completion (polecenia + ścieżki) | `shell/shell.cpp` | Tab uzupełnia prefiks |
| 5.3 | Edycja linii (Home/End, insert/delete) | `shell/shell.cpp` | Pełna edycja przed Enter |
| 5.4 | Scrollback buffer | `ui/terminal.cpp`, `drivers/vga.cpp` | Przewijanie historii wyjścia |
| 5.5 | TTE jako domyślny UX | `ui/terminal.cpp` | Status bar z uptime, profile, storage |
| 5.6 | Refaktor shell → moduły | `apps/shell/` | Podział na parser, builtins, completion |
| 5.7 | Persistent shell preferences | `/system/tinyos.conf` | Motyw, historia, aliasy zachowane |
| 5.8 | `terminaltheme`, `videomode` | `kernel/admin/tools.cpp` | Zmiana motywu terminala |
| 5.9 | Console backend abstraction | `drivers/console/` (nowy) | VGA / serial / FB jako backends |
| 5.10 | Serial mirror interaktywnej konsoli | `drivers/serial.cpp` | Shell output widoczny na serial |

### Testy

```bash
make iso && make run
# Interaktywny test: historia, tab, scrollback, TTE status bar
```

### Zależności

**Zalecana po F3** (shell w userland), ale część prac można zacząć wcześniej w kernel shell.

---

## 9. Fala 6 — System z instalacją (P1)

**Cel:** TinyOS instalowalny na dysk z kontami użytkowników.

**Stan wyjściowy:** `examples/install.profile`, `docs/installed-system-pattern.md`, mock `install` w shell.

### Zadania

| # | Zadanie | Pliki docelowe | Kryterium akceptacji |
|---|---------|----------------|----------------------|
| 6.1 | Terminal installer (interaktywny) | `apps/installer/` (nowy) | Partycjonowanie, format, kopiowanie systemu |
| 6.2 | Boot z zainstalowanego dysku | `scripts/tinyos-image.sh`, GRUB config | QEMU boot bez ISO |
| 6.3 | First-boot profile na persistent FS | `/system/profile.txt` | Profil przetrwa restart |
| 6.4 | `useradd`, `passwd`, `whoami`, `id` | `kernel/admin/tools.cpp`, shell | Zarządzanie kontami |
| 6.5 | Wybór języka i klawiatury w installerze | `apps/installer/` | Layout/locale zapisany w profilu |
| 6.6 | First-boot summary screen | `apps/firstboot/` (nowy) | Podsumowanie: hostname, user, storage, network |
| 6.7 | Persistent logs i receipts | `/logs/`, `/receipts/` | Logi i receipts na dysku |
| 6.8 | Install receipt + rollback metadata | `kernel/provision/image.cpp` | Poprzedni slot dostępny do rollback |
| 6.9 | QEMU disk-install smoke test | `Makefile`, `docs/testing.md` | CI: install → reboot → login |

### Testy

```bash
make disk-install-test
# Pełny cykl: ISO → install → reboot → login → plik persistent
```

### Zależności

**Wymaga F2** (persistent FS), **F4** (credentials), **F5** (installer UX).

---

## 10. Fala 7 — Sieć i provisioning (P2)

**Cel:** podstawowa łączność sieciowa i bezpieczne wdrażanie obrazów.

### Zadania

| # | Zadanie | Kryterium akceptacji |
|---|---------|----------------------|
| 7.1 | Model urządzenia sieciowego + sterownik VirtIO net | `ping` do hosta w QEMU |
| 7.2 | Podstawowy stack L2/L3 (ARP, IP, ICMP) | `netinfo` pokazuje adres |
| 7.3 | DHCP client | Automatyczna konfiguracja w QEMU user net |
| 7.4 | Static IP + DNS | `netconfig` ustawia i zapisuje profil |
| 7.5 | `hostname` shell command | Nazwa hosta persistent |
| 7.6 | Target verify przed aktywacją obrazu | `provision` odrzuca niepodpisany obraz |
| 7.7 | Rollback slot | `rollback` przywraca poprzedni obraz |
| 7.8 | Host provisioning workbench | `provisioninit`, `provisionconfig`, `provisionvariant` |
| 7.9 | TinyLink transport | Podpisany kanał provisioning (post-TCP) |

### Zależności

**Wymaga F6** (installed system).

---

## 11. Fala 8 — Portowalność (P2)

**Cel:** TinyOS na `x86_64` i `aarch64` (QEMU virt).

### Zadania

| # | Zadanie | Kryterium akceptacji |
|---|---------|----------------------|
| 8.1 | `ARCH=` / `PLATFORM=` w Makefile | `make ARCH=x86_64 iso` buduje |
| 8.2 | Split `kernel/platform/` → `platform/` | Czysty podział arch vs platform |
| 8.3 | `arch/x86_64/` — long mode, 4-level paging, NX | Boot ISO w QEMU x86_64 |
| 8.4 | `arch/aarch64/` — MMU, GIC, PL011 serial | Boot w QEMU virt |
| 8.5 | Per-arch linker scripts i QEMU targets | `make run ARCH=aarch64` |
| 8.6 | Top-level `apps/` dla userland | Shell, init, przykładowe apps poza kernel |
| 8.7 | `libk/` z `core/` | Biblioteka freestanding wydzielona |

### Zależności

**Wymaga F3** (userspace) — port bez userland nie ma sensu produktowego.

---

## 12. Fala 9 — Aplikacje i opcjonalne GUI (P2–P3)

**Cel:** ekosystem aplikacji natywnych i opcjonalny desktop.

### Zadania

| # | Zadanie | Kryterium akceptacji |
|---|---------|----------------------|
| 9.1 | Native ELF32 app launch | `example-system-tool` wykonany |
| 9.2 | `tappinstall`, `tappremove`, `package` | Instalacja pakietów `.tapp` |
| 9.3 | `ps`, `kill`, `service` | Zarządzanie procesami i usługami |
| 9.4 | Framebuffer text rendering | Tekst na FB w rozdzielczości > 80×25 |
| 9.5 | Graphical terminal | Terminal w oknie WM |
| 9.6 | Production GUI widget set | Reusable widgets na rendererze |
| 9.7 | Sandbox runtimes (WASM, bytecode) | Po stabilizacji native launch |

### Zależności

**Wymaga F3, F4, F7**.

---

## 13. Harmonogram i kamienie milowe

```
v0.2  ── F1 ──► Wielozadaniowy kernel
v0.3  ── F2 ──► Trwała pamięć masowa + disk boot
v0.4  ── F3 ──► Userspace + syscalls + init
v0.5  ── F4 ──► Hardening bezpieczeństwa
v0.6  ── F5 ──► UX terminala (TTE, historia, completion)
v0.7  ── F6 ──► Instalator + konta + first-boot
v0.8  ── F7 ──► Sieć + provisioning + rollback
v0.9  ── F8 ──► x86_64 boot
v1.0  ── F8+9 ► aarch64 + native apps + stabilizacja
```

Każda wersja musi spełniać bramki jakości z `docs/os-roadmap.md`:

- `make iso` buduje bootowalny obraz;
- `make test-boot` osiąga marker sukcesu w QEMU;
- panic paths drukują diagnostykę VGA + serial;
- shell pozostaje używalny (nie regresja UX).

---

## 14. Metryki dojrzałości (scorecard)

Po zakończeniu wszystkich fal P0–P1 system powinien osiągnąć:

| Metryka | Obecny stan | Cel v1.0 |
|---------|-------------|----------|
| Context switch aktywny | Nie | Tak |
| Preemptive scheduling | Nie | Tak |
| Persistent writable FS | Nie | Tak |
| User processes (ring-3) | Nie | Tak |
| Syscalls zaimplementowane | 0/7 | 7/7 |
| Password hashing | Nie | Tak |
| Target-side package verify | Dry-check | Crypto verify |
| Shell historia + completion | Nie | Tak |
| Disk installer | Mock | Pełny |
| Boot z dysku | Nie | Tak |
| Networking (ping/DHCP) | Nie | Tak |
| x86_64 boot | Nie | Tak |
| Security roadmap Stage 4–7 | ~30% | ≥80% |
| Backlog pending items | ~202 | <50 |

---

## 15. Mapowanie kluczowego kodu do fal

| Plik / moduł | Fala | Rola |
|--------------|------|------|
| `arch/i686/context.cpp` | F1 | Przełączanie kontekstu CPU |
| `kernel/sched/scheduler.cpp` | F1 | Polityka planowania |
| `kernel/task/task.cpp` | F1, F4 | Task lifecycle + watchdog |
| `kernel/device/block.cpp` | F2 | Abstrakcja block device |
| `kernel/vfs/blockfs.cpp` | F2 | FS na block device |
| `kernel/initrd/modules.cpp` | F2 | Boot modules → VFS |
| `kernel/memory/address_space.cpp` | F3 | Per-process mappings |
| `kernel/user/transition.cpp` | F3 | Ring-3 entry |
| `kernel/syscall/syscall.cpp` | F3, F4 | Dispatch + enforcement |
| `kernel/elf/loader.cpp` | F3 | Ładowanie programów |
| `kernel/app/launcher.cpp` | F3, F9 | Uruchamianie apps |
| `kernel/security/trust.cpp` | F4, F7 | Trust store + weryfikacja |
| `kernel/app/package_verifier.cpp` | F4, F9 | Weryfikacja `.tapp` |
| `shell/shell.cpp` | F5 | UX terminala |
| `ui/terminal.cpp` | F5 | TTE session |
| `kernel/admin/tools.cpp` | F5, F6 | Manifest poleceń shell |
| `examples/install.profile` | F6 | Profil instalacji |
| `kernel/provision/image.cpp` | F6, F7 | Image signing + rollback |
| `scripts/tinyos-image.sh` | F2, F6, F8 | Host image tooling |
| `Makefile` | F8 | Multi-arch build matrix |

---

## 16. Zasady utrzymania planu

1. Po implementacji pozycji — usuń z `docs/pending-implementation-roadmap.md`, oznacz `[x]` w `docs/implementation-roadmap.md`.
2. Każda fala kończy się bootowalnym przyrostem — brak „big bang".
3. Bezpieczeństwo wbudowane w każdą falę (nie osobny „security sprint" na końcu).
4. Użyteczność rośnie iteracyjnie — TTE i historia można zacząć przed pełnym userland.
5. Elementy P3 (GUI, WASM, SMP) pozostają odroczone do stabilizacji P0–P1.

---

## 17. Następny krok

**Natychmiastowy priorytet:** Fala 1, zadanie 1.1 — implementacja aktywnego `context_switch()` w `arch/i686/context.cpp`.

To odblokowuje scheduler, preemptive scheduling, a następnie cały łańcuch userspace → bezpieczeństwo → instalacja.

TARGET := tinyos.kernel
ARCH ?= i686
DEBUG_BOOT ?= 0
BOOT_TEST_TIMEOUT ?= 8s
STABILITY_TEST_TIMEOUT ?= 20s
MINIMAL_TEST_MEMORY ?= 32M
MINIMAL_PROBE_MEMORY ?= 32M 24M 16M
LOWMEM_PROBE_MEMORY ?= 4M 3M 2880K 2816K 2752K 2688K 2624K 2561K 2560K 2529K 2528K 2M 1536K 1024K 512K 256K 128K 64K
LOWMEM_PROBE_SUMMARY ?= build/lowmem-probe-summary.txt
LOWMEM_PROBE_LABEL ?= TinyOS terminal boot
LOWMEM_PROBE_EXTRA_MARKER ?=
BOOT_TEST_LOG ?= build/boot-smoke.log
MINIMAL_TEST_LOG ?= build/boot-minimal.log

# Boot marker contract. Each architecture verifies its own file so a marker that
# only exists on one backend cannot silently gate the other.
ifeq ($(ARCH),x86_64)
	BOOT_MARKERS := tests/boot-markers-x86_64.txt
else
	BOOT_MARKERS := tests/boot-markers.txt
endif

# Architecture-specific artefacts.
#
# Every architecture gets its own object directory, ISO tree and image name.
# Sharing them was a real source of bugs: changing CXXFLAGS does not invalidate
# existing .o files, so an i686 build could silently link objects compiled for a
# different target.
ifeq ($(ARCH),x86_64)
	ARCH_TARGET := tinyos-x86_64.kernel
	ISO := build/tinyos-x86_64.iso
	ISO_DIR := build/isodir-x86_64
	OBJ_DIR := build/obj-x86_64
	LINKER_SCRIPT := build/linker-x86_64.ld
	GRUB_CONFIG := build/grub/grub-x86_64.cfg
	QEMU ?= qemu-system-x86_64
	ARCH_CXXFLAGS ?= --target=x86_64-elf
	ARCH_ASFLAGS := -f elf64
	CLANG_RT_BUILTINS := $(shell ls /usr/lib/llvm-*/lib/clang/*/lib/linux/libclang_rt.builtins-x86_64.a 2>/dev/null | head -n 1)
else
	ARCH_TARGET := tinyos.kernel
	ISO := build/tinyos.iso
	ISO_DIR := build/isodir
	OBJ_DIR := build/obj
	LINKER_SCRIPT := build/linker.ld
	GRUB_CONFIG := build/grub/grub.cfg
	QEMU ?= qemu-system-i386
	ARCH_CXXFLAGS ?= --target=i686-elf
	ARCH_ASFLAGS := -f elf32
	CLANG_RT_BUILTINS := $(shell ls /usr/lib/llvm-*/lib/clang/*/lib/linux/libclang_rt.builtins-i386.a 2>/dev/null | head -n 1)
endif

TARGET := $(ARCH_TARGET)

I686_CXX := $(shell command -v i686-elf-g++ 2>/dev/null)
CLANG_CXX := $(shell command -v clang++ 2>/dev/null)
LLD := $(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null)

ifeq ($(origin CXX), default)
ifneq ($(I686_CXX),)
	CXX := i686-elf-g++
else ifneq ($(CLANG_CXX),)
	CXX := clang++
else
	CXX := clang++
endif
endif

ifneq ($(findstring i686-elf-g++,$(notdir $(CXX))),)
	ARCH_LDFLAGS ?=
	LIBS ?= -lgcc
	LINKER_CHECK :=
	LINK = $(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)
else
	ARCH_LDFLAGS ?=
	LIBS ?=
	LINKER_CHECK := lld
	LINK = ld.lld -T $(LINKER_SCRIPT) -o $@ $(OBJECTS) $(CLANG_RT_BUILTINS)
endif

ifeq ($(origin AS), default)
	AS := nasm
endif

ifeq ($(shell command -v grub-mkrescue 2>/dev/null),)
	GRUBMKRESCUE ?= grub2-mkrescue
else
	GRUBMKRESCUE ?= grub-mkrescue
endif

XORRISO ?= xorriso
TIMEOUT ?= timeout

REQUIRED_BUILD_TOOLS := $(CXX) $(AS) $(LINKER_CHECK)
REQUIRED_IMAGE_TOOLS := $(REQUIRED_BUILD_TOOLS) $(GRUBMKRESCUE) $(XORRISO)
REQUIRED_QEMU_TOOLS := $(QEMU) $(TIMEOUT)
REQUIRED_TEST_TOOLS := $(REQUIRED_IMAGE_TOOLS) $(REQUIRED_QEMU_TOOLS)

define require_tools
	@missing=0; \
	for tool in $(1); do \
		case "$$tool" in \
			clang++) \
				if command -v clang++ >/dev/null 2>&1; then \
					echo "found: clang++ ($$(command -v clang++))"; \
				elif command -v i686-elf-g++ >/dev/null 2>&1; then \
					echo "found: i686-elf-g++ ($$(command -v i686-elf-g++))"; \
				else \
					echo "missing: clang++ (or i686-elf-g++)"; \
					missing=1; \
				fi ;; \
			lld) \
				if command -v ld.lld >/dev/null 2>&1; then \
					echo "found: ld.lld ($$(command -v ld.lld))"; \
				elif command -v lld >/dev/null 2>&1; then \
					echo "found: lld ($$(command -v lld))"; \
				else \
					echo "missing: ld.lld (or lld)"; \
					missing=1; \
				fi ;; \
			grub2-mkrescue) \
				if command -v grub2-mkrescue >/dev/null 2>&1; then \
					echo "found: grub2-mkrescue ($$(command -v grub2-mkrescue))"; \
				elif command -v grub-mkrescue >/dev/null 2>&1; then \
					echo "found: grub-mkrescue ($$(command -v grub-mkrescue))"; \
				else \
					echo "missing: grub2-mkrescue (or grub-mkrescue)"; \
					missing=1; \
				fi ;; \
			"") ;; \
			*) \
				if command -v $$tool >/dev/null 2>&1; then \
					echo "found: $$tool ($$(command -v $$tool))"; \
				else \
					echo "missing: $$tool"; \
					missing=1; \
				fi ;; \
		esac; \
	done; \
	if [ $$missing -ne 0 ]; then \
		echo ""; \
		echo "Install missing tools for Fedora:"; \
		echo "  sudo dnf install -y clang lld nasm make grub2-tools-extra xorriso qemu-system-x86"; \
		echo "Or run:"; \
		echo "  scripts/tinyos-dev.sh install-deps --install"; \
		exit 1; \
	fi
endef

CXXFLAGS := -std=gnu++17 -ffreestanding -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-stack-protector -fno-pic -fno-pie -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -Wall -Wextra -O2 -Iinclude $(ARCH_CXXFLAGS)
ASFLAGS := $(ARCH_ASFLAGS)
LDFLAGS := -T $(LINKER_SCRIPT) -ffreestanding -O2 -nostdlib -no-pie $(ARCH_LDFLAGS)

ifeq ($(DEBUG_BOOT),1)
	CXXFLAGS += -DTINYOS_DEBUG_BOOT
endif

# Development-only bootstrap credentials (root/tinyos, user/user). Never enable
# this for a release image; the credential store replaces it in the security
# phase.
#
# Enabled by default for the QEMU development image: without a way to log in, the
# shell is unusable because an unauthenticated session has no administrative
# capabilities (defect D48). A release build must pass
# ALLOW_DEVELOPMENT_CREDENTIALS=0, and the boot log says which build it is.
ALLOW_DEVELOPMENT_CREDENTIALS ?= 1
ifeq ($(ALLOW_DEVELOPMENT_CREDENTIALS),1)
	CXXFLAGS += -DTINYOS_ALLOW_DEVELOPMENT_CREDENTIALS
endif

KERNEL_CPP_SOURCES := \
	api/system_api.cpp \
   core/memory.cpp \
	core/libc.cpp \
	core/string.cpp \
	libk/text.cpp \
	libk/sha256.cpp \
	libk/path.cpp \
	libshell/stream.cpp \
	libshell/session.cpp \
	libshell/registry.cpp \
	libshell/parser.cpp \
	libshell/exec.cpp \
	libshell/capture.cpp \
	kernel/sync.cpp \
	drivers/input.cpp \
   drivers/pic.cpp \
	drivers/pit.cpp \
 drivers/serial.cpp \
	drivers/console.cpp \
	drivers/virtio_blk.cpp \
	drivers/virtio_net.cpp \
	drivers/ata.cpp \
	drivers/usb_hid.cpp \
	drivers/vga.cpp \
	drivers/keyboard.cpp \
	 kernel/admin/tools.cpp \
 kernel/device/block.cpp \
 kernel/device/framebuffer.cpp \
 kernel/device/registry.cpp \
 kernel/app/launcher.cpp \
 kernel/app/manifest.cpp \
 kernel/app/package.cpp \
 kernel/app/package_verifier.cpp \
 kernel/app/runtime.cpp \
 kernel/provision/image.cpp \
 kernel/elf/loader.cpp \
    kernel/initrd/modules.cpp \
    kernel/memory/address_space.cpp \
    kernel/memory/heap.cpp \
 kernel/memory/frame_allocator.cpp \
 kernel/memory/memory_map.cpp \
 kernel/memory/paging.cpp \
    kernel/memory/user_access.cpp \
    kernel/interrupts.cpp \
	kernel/klog.cpp \
	kernel/panic.cpp \
	 kernel/platform/pc.cpp \
	 kernel/platform/requirements.cpp \
   kernel/sched/scheduler.cpp \
  kernel/security/integrity.cpp \
	kernel/security/trust.cpp \
	kernel/security/accounts.cpp \
  kernel/syscall/syscall.cpp \
	kernel/task/task.cpp \
 kernel/user/transition.cpp \
 kernel/vfs/blockfs.cpp \
 kernel/vfs/fatfs.cpp \
 kernel/vfs/mount.cpp \
 kernel/vfs/ramfs.cpp \
   kernel/vfs/vfs.cpp \
	ui/renderer.cpp \
	ui/events.cpp \
	ui/terminal.cpp \
	ui/widgets.cpp \
	ui/font.cpp \
	ui/font_atlas.cpp \
	ui/gfx_scrollback.cpp \
	shell/sh.cpp \
	shell/commands/fs.cpp \
	shell/commands/shell.cpp \
	shell/commands/system.cpp \
	shell/commands/scheduler.cpp \
	shell/commands/identity.cpp \
	shell/commands/planned.cpp \
	shell/commands/register.cpp

ASM_SOURCES := \
    arch/i686/interrupt_stubs.asm \
    arch/i686/context_switch.asm \
    arch/i686/gdt_flush.asm \
    arch/i686/syscall_stub.asm

ifneq ($(ARCH),x86_64)
	KERNEL_CPP_SOURCES += \
		arch/i686/arch.cpp \
		arch/i686/context.cpp \
		arch/i686/gdt.cpp \
		arch/i686/interrupts.cpp \
		arch/i686/io.cpp \
		arch/i686/pci.cpp \
		kernel/kernel.cpp
	ASM_SOURCES += boot/multiboot.asm
else
	# The x86_64 tree currently contains the bring-up path only: long mode,
	# CPU capability probing, GDT/TSS with IST and an IDT with full fault
	# reporting. The portable kernel sources above are added back as each
	# subsystem gains a 64-bit backend (see docs/x86_64-migration.md).
	KERNEL_CPP_SOURCES := \
		drivers/serial.cpp \
		arch/x86_64/bootstrap.cpp \
		arch/x86_64/cpu.cpp \
		arch/x86_64/gdt.cpp \
		arch/x86_64/interrupts.cpp \
		arch/x86_64/io.cpp
	ASM_SOURCES := \
		arch/x86_64/boot64.asm \
		arch/x86_64/interrupt_stubs64.asm
endif

CPP_SOURCES := $(KERNEL_CPP_SOURCES)

OBJECTS := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o) $(ASM_SOURCES:%.asm=$(OBJ_DIR)/%.o)
DEPFILES := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.d)

DISK_IMAGE ?= build/tinyos-disk.img
BOOT_DISK_IMAGE ?= build/tinyos.img
DISK_SECTORS ?= 8192
VIRTIO_BOOT_TEST_LOG ?= build/boot-virtio.log
DISK_BOOT_TEST_LOG ?= build/boot-disk.log
QEMU_VIRTIO_ARGS = -drive file=$(DISK_IMAGE),format=raw,if=none,id=disk0 -device virtio-blk-pci,drive=disk0
QEMU_DISK_BOOT_ARGS = -drive file=$(BOOT_DISK_IMAGE),format=raw,if=ide,index=0,media=disk -boot order=c

.PHONY: all iso run run-headless image-plan provision-plan install-plan image-profile-check install-profile-check image-app-check image-deploy-check-test tapp-pack tapp-verify tapp-sign-test tapp-trust-test image-build disk-image virtio-disk-image ata-disk-image boot-disk-image test-boot test-boot-x86_64 test-shell-qemu test-memory-scaling test-host test-virtio-block test-ata-block test-disk-boot test-existing-iso test-minimal test-minimal-probe test-lowmem-probe test-stability test-gate debug-boot debug-run check-build-tools check-image-tools check-qemu-tools check-test-tools prepare-test-env dev-help clean

all: check-build-tools $(TARGET)

check-build-tools:
	$(call require_tools,$(REQUIRED_BUILD_TOOLS))

check-image-tools:
	$(call require_tools,$(REQUIRED_IMAGE_TOOLS))

check-qemu-tools:
	$(call require_tools,$(REQUIRED_QEMU_TOOLS))

check-test-tools:
	$(call require_tools,$(REQUIRED_TEST_TOOLS))

prepare-test-env: check-test-tools
	@mkdir -p build
	@echo "TinyOS test environment is ready."

dev-help:
	@echo "TinyOS development helper: scripts/tinyos-dev.sh"
	@echo "  check | build | iso | run | run-serial | test | test-gate | test-terminal | clean"
	@bash scripts/tinyos-dev.sh help

# Host unit tests: compile the pure-logic modules for the host and run them.
# These cover security-relevant validators that used to be testable only by
# booting the kernel and typing at the shell.
#
# Every tests/host/test_*.cpp becomes its own binary automatically, so adding a
# test never requires editing this file. The previous hard-coded single-entry
# source list actively discouraged writing tests (defect D26).
#
# tests/host/test_<name>.cpp links libk/*.cpp plus the pure-logic core helpers.
# Tests that need different objects can override HOST_TEST_EXTRA_SOURCES on the
# command line, or place their own main() and rely on the defaults below.
HOST_TEST_DIR := tests/host
HOST_TEST_BIN_DIR := build/host-test-bin
# Support objects available to every host test. These must stay free of kernel
# and driver dependencies: the registry pulls stream.hpp only for the Stream
# pointer in Context, never the driver-backed implementations.
HOST_TEST_SUPPORT := \
	libk/path.cpp \
	libk/text.cpp \
	libk/sha256.cpp \
	core/memory.cpp \
	libshell/session.cpp \
	libshell/registry.cpp \
	libshell/parser.cpp \
	libshell/exec.cpp \
	libshell/capture.cpp
HOST_TEST_NAMES := $(patsubst $(HOST_TEST_DIR)/test_%.cpp,%,$(wildcard $(HOST_TEST_DIR)/test_*.cpp))
HOST_TEST_BINS := $(addprefix $(HOST_TEST_BIN_DIR)/,$(HOST_TEST_NAMES))
HOST_CXXFLAGS := -std=gnu++17 -Wall -Wextra -O1 -Iinclude

test-host:
	@mkdir -p $(HOST_TEST_BIN_DIR)
	@set -e; \
	if [ -z "$(HOST_TEST_NAMES)" ]; then \
		echo "No host tests found in $(HOST_TEST_DIR)."; \
		exit 1; \
	fi; \
	failures=0; \
	for name in $(HOST_TEST_NAMES); do \
		echo "=== host test: $$name ==="; \
		$(CLANG_CXX) $(HOST_CXXFLAGS) \
			$(HOST_TEST_DIR)/test_$$name.cpp $(HOST_TEST_SUPPORT) $(HOST_TEST_EXTRA_SOURCES) \
			-o $(HOST_TEST_BIN_DIR)/$$name; \
		if ! $(HOST_TEST_BIN_DIR)/$$name; then failures=$$((failures + 1)); fi; \
	done; \
	if [ $$failures -ne 0 ]; then \
		echo "$$failures host test binarie(s) failed."; \
		exit 1; \
	fi; \
	echo "All host tests passed."

$(TARGET): $(OBJECTS)
	$(LINK)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

iso: check-image-tools $(TARGET)
	@mkdir -p $(ISO_DIR)/boot/grub $(ISO_DIR)/boot/modules
	cp $(TARGET) $(ISO_DIR)/boot/tinyos.kernel
	cp $(GRUB_CONFIG) $(ISO_DIR)/boot/grub/grub.cfg
	cp build/initrd/initrd-placeholder.txt $(ISO_DIR)/boot/modules/initrd-placeholder.txt
	$(GRUBMKRESCUE) -o $(ISO) $(ISO_DIR)

run: check-test-tools iso
	$(QEMU) -cdrom $(ISO)

run-headless: check-test-tools iso
	$(QEMU) -cdrom $(ISO) -serial stdio -display none

image-plan:
	bash scripts/tinyos-image.sh plan

provision-plan:
	bash scripts/tinyos-image.sh provision-plan

install-plan:
	bash scripts/tinyos-image.sh install-plan

image-profile-check:
	bash scripts/tinyos-image.sh check-profile examples/system.profile

install-profile-check:
	bash scripts/tinyos-image.sh check-install-profile examples/install.profile

image-app-check:
	bash scripts/tinyos-image.sh check-app examples/app.manifest
	bash scripts/tinyos-image.sh check-app examples/example-system-tool.tapp

image-deploy-check-test:
	@mkdir -p build/security
	@printf 'tinyos-deploy-test\n' > build/security/deploy.iso
	@printf 'tinyos-signature-test\n' > build/security/deploy.iso.sig
	@cp build/security/deploy.iso build/security/deploy.iso.age
	bash scripts/tinyos-image.sh deploy-check build/security/deploy.iso.age
	@if bash scripts/tinyos-image.sh deploy-check build/security/deploy.iso >/dev/null 2>&1; then \
		echo "Deploy check failed: plaintext artifact was accepted."; \
		exit 1; \
	else \
		echo "Plaintext deploy artifact rejected as expected."; \
	fi

tapp-pack:
	bash scripts/tinyos-image.sh pack-app examples/app.manifest build/apps/example-system-tool.tapp

tapp-verify:
	bash scripts/tinyos-image.sh verify-app examples/example-system-tool.tapp

tapp-sign-test:
	bash scripts/tinyos-image.sh keygen-app build/keys/tapp-dev-private.pem build/keys/tapp-dev-public.pem
	bash scripts/tinyos-image.sh trust-app build/keys/tapp-dev-public.pem build/keys/tapp-dev-public.pem.trust
	bash scripts/tinyos-image.sh pack-app examples/app.manifest build/apps/example-system-tool.tapp
	bash scripts/tinyos-image.sh sign-app build/apps/example-system-tool.tapp build/keys/tapp-dev-private.pem
	bash scripts/tinyos-image.sh verify-app build/apps/example-system-tool.tapp build/keys/tapp-dev-public.pem

tapp-trust-test:
	bash scripts/tinyos-image.sh keygen-app build/keys/tapp-dev-private.pem build/keys/tapp-dev-public.pem
	bash scripts/tinyos-image.sh trust-app build/keys/tapp-dev-public.pem build/keys/tapp-dev-public.pem.trust

image-build: check-test-tools
	bash scripts/tinyos-image.sh build

test-boot: check-test-tools iso
	@mkdir -p build
	@rm -f $(BOOT_TEST_LOG)
	@echo "Running TinyOS boot smoke test for $(BOOT_TEST_TIMEOUT)..."
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -cdrom $(ISO) -display none -serial file:$(BOOT_TEST_LOG) -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -ne 124 ]; then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: QEMU exited before timeout with status $$status."; \
		exit 1; \
	fi; \
	python3 scripts/tinyos-check-log.py $(BOOT_TEST_LOG) $(BOOT_MARKERS)

test-boot-x86_64:
	$(MAKE) ARCH=x86_64 BOOT_TEST_LOG=build/boot-x86_64.log test-boot

# Shell behaviour test: drives the real shell over the serial console and
# asserts on responses. Before this target there was no automated test of shell
# behaviour at all, which is how commands shipped that printed "not wired to a
# create path yet" while their manifest entry claimed Ready.
test-shell-qemu: check-test-tools iso
	@echo "Running TinyOS shell behaviour test over serial..."
	python3 scripts/tinyos-shell-test.py $(ISO)

# Memory scaling regression test. The frame allocator used to be clamped to
# 16 MiB and later to a 256 MiB bitmap, so a machine with more RAM silently got
# a tiny pool. This target boots with increasing RAM and requires the managed
# pool to actually follow it.
RAM_PROBE_MEMORY ?= 32M 512M 2G
test-memory-scaling: check-test-tools iso
	@mkdir -p build
	@set -e; \
	for memory in $(RAM_PROBE_MEMORY); do \
		log="build/ram-scaling-$$memory.log"; \
		rm -f "$$log"; \
		set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -m $$memory -cdrom $(ISO) -display none -serial file:"$$log" -no-reboot -no-shutdown >/dev/null 2>&1; set -e; \
		if ! python3 scripts/tinyos-check-log.py "$$log" tests/boot-markers.txt >/dev/null; then \
			cat "$$log"; \
			echo "Memory scaling test failed: boot markers missing at $$memory."; \
			exit 1; \
		fi; \
		if ! python3 scripts/tinyos-check-memory.py "$$log" "$$memory"; then \
			echo "Memory scaling test failed: pool does not follow RAM at $$memory."; \
			exit 1; \
		fi; \
	done; \
	echo "Memory scaling test passed: $(RAM_PROBE_MEMORY)"

test-existing-iso: check-qemu-tools
	@test -f $(ISO) || { echo "Missing $(ISO). Run 'make iso' after installing the build toolchain."; exit 1; }
	@mkdir -p build
	@rm -f $(BOOT_TEST_LOG)
	@echo "Running TinyOS boot smoke test from existing $(ISO) for $(BOOT_TEST_TIMEOUT)..."
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -cdrom $(ISO) -display none -serial file:$(BOOT_TEST_LOG) -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -ne 124 ]; then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: QEMU exited before timeout with status $$status."; \
		exit 1; \
	fi; \
	python3 scripts/tinyos-check-log.py $(BOOT_TEST_LOG) $(BOOT_MARKERS)

test-minimal: check-test-tools iso
	@mkdir -p build
	@rm -f $(MINIMAL_TEST_LOG)
	@echo "Running TinyOS minimal requirement test with $(MINIMAL_TEST_MEMORY) RAM for $(BOOT_TEST_TIMEOUT)..."
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -m $(MINIMAL_TEST_MEMORY) -cdrom $(ISO) -display none -serial file:$(MINIMAL_TEST_LOG) -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -ne 124 ]; then \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: QEMU exited before timeout with status $$status."; \
		exit 1; \
	fi; \
	python3 scripts/tinyos-check-log.py $(MINIMAL_TEST_LOG) $(BOOT_MARKERS)
test-minimal-probe: check-test-tools iso
	@set -e; \
	for memory in $(MINIMAL_PROBE_MEMORY); do \
		safe=$$(printf '%s' "$$memory" | tr -c 'A-Za-z0-9' '_'); \
		echo "Probing TinyOS minimal runtime with $$memory RAM..."; \
		$(MAKE) --no-print-directory MINIMAL_TEST_MEMORY=$$memory MINIMAL_TEST_LOG=build/boot-minimal-$$safe.log test-minimal; \
	done

test-lowmem-probe: check-test-tools iso
	@mkdir -p build
	@printf '%-8s %-8s %s\n' RAM RESULT DETAIL > $(LOWMEM_PROBE_SUMMARY)
	@set -e; \
	for memory in $(LOWMEM_PROBE_MEMORY); do \
		safe=$$(printf '%s' "$$memory" | tr -c 'A-Za-z0-9' '_'); \
		log="build/boot-lowmem-$$safe.log"; \
		rm -f "$$log"; \
		echo "Probing $(LOWMEM_PROBE_LABEL) with $$memory RAM..."; \
		set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -m $$memory -cdrom $(ISO) -display none -serial file:$$log -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; set -e; \
		extra_marker='$(LOWMEM_PROBE_EXTRA_MARKER)'; \
		extra_ok=1; \
		if [ -n "$$extra_marker" ] && ! grep -q "$$extra_marker" "$$log"; then extra_ok=0; fi; \
		if [ $$status -eq 124 ] && [ $$extra_ok -eq 1 ] && grep -q "TinyOS booted successfully" "$$log" && grep -q "System requirements manifest ready" "$$log" && grep -q "Terminal UI scaffold ready" "$$log"; then \
			printf '%-8s %-8s %s\n' "$$memory" PASS "terminal markers present"; \
			printf '%-8s %-8s %s\n' "$$memory" PASS "terminal markers present" >> $(LOWMEM_PROBE_SUMMARY); \
		elif [ $$status -eq 124 ]; then \
			bytes=$$(wc -c < "$$log"); \
			printf '%-8s %-8s %s\n' "$$memory" FAIL "timeout without terminal markers, serial-bytes=$$bytes"; \
			printf '%-8s %-8s %s\n' "$$memory" FAIL "timeout without terminal markers, serial-bytes=$$bytes" >> $(LOWMEM_PROBE_SUMMARY); \
		else \
			printf '%-8s %-8s %s\n' "$$memory" FAIL "QEMU exited status $$status"; \
			printf '%-8s %-8s %s\n' "$$memory" FAIL "QEMU exited status $$status" >> $(LOWMEM_PROBE_SUMMARY); \
		fi; \
	done

test-stability: BOOT_TEST_TIMEOUT = $(STABILITY_TEST_TIMEOUT)
test-stability: test-boot

disk-image: boot-disk-image

boot-disk-image: iso
	@bash scripts/tinyos-boot-disk.sh $(BOOT_DISK_IMAGE) $(ISO)

virtio-disk-image:
	@bash scripts/tinyos-disk-image.sh $(DISK_IMAGE) $(DISK_SECTORS)

ata-disk-image:
	@bash scripts/tinyos-ata-disk-image.sh build/tinyos-ata.img $(DISK_SECTORS)

test-ata-block: check-test-tools iso ata-disk-image
	@mkdir -p build
	@rm -f build/boot-ata.log
	@echo "Running ATA/FAT boot test for $(BOOT_TEST_TIMEOUT)..."
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -cdrom $(ISO) -drive file=build/tinyos-ata.img,format=raw,if=ide,index=0,media=disk -display none -serial file:build/boot-ata.log -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -ne 124 ]; then \
		cat build/boot-ata.log; \
		echo "ATA block boot test failed: QEMU exited before timeout with status $$status."; \
		exit 1; \
	fi; \
	if ! grep -q "ATA PIO primary master ready" build/boot-ata.log; then \
		cat build/boot-ata.log; \
		echo "ATA block boot test failed: ATA marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "FAT16" build/boot-ata.log; then \
		cat build/boot-ata.log; \
		echo "ATA block boot test failed: FAT16 marker not found."; \
		exit 1; \
	fi; \
	echo "ATA/FAT boot test passed."

test-virtio-block: check-test-tools iso virtio-disk-image
	@mkdir -p build
	@rm -f $(VIRTIO_BOOT_TEST_LOG)
	@echo "Running VirtIO block boot test for $(BOOT_TEST_TIMEOUT)..."
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -cdrom $(ISO) $(QEMU_VIRTIO_ARGS) -display none -serial file:$(VIRTIO_BOOT_TEST_LOG) -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -ne 124 ]; then \
		cat $(VIRTIO_BOOT_TEST_LOG); \
		echo "VirtIO block boot test failed: QEMU exited before timeout with status $$status."; \
		exit 1; \
	fi; \
	if ! grep -q "VirtIO block device ready" $(VIRTIO_BOOT_TEST_LOG); then \
		cat $(VIRTIO_BOOT_TEST_LOG); \
		echo "VirtIO block boot test failed: device marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Block catalog loaded" $(VIRTIO_BOOT_TEST_LOG); then \
		cat $(VIRTIO_BOOT_TEST_LOG); \
		echo "VirtIO block boot test failed: block catalog marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Block writable store ready" $(VIRTIO_BOOT_TEST_LOG); then \
		cat $(VIRTIO_BOOT_TEST_LOG); \
		echo "VirtIO block boot test failed: writable store marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Block layout directories ready" $(VIRTIO_BOOT_TEST_LOG); then \
		cat $(VIRTIO_BOOT_TEST_LOG); \
		echo "VirtIO block boot test failed: layout directory marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Persistent layout mounted." $(VIRTIO_BOOT_TEST_LOG); then \
		cat $(VIRTIO_BOOT_TEST_LOG); \
		echo "VirtIO block boot test failed: persistent layout marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "TinyOS booted successfully" $(VIRTIO_BOOT_TEST_LOG); then \
		cat $(VIRTIO_BOOT_TEST_LOG); \
		echo "VirtIO block boot test failed: boot success marker not found."; \
		exit 1; \
	fi; \
	echo "VirtIO block boot test passed. Log: $(VIRTIO_BOOT_TEST_LOG)"

test-disk-boot: check-test-tools boot-disk-image virtio-disk-image
	@mkdir -p build
	@rm -f $(DISK_BOOT_TEST_LOG)
	@echo "Running raw disk boot test for $(BOOT_TEST_TIMEOUT)..."
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) $(QEMU_DISK_BOOT_ARGS) $(QEMU_VIRTIO_ARGS) -display none -serial file:$(DISK_BOOT_TEST_LOG) -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -ne 124 ]; then \
		cat $(DISK_BOOT_TEST_LOG); \
		echo "Disk boot test failed: QEMU exited before timeout with status $$status."; \
		exit 1; \
	fi; \
	if ! grep -q "Initrd boot modules mounted at /boot." $(DISK_BOOT_TEST_LOG); then \
		cat $(DISK_BOOT_TEST_LOG); \
		echo "Disk boot test failed: initrd VFS marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "VirtIO block device ready" $(DISK_BOOT_TEST_LOG); then \
		cat $(DISK_BOOT_TEST_LOG); \
		echo "Disk boot test failed: VirtIO block marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Block catalog loaded" $(DISK_BOOT_TEST_LOG); then \
		cat $(DISK_BOOT_TEST_LOG); \
		echo "Disk boot test failed: block catalog marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Persistent layout mounted." $(DISK_BOOT_TEST_LOG); then \
		cat $(DISK_BOOT_TEST_LOG); \
		echo "Disk boot test failed: persistent layout marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "TinyOS booted successfully" $(DISK_BOOT_TEST_LOG); then \
		cat $(DISK_BOOT_TEST_LOG); \
		echo "Disk boot test failed: boot success marker not found."; \
		exit 1; \
	fi; \
	echo "Disk boot test passed. Log: $(DISK_BOOT_TEST_LOG)"

test-gate: check-test-tools check-image-tools
	@echo "=== TinyOS change-scope gate: stability + security ==="
	$(MAKE) prepare-test-env
	$(MAKE) test-host
	$(MAKE) test-shell-qemu
	$(MAKE) test-stability
	$(MAKE) test-boot-x86_64
	$(MAKE) test-memory-scaling
	$(MAKE) test-minimal
	$(MAKE) test-disk-boot
	$(MAKE) install-profile-check
	$(MAKE) tapp-trust-test
	@echo "Change-scope gate passed. Run manual shell checks: syscheck, securityinfo, integritycheck (see docs/testing.md)."

debug-boot:
	$(MAKE) DEBUG_BOOT=1 TARGET=tinyos-debug.kernel ISO=build/tinyos-debug.iso ISO_DIR=build/isodir-debug OBJ_DIR=build/obj-debug iso

debug-run:
	$(MAKE) DEBUG_BOOT=1 TARGET=tinyos-debug.kernel ISO=build/tinyos-debug.iso ISO_DIR=build/isodir-debug OBJ_DIR=build/obj-debug run

clean:
	rm -rf build/obj build/obj-debug build/obj-x86_64 build/isodir build/isodir-debug build/isodir-x86_64 build/tinyos.iso build/tinyos-x86_64.iso build/tinyos.img build/tinyos-disk.img build/tinyos-debug.iso build/boot-smoke.log build/boot-x86_64.log build/boot-disk.log build/boot-minimal.log tinyos.kernel tinyos-x86_64.kernel tinyos-debug.kernel

-include $(DEPFILES)

TARGET := tinyos.kernel
ISO := build/tinyos.iso
ISO_DIR := build/isodir
OBJ_DIR := build/obj
DEBUG_BOOT ?= 0
GRAPHICAL_BOOT ?= 0
GRAPHICAL_AUTOSTART ?= 0
TERMINAL_ONLY ?= 0
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

I686_CXX := $(shell command -v i686-elf-g++ 2>/dev/null)
CLANG_CXX := $(shell command -v clang++ 2>/dev/null)
LLD := $(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null)
CLANG_RT_BUILTINS := $(shell ls /usr/lib/llvm-*/lib/clang/*/lib/linux/libclang_rt.builtins-i386.a 2>/dev/null | head -n 1)

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
	ARCH_CXXFLAGS ?=
	ARCH_LDFLAGS ?=
	LIBS ?= -lgcc
	LINKER_CHECK :=
	LINK = $(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)
else
	ARCH_CXXFLAGS ?= --target=i686-elf
	ARCH_LDFLAGS ?=
	LIBS ?=
	LINKER_CHECK := lld
	LINK = ld.lld -T build/linker.ld -o $@ $(OBJECTS) $(CLANG_RT_BUILTINS)
endif

ifeq ($(origin AS), default)
	AS := nasm
endif

ifeq ($(shell command -v grub-mkrescue 2>/dev/null),)
	GRUBMKRESCUE ?= grub2-mkrescue
else
	GRUBMKRESCUE ?= grub-mkrescue
endif

QEMU ?= qemu-system-i386
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

CXXFLAGS := -std=gnu++17 -ffreestanding -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-stack-protector -fno-pic -fno-pie -mno-mmx -mno-sse -mno-sse2 -Wall -Wextra -O2 -Iinclude $(ARCH_CXXFLAGS)
ASFLAGS := -f elf32
LDFLAGS := -T build/linker.ld -ffreestanding -O2 -nostdlib -no-pie $(ARCH_LDFLAGS)

ifeq ($(DEBUG_BOOT),1)
	CXXFLAGS += -DTINYOS_DEBUG_BOOT
endif

ifeq ($(GRAPHICAL_BOOT),1)
	CXXFLAGS += -DTINYOS_GRAPHICAL_BOOT
	ASFLAGS += -DTINYOS_GRAPHICAL_BOOT=1
endif

ifeq ($(GRAPHICAL_AUTOSTART),1)
	CXXFLAGS += -DTINYOS_GRAPHICAL_AUTOSTART
endif

ifeq ($(GFX_TERM_AUTOSTART),1)
	CXXFLAGS += -DTINYOS_GFX_TERM_AUTOSTART
endif

ifeq ($(TERMINAL_ONLY),1)
	CXXFLAGS += -DTINYOS_TERMINAL_ONLY
endif

CPP_SOURCES := \
	arch/i686/arch.cpp \
	arch/i686/context.cpp \
	arch/i686/gdt.cpp \
    arch/i686/interrupts.cpp \
	arch/i686/io.cpp \
	arch/i686/pci.cpp \
	api/system_api.cpp \
   core/memory.cpp \
	core/string.cpp \
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
	shell/completion.cpp \
	shell/shell.cpp \
	kernel/kernel.cpp

DESKTOP_CPP_SOURCES := \
	drivers/mouse.cpp \
	ui/cursor.cpp \
	ui/desktop.cpp \
	ui/font_logo.cpp \
	ui/graphical_desktop.cpp \
	ui/gfx_terminal.cpp \
	ui/gfx_input.cpp \
	ui/gfx_theme.cpp \
	ui/gfx_console.cpp \
	ui/gfx_anim.cpp \
	ui/gfx_picker.cpp \
	ui/window_manager.cpp

ifneq ($(TERMINAL_ONLY),1)
	CPP_SOURCES += $(DESKTOP_CPP_SOURCES)
endif

ASM_SOURCES := \
  boot/multiboot.asm \
    arch/i686/interrupt_stubs.asm \
    arch/i686/context_switch.asm \
    arch/i686/gdt_flush.asm \
    arch/i686/syscall_stub.asm

OBJECTS := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o) $(ASM_SOURCES:%.asm=$(OBJ_DIR)/%.o)
DEPFILES := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.d)

DISK_IMAGE ?= build/tinyos-disk.img
BOOT_DISK_IMAGE ?= build/tinyos.img
DISK_SECTORS ?= 8192
VIRTIO_BOOT_TEST_LOG ?= build/boot-virtio.log
DISK_BOOT_TEST_LOG ?= build/boot-disk.log
QEMU_VIRTIO_ARGS = -drive file=$(DISK_IMAGE),format=raw,if=none,id=disk0 -device virtio-blk-pci,drive=disk0
QEMU_DISK_BOOT_ARGS = -drive file=$(BOOT_DISK_IMAGE),format=raw,if=ide,index=0,media=disk -boot order=c

.PHONY: all iso terminal-only-iso run run-gui run-framebuffer-preview run-headless image-plan provision-plan install-plan image-profile-check install-profile-check image-app-check image-deploy-check-test tapp-pack tapp-verify tapp-sign-test tapp-trust-test image-build disk-image virtio-disk-image ata-disk-image boot-disk-image test-boot test-virtio-block test-ata-block test-disk-boot test-terminal-boot test-existing-iso test-gui-boot test-minimal test-minimal-probe test-lowmem-probe test-terminal-lowmem-probe test-stability test-gate debug-boot debug-run check-build-tools check-image-tools check-qemu-tools check-test-tools prepare-test-env dev-help clean

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
	cp build/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	cp build/initrd/initrd-placeholder.txt $(ISO_DIR)/boot/modules/initrd-placeholder.txt
	$(GRUBMKRESCUE) -o $(ISO) $(ISO_DIR)

terminal-only-iso:
	$(MAKE) TERMINAL_ONLY=1 TARGET=tinyos-terminal.kernel ISO=build/tinyos-terminal.iso ISO_DIR=build/isodir-terminal OBJ_DIR=build/obj-terminal iso

run: check-test-tools iso
	$(QEMU) -cdrom $(ISO)

run-gui: run

run-framebuffer-preview:
	$(MAKE) GRAPHICAL_BOOT=1 GRAPHICAL_AUTOSTART=1 TARGET=tinyos-desktop.kernel ISO=build/tinyos-desktop.iso ISO_DIR=build/isodir-desktop OBJ_DIR=build/obj-gui-autostart run

run-gfxterm: check-test-tools iso
	$(QEMU) -cdrom $(ISO) -vga std -m 64M

run-gfxterm-autostart:
	$(MAKE) GFX_TERM_AUTOSTART=1 iso run-gfxterm

test-gfxterm-boot: check-test-tools iso
	@mkdir -p build
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -cdrom $(ISO) -vga std -m 64M -display none -serial file:build/boot-gfxterm.log -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -eq 124 ]; then \
		echo "GFX terminal boot smoke test passed (timeout reached)."; \
	elif [ $$status -ne 0 ]; then \
		echo "GFX terminal boot smoke test failed: QEMU exited before timeout with status $$status."; \
		exit $$status; \
	else \
		echo "GFX terminal boot smoke test failed: QEMU exited early."; \
		exit 1; \
	fi

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
	if ! grep -q "Window manager scaffold ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: window manager marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Desktop shell prototype ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: desktop shell marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Fullscreen desktop mode ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: fullscreen desktop marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Desktop input interactions ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: desktop input marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Linear framebuffer boot contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: linear framebuffer marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Pixel renderer contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: pixel renderer marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Cursor scaffold ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: cursor marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Terminal style contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: terminal style marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Scheduler round-robin policy ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: scheduler round-robin marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Scheduler sleep wake contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: scheduler sleep/wake marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Runtime paging policy self-test ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: runtime paging policy marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Syscall scheduling primitives ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: syscall scheduling marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Initial process contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: initial process marker not found."; \
		exit 1; \
	fi; \
	if grep -q "TinyOS booted successfully" $(BOOT_TEST_LOG); then \
		if grep -q "PIT IRQ0 stable at 100 Hz" $(BOOT_TEST_LOG) && grep -q "Keyboard IRQ1 enabled with polling fallback" $(BOOT_TEST_LOG) && grep -q "Kernel task stack ownership scaffold ready" $(BOOT_TEST_LOG) && grep -q "Kernel task stack guard pages ready" $(BOOT_TEST_LOG) && grep -q "i686 context switch active" $(BOOT_TEST_LOG) && grep -q "Active context switch validation passed" $(BOOT_TEST_LOG) && grep -q "Task watchdog diagnostics ready" $(BOOT_TEST_LOG) && grep -q "IRQ preemption active on PIT time slices" $(BOOT_TEST_LOG) && grep -q "Scheduler scaffold receiving PIT ticks" $(BOOT_TEST_LOG) && grep -q "Boot module metadata validated" $(BOOT_TEST_LOG) && grep -q "ELF loader validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "RAMFS file tools scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall argument validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall boundary policy contract ready" $(BOOT_TEST_LOG) && grep -q "Syscall definition table ready" $(BOOT_TEST_LOG) && grep -q "Syscall filter policy ready" $(BOOT_TEST_LOG) && grep -q "Syscall resource limit policy ready" $(BOOT_TEST_LOG) && grep -q "Language runtime manifest ready" $(BOOT_TEST_LOG) && grep -q "Application capability profile scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP trust store scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package verification scaffold ready" $(BOOT_TEST_LOG) && grep -q "Application launch policy scaffold ready" $(BOOT_TEST_LOG) && grep -q "System management tools manifest ready" $(BOOT_TEST_LOG) && grep -q "Secure image provisioning manifest ready" $(BOOT_TEST_LOG) && grep -q "Device registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block device scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block VFS mount scaffold ready" $(BOOT_TEST_LOG) && grep -q "Framebuffer surface scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer primitive scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal UI scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal panel scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget event bridge ready" $(BOOT_TEST_LOG) && grep -q "UI event queue scaffold ready" $(BOOT_TEST_LOG) && grep -q "System requirements manifest ready" $(BOOT_TEST_LOG) && grep -q "Platform compatibility manifest ready" $(BOOT_TEST_LOG) && grep -q "PC platform initialization contract ready" $(BOOT_TEST_LOG) && grep -q "PC required device classes ready" $(BOOT_TEST_LOG) && grep -q "Device RAMFS metadata scaffold ready" $(BOOT_TEST_LOG) && grep -q "Architecture capability manifest ready" $(BOOT_TEST_LOG) && grep -q "Address space scaffold ready" $(BOOT_TEST_LOG) && grep -q "Address space protection flag scaffold ready" $(BOOT_TEST_LOG) && grep -q "Kernel section protection contract ready" $(BOOT_TEST_LOG) && grep -q "Boot module address-space regions ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy gap diagnostics ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy applied to bootstrap tables" $(BOOT_TEST_LOG) && grep -q "Runtime paging enabled with protected bootstrap map" $(BOOT_TEST_LOG) && grep -q "Paging structures prepared for bootstrap identity map" $(BOOT_TEST_LOG) && grep -q "Paging protection flag scaffold ready" $(BOOT_TEST_LOG); then \
			echo "Boot smoke test passed. Log: $(BOOT_TEST_LOG)"; \
		else \
			cat $(BOOT_TEST_LOG); \
			echo "Boot smoke test failed: required runtime markers not found."; \
			exit 1; \
		fi; \
	else \
		cat $(BOOT_TEST_LOG); \
		echo "Boot smoke test failed: success marker not found."; \
		exit 1; \
	fi

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
	if ! grep -q "Window manager scaffold ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: window manager marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Desktop shell prototype ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: desktop shell marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Fullscreen desktop mode ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: fullscreen desktop marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Desktop input interactions ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: desktop input marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Linear framebuffer boot contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: linear framebuffer marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Pixel renderer contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: pixel renderer marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Cursor scaffold ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: cursor marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Terminal style contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: terminal style marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Scheduler round-robin policy ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: scheduler round-robin marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Scheduler sleep wake contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: scheduler sleep/wake marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Runtime paging policy self-test ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: runtime paging policy marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Syscall scheduling primitives ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: syscall scheduling marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Initial process contract ready" $(BOOT_TEST_LOG); then \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: initial process marker not found."; \
		exit 1; \
	fi; \
	if grep -q "TinyOS booted successfully" $(BOOT_TEST_LOG); then \
		if grep -q "PIT IRQ0 stable at 100 Hz" $(BOOT_TEST_LOG) && grep -q "Keyboard IRQ1 enabled with polling fallback" $(BOOT_TEST_LOG) && grep -q "Kernel task stack ownership scaffold ready" $(BOOT_TEST_LOG) && grep -q "Kernel task stack guard pages ready" $(BOOT_TEST_LOG) && grep -q "i686 context switch active" $(BOOT_TEST_LOG) && grep -q "Active context switch validation passed" $(BOOT_TEST_LOG) && grep -q "Task watchdog diagnostics ready" $(BOOT_TEST_LOG) && grep -q "IRQ preemption active on PIT time slices" $(BOOT_TEST_LOG) && grep -q "Scheduler scaffold receiving PIT ticks" $(BOOT_TEST_LOG) && grep -q "Boot module metadata validated" $(BOOT_TEST_LOG) && grep -q "ELF loader validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "RAMFS file tools scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall argument validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall boundary policy contract ready" $(BOOT_TEST_LOG) && grep -q "Syscall definition table ready" $(BOOT_TEST_LOG) && grep -q "Syscall filter policy ready" $(BOOT_TEST_LOG) && grep -q "Syscall resource limit policy ready" $(BOOT_TEST_LOG) && grep -q "Language runtime manifest ready" $(BOOT_TEST_LOG) && grep -q "Application capability profile scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP trust store scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package verification scaffold ready" $(BOOT_TEST_LOG) && grep -q "Application launch policy scaffold ready" $(BOOT_TEST_LOG) && grep -q "System management tools manifest ready" $(BOOT_TEST_LOG) && grep -q "Secure image provisioning manifest ready" $(BOOT_TEST_LOG) && grep -q "Device registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block device scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block VFS mount scaffold ready" $(BOOT_TEST_LOG) && grep -q "Framebuffer surface scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer primitive scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal UI scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal panel scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget event bridge ready" $(BOOT_TEST_LOG) && grep -q "UI event queue scaffold ready" $(BOOT_TEST_LOG) && grep -q "System requirements manifest ready" $(BOOT_TEST_LOG) && grep -q "Platform compatibility manifest ready" $(BOOT_TEST_LOG) && grep -q "PC platform initialization contract ready" $(BOOT_TEST_LOG) && grep -q "PC required device classes ready" $(BOOT_TEST_LOG) && grep -q "Device RAMFS metadata scaffold ready" $(BOOT_TEST_LOG) && grep -q "Architecture capability manifest ready" $(BOOT_TEST_LOG) && grep -q "Address space scaffold ready" $(BOOT_TEST_LOG) && grep -q "Address space protection flag scaffold ready" $(BOOT_TEST_LOG) && grep -q "Kernel section protection contract ready" $(BOOT_TEST_LOG) && grep -q "Boot module address-space regions ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy gap diagnostics ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy applied to bootstrap tables" $(BOOT_TEST_LOG) && grep -q "Runtime paging enabled with protected bootstrap map" $(BOOT_TEST_LOG) && grep -q "Paging structures prepared for bootstrap identity map" $(BOOT_TEST_LOG) && grep -q "Paging protection flag scaffold ready" $(BOOT_TEST_LOG); then \
			echo "Existing ISO boot smoke test passed. Log: $(BOOT_TEST_LOG)"; \
		else \
			cat $(BOOT_TEST_LOG); \
			echo "Existing ISO boot smoke test failed: required runtime markers not found."; \
			exit 1; \
		fi; \
	else \
		cat $(BOOT_TEST_LOG); \
		echo "Existing ISO boot smoke test failed: success marker not found."; \
		exit 1; \
	fi

test-gui-boot:
	$(MAKE) GRAPHICAL_BOOT=1 TARGET=tinyos-gui.kernel ISO=build/tinyos-gui.iso ISO_DIR=build/isodir-gui OBJ_DIR=build/obj-gui BOOT_TEST_LOG=build/boot-gui.log test-boot
	@if grep -q "Graphical desktop optional mode ready" build/boot-gui.log; then \
		echo "Graphical boot smoke test passed. Log: build/boot-gui.log"; \
	else \
		cat build/boot-gui.log; \
		echo "Graphical boot smoke test failed: graphical desktop optional marker not found."; \
		exit 1; \
	fi

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
	if ! grep -q "Terminal style contract ready" $(MINIMAL_TEST_LOG); then \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: terminal style marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Scheduler round-robin policy ready" $(MINIMAL_TEST_LOG); then \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: scheduler round-robin marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Scheduler sleep wake contract ready" $(MINIMAL_TEST_LOG); then \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: scheduler sleep/wake marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Runtime paging policy self-test ready" $(MINIMAL_TEST_LOG); then \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: runtime paging policy marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Syscall scheduling primitives ready" $(MINIMAL_TEST_LOG); then \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: syscall scheduling marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Initial process contract ready" $(MINIMAL_TEST_LOG); then \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: initial process marker not found."; \
		exit 1; \
	fi; \
	if grep -q "TinyOS booted successfully" $(MINIMAL_TEST_LOG) && grep -q "System requirements manifest ready" $(MINIMAL_TEST_LOG) && grep -q "Language runtime manifest ready" $(MINIMAL_TEST_LOG) && grep -q "Application capability profile scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "TAPP package registry scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "TAPP trust store scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "TAPP package verification scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Application launch policy scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "System management tools manifest ready" $(MINIMAL_TEST_LOG) && grep -q "Secure image provisioning manifest ready" $(MINIMAL_TEST_LOG) && grep -q "Linear framebuffer boot contract ready" $(MINIMAL_TEST_LOG) && grep -q "Pixel renderer contract ready" $(MINIMAL_TEST_LOG) && grep -q "Cursor scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Renderer scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Renderer primitive scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Terminal UI scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Terminal panel scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "TUI widget scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Window manager scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Desktop shell prototype ready" $(MINIMAL_TEST_LOG) && grep -q "Fullscreen desktop mode ready" $(MINIMAL_TEST_LOG) && grep -q "Desktop input interactions ready" $(MINIMAL_TEST_LOG) && grep -q "TUI widget event bridge ready" $(MINIMAL_TEST_LOG) && grep -q "UI event queue scaffold ready" $(MINIMAL_TEST_LOG) && grep -q "Block VFS mount scaffold ready" $(MINIMAL_TEST_LOG); then \
		echo "Minimal requirement test passed. Log: $(MINIMAL_TEST_LOG)"; \
	else \
		cat $(MINIMAL_TEST_LOG); \
		echo "Minimal requirement test failed: required markers not found."; \
		exit 1; \
	fi

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

test-terminal-lowmem-probe:
	$(MAKE) TERMINAL_ONLY=1 TARGET=tinyos-terminal.kernel ISO=build/tinyos-terminal.iso ISO_DIR=build/isodir-terminal OBJ_DIR=build/obj-terminal LOWMEM_PROBE_MEMORY="$(LOWMEM_PROBE_MEMORY)" LOWMEM_PROBE_SUMMARY=build/terminal-lowmem-probe-summary.txt LOWMEM_PROBE_LABEL="TinyOS terminal-only boot" LOWMEM_PROBE_EXTRA_MARKER="Terminal-only low-memory profile ready" test-lowmem-probe

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
	$(MAKE) test-stability
	$(MAKE) test-terminal-boot
	$(MAKE) test-disk-boot
	$(MAKE) install-profile-check
	$(MAKE) tapp-trust-test
	@echo "Change-scope gate passed. Run manual shell checks: syscheck, securityinfo, integritycheck (see docs/testing.md)."

test-terminal-boot: check-test-tools terminal-only-iso
	@mkdir -p build
	@rm -f build/boot-terminal-smoke.log
	@echo "Running TinyOS terminal-only boot smoke test for $(BOOT_TEST_TIMEOUT)..."
	@set +e; $(TIMEOUT) $(BOOT_TEST_TIMEOUT) $(QEMU) -cdrom build/tinyos-terminal.iso -display none -serial file:build/boot-terminal-smoke.log -no-reboot -no-shutdown >/dev/null 2>&1; status=$$?; \
	if [ $$status -ne 124 ]; then \
		cat build/boot-terminal-smoke.log; \
		echo "Terminal boot smoke test failed: QEMU exited before timeout with status $$status."; \
		exit 1; \
	fi; \
	if ! grep -q "TinyOS booted successfully" build/boot-terminal-smoke.log; then \
		cat build/boot-terminal-smoke.log; \
		echo "Terminal boot smoke test failed: success marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "i686 context switch active" build/boot-terminal-smoke.log; then \
		cat build/boot-terminal-smoke.log; \
		echo "Terminal boot smoke test failed: context switch marker not found."; \
		exit 1; \
	fi; \
	if ! grep -q "Terminal-only low-memory profile ready" build/boot-terminal-smoke.log; then \
		cat build/boot-terminal-smoke.log; \
		echo "Terminal boot smoke test failed: terminal-only marker not found."; \
		exit 1; \
	fi; \
	echo "Terminal boot smoke test passed. Log: build/boot-terminal-smoke.log"

debug-boot:
	$(MAKE) DEBUG_BOOT=1 TARGET=tinyos-debug.kernel ISO=build/tinyos-debug.iso ISO_DIR=build/isodir-debug OBJ_DIR=build/obj-debug iso

debug-run:
	$(MAKE) DEBUG_BOOT=1 TARGET=tinyos-debug.kernel ISO=build/tinyos-debug.iso ISO_DIR=build/isodir-debug OBJ_DIR=build/obj-debug run

clean:
	rm -rf build/obj build/obj-debug build/obj-gui build/obj-gui-autostart build/obj-terminal build/isodir build/isodir-debug build/isodir-gui build/isodir-desktop build/isodir-terminal build/tinyos.iso build/tinyos.img build/tinyos-disk.img build/tinyos-debug.iso build/tinyos-gui.iso build/tinyos-desktop.iso build/tinyos-terminal.iso build/boot-smoke.log build/boot-disk.log build/boot-minimal.log build/boot-gui.log tinyos.kernel tinyos-debug.kernel tinyos-gui.kernel tinyos-desktop.kernel tinyos-terminal.kernel

-include $(DEPFILES)

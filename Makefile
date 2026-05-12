TARGET := tinyos.kernel
ISO := build/tinyos.iso
ISO_DIR := build/isodir
OBJ_DIR := build/obj
DEBUG_BOOT ?= 0
GRAPHICAL_BOOT ?= 0
GRAPHICAL_AUTOSTART ?= 0
BOOT_TEST_TIMEOUT ?= 8s
STABILITY_TEST_TIMEOUT ?= 20s
MINIMAL_TEST_MEMORY ?= 32M
MINIMAL_PROBE_MEMORY ?= 32M 24M 16M
BOOT_TEST_LOG ?= build/boot-smoke.log
MINIMAL_TEST_LOG ?= build/boot-minimal.log

I686_CXX := $(shell command -v i686-elf-g++ 2>/dev/null)

ifeq ($(origin CXX), default)
ifneq ($(I686_CXX),)
	CXX := i686-elf-g++
else
	CXX := clang++
endif
endif

ifneq ($(findstring i686-elf-g++,$(notdir $(CXX))),)
	ARCH_CXXFLAGS ?=
	ARCH_LDFLAGS ?=
	LIBS ?= -lgcc
else
	ARCH_CXXFLAGS ?= --target=i686-elf
	ARCH_LDFLAGS ?= -fuse-ld=lld --target=i686-elf
	LIBS ?=
	EXTRA_BUILD_TOOLS := ld.lld
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

REQUIRED_BUILD_TOOLS := $(CXX) $(AS) $(EXTRA_BUILD_TOOLS)
REQUIRED_IMAGE_TOOLS := $(REQUIRED_BUILD_TOOLS) $(GRUBMKRESCUE) $(XORRISO)
REQUIRED_QEMU_TOOLS := $(QEMU) $(TIMEOUT)
REQUIRED_TEST_TOOLS := $(REQUIRED_IMAGE_TOOLS) $(REQUIRED_QEMU_TOOLS)

CXXFLAGS := -std=gnu++17 -ffreestanding -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-stack-protector -mno-mmx -mno-sse -mno-sse2 -Wall -Wextra -O2 -Iinclude $(ARCH_CXXFLAGS)
ASFLAGS := -f elf32
LDFLAGS := -T build/linker.ld -ffreestanding -O2 -nostdlib $(ARCH_LDFLAGS)

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

CPP_SOURCES := \
	arch/i686/arch.cpp \
	arch/i686/context.cpp \
    arch/i686/interrupts.cpp \
	arch/i686/io.cpp \
	api/system_api.cpp \
   core/memory.cpp \
	core/string.cpp \
    drivers/input.cpp \
   drivers/pic.cpp \
	drivers/pit.cpp \
 drivers/serial.cpp \
	drivers/vga.cpp \
	drivers/keyboard.cpp \
	drivers/mouse.cpp \
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
  kernel/syscall/syscall.cpp \
	kernel/task/task.cpp \
 kernel/user/transition.cpp \
 kernel/vfs/blockfs.cpp \
 kernel/vfs/ramfs.cpp \
   kernel/vfs/vfs.cpp \
	ui/renderer.cpp \
	ui/events.cpp \
	ui/terminal.cpp \
	ui/cursor.cpp \
	ui/desktop.cpp \
	ui/graphical_desktop.cpp \
	ui/window_manager.cpp \
	ui/widgets.cpp \
	shell/shell.cpp \
	kernel/kernel.cpp

ASM_SOURCES := \
  boot/multiboot.asm \
    arch/i686/interrupt_stubs.asm

OBJECTS := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o) $(ASM_SOURCES:%.asm=$(OBJ_DIR)/%.o)
DEPFILES := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.d)

define require_tools
	@missing=0; \
	for tool in $(1); do \
		if command -v $$tool >/dev/null 2>&1; then \
			echo "found: $$tool ($$(command -v $$tool))"; \
		else \
			echo "missing: $$tool"; \
			missing=1; \
		fi; \
	done; \
	if [ $$missing -ne 0 ]; then \
		echo "Install the missing tools before continuing."; \
		exit 1; \
	fi
endef

.PHONY: all iso run run-gui run-framebuffer-preview run-headless image-plan provision-plan image-profile-check image-app-check image-deploy-check-test tapp-pack tapp-verify tapp-sign-test tapp-trust-test image-build test-boot test-existing-iso test-gui-boot test-minimal test-minimal-probe test-stability debug-boot debug-run check-build-tools check-image-tools check-qemu-tools check-test-tools prepare-test-env clean

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

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

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

run: check-test-tools iso
	$(QEMU) -cdrom $(ISO)

run-gui: run

run-framebuffer-preview:
	$(MAKE) GRAPHICAL_BOOT=1 GRAPHICAL_AUTOSTART=1 TARGET=tinyos-desktop.kernel ISO=build/tinyos-desktop.iso ISO_DIR=build/isodir-desktop OBJ_DIR=build/obj-gui-autostart run

run-headless: check-test-tools iso
	$(QEMU) -cdrom $(ISO) -serial stdio -display none

image-plan:
	bash scripts/tinyos-image.sh plan

provision-plan:
	bash scripts/tinyos-image.sh provision-plan

image-profile-check:
	bash scripts/tinyos-image.sh check-profile examples/system.profile

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
	if grep -q "TinyOS booted successfully" $(BOOT_TEST_LOG); then \
		if grep -q "PIT IRQ0 stable at 100 Hz" $(BOOT_TEST_LOG) && grep -q "Keyboard IRQ1 enabled with polling fallback" $(BOOT_TEST_LOG) && grep -q "Kernel task stack ownership scaffold ready" $(BOOT_TEST_LOG) && grep -q "i686 context switch ABI scaffold ready" $(BOOT_TEST_LOG) && grep -q "Scheduler scaffold receiving PIT ticks" $(BOOT_TEST_LOG) && grep -q "Boot module metadata validated" $(BOOT_TEST_LOG) && grep -q "ELF loader validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "RAMFS file tools scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall argument validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall boundary policy contract ready" $(BOOT_TEST_LOG) && grep -q "Syscall definition table ready" $(BOOT_TEST_LOG) && grep -q "Syscall filter policy ready" $(BOOT_TEST_LOG) && grep -q "Syscall resource limit policy ready" $(BOOT_TEST_LOG) && grep -q "Language runtime manifest ready" $(BOOT_TEST_LOG) && grep -q "Application capability profile scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP trust store scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package verification scaffold ready" $(BOOT_TEST_LOG) && grep -q "Application launch policy scaffold ready" $(BOOT_TEST_LOG) && grep -q "System management tools manifest ready" $(BOOT_TEST_LOG) && grep -q "Secure image provisioning manifest ready" $(BOOT_TEST_LOG) && grep -q "Device registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block device scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block VFS mount scaffold ready" $(BOOT_TEST_LOG) && grep -q "Framebuffer surface scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer primitive scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal UI scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal panel scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget event bridge ready" $(BOOT_TEST_LOG) && grep -q "UI event queue scaffold ready" $(BOOT_TEST_LOG) && grep -q "System requirements manifest ready" $(BOOT_TEST_LOG) && grep -q "Platform compatibility manifest ready" $(BOOT_TEST_LOG) && grep -q "PC platform initialization contract ready" $(BOOT_TEST_LOG) && grep -q "PC required device classes ready" $(BOOT_TEST_LOG) && grep -q "Device RAMFS metadata scaffold ready" $(BOOT_TEST_LOG) && grep -q "Architecture capability manifest ready" $(BOOT_TEST_LOG) && grep -q "Address space scaffold ready" $(BOOT_TEST_LOG) && grep -q "Address space protection flag scaffold ready" $(BOOT_TEST_LOG) && grep -q "Kernel section protection contract ready" $(BOOT_TEST_LOG) && grep -q "Boot module address-space regions ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy gap diagnostics ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy applied to bootstrap tables" $(BOOT_TEST_LOG) && grep -q "Runtime paging enabled with protected bootstrap map" $(BOOT_TEST_LOG) && grep -q "Paging structures prepared for bootstrap identity map" $(BOOT_TEST_LOG) && grep -q "Paging protection flag scaffold ready" $(BOOT_TEST_LOG); then \
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
	if grep -q "TinyOS booted successfully" $(BOOT_TEST_LOG); then \
		if grep -q "PIT IRQ0 stable at 100 Hz" $(BOOT_TEST_LOG) && grep -q "Keyboard IRQ1 enabled with polling fallback" $(BOOT_TEST_LOG) && grep -q "Kernel task stack ownership scaffold ready" $(BOOT_TEST_LOG) && grep -q "i686 context switch ABI scaffold ready" $(BOOT_TEST_LOG) && grep -q "Scheduler scaffold receiving PIT ticks" $(BOOT_TEST_LOG) && grep -q "Boot module metadata validated" $(BOOT_TEST_LOG) && grep -q "ELF loader validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "RAMFS file tools scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall argument validation scaffold ready" $(BOOT_TEST_LOG) && grep -q "Syscall boundary policy contract ready" $(BOOT_TEST_LOG) && grep -q "Syscall definition table ready" $(BOOT_TEST_LOG) && grep -q "Syscall filter policy ready" $(BOOT_TEST_LOG) && grep -q "Syscall resource limit policy ready" $(BOOT_TEST_LOG) && grep -q "Language runtime manifest ready" $(BOOT_TEST_LOG) && grep -q "Application capability profile scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP trust store scaffold ready" $(BOOT_TEST_LOG) && grep -q "TAPP package verification scaffold ready" $(BOOT_TEST_LOG) && grep -q "Application launch policy scaffold ready" $(BOOT_TEST_LOG) && grep -q "System management tools manifest ready" $(BOOT_TEST_LOG) && grep -q "Secure image provisioning manifest ready" $(BOOT_TEST_LOG) && grep -q "Device registry scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block device scaffold ready" $(BOOT_TEST_LOG) && grep -q "Block VFS mount scaffold ready" $(BOOT_TEST_LOG) && grep -q "Framebuffer surface scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer scaffold ready" $(BOOT_TEST_LOG) && grep -q "Renderer primitive scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal UI scaffold ready" $(BOOT_TEST_LOG) && grep -q "Terminal panel scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget scaffold ready" $(BOOT_TEST_LOG) && grep -q "TUI widget event bridge ready" $(BOOT_TEST_LOG) && grep -q "UI event queue scaffold ready" $(BOOT_TEST_LOG) && grep -q "System requirements manifest ready" $(BOOT_TEST_LOG) && grep -q "Platform compatibility manifest ready" $(BOOT_TEST_LOG) && grep -q "PC platform initialization contract ready" $(BOOT_TEST_LOG) && grep -q "PC required device classes ready" $(BOOT_TEST_LOG) && grep -q "Device RAMFS metadata scaffold ready" $(BOOT_TEST_LOG) && grep -q "Architecture capability manifest ready" $(BOOT_TEST_LOG) && grep -q "Address space scaffold ready" $(BOOT_TEST_LOG) && grep -q "Address space protection flag scaffold ready" $(BOOT_TEST_LOG) && grep -q "Kernel section protection contract ready" $(BOOT_TEST_LOG) && grep -q "Boot module address-space regions ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy gap diagnostics ready" $(BOOT_TEST_LOG) && grep -q "Address-space paging policy applied to bootstrap tables" $(BOOT_TEST_LOG) && grep -q "Runtime paging enabled with protected bootstrap map" $(BOOT_TEST_LOG) && grep -q "Paging structures prepared for bootstrap identity map" $(BOOT_TEST_LOG) && grep -q "Paging protection flag scaffold ready" $(BOOT_TEST_LOG); then \
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

test-stability: BOOT_TEST_TIMEOUT = $(STABILITY_TEST_TIMEOUT)
test-stability: test-boot

debug-boot:
	$(MAKE) DEBUG_BOOT=1 TARGET=tinyos-debug.kernel ISO=build/tinyos-debug.iso ISO_DIR=build/isodir-debug OBJ_DIR=build/obj-debug iso

debug-run:
	$(MAKE) DEBUG_BOOT=1 TARGET=tinyos-debug.kernel ISO=build/tinyos-debug.iso ISO_DIR=build/isodir-debug OBJ_DIR=build/obj-debug run

clean:
	rm -rf build/obj build/obj-debug build/obj-gui build/obj-gui-autostart build/isodir build/isodir-debug build/isodir-gui build/isodir-desktop build/tinyos.iso build/tinyos-debug.iso build/tinyos-gui.iso build/tinyos-desktop.iso build/boot-smoke.log build/boot-minimal.log build/boot-gui.log tinyos.kernel tinyos-debug.kernel tinyos-gui.kernel tinyos-desktop.kernel

-include $(DEPFILES)

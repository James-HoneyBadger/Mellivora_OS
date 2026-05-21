#
# Mellivora OS - Build System
#
# Produces a bootable disk image with:
#   Sector 0:      Stage 1 boot sector (512 bytes)
#   Sectors 1-32:  Stage 2 loader (16 KB)
#   Sectors 33+:   Kernel (192 KB)
#   Remaining:     Filesystem area
#
# Target: i486+ emulation via QEMU
#
# Parallel program compilation is enabled automatically when make -j is
# not already set.  Override with:  make programs NPROC=1
#

NASM = $(if $(wildcard /usr/bin/flatpak-spawn),nasm,$(if $(shell command -v nasm 2>/dev/null),nasm,flatpak run --command=nasm com.visualstudio.code))
QEMU = $(if $(wildcard /usr/bin/flatpak-spawn),/usr/bin/flatpak-spawn --host qemu-system-x86_64,qemu-system-x86_64)
DD = dd
UNAME_S := $(shell uname -s)
# Phase 3.1: portable parallelism detection — try Linux nproc, then BSD/macOS
# sysctl, then POSIX getconf, then a safe default of 4 cores.
NPROC ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

# -----------------------------------------------------------------------
# v10.0: C toolchain (for tools/hbpkg.c and boot/uefi_loader.c)
# -----------------------------------------------------------------------
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L

# UEFI cross-toolchain (requires gnu-efi and x86_64 cross-compiler)
# Override these on your distro, e.g.:
#   make uefi CC_EFI=x86_64-w64-mingw32-gcc EFI_INCLUDE=/usr/include/efi
CC_EFI     ?= x86_64-linux-gnu-gcc
LD_EFI     ?= x86_64-linux-gnu-ld
OBJCOPY    ?= x86_64-linux-gnu-objcopy
EFI_INCLUDE ?= /usr/include/efi
EFI_LIB    ?= /usr/lib/x86_64-linux-gnu/gnuefi
OVMF_CODE  ?= /usr/share/OVMF/OVMF_CODE.fd
OVMF_VARS  ?= /usr/share/OVMF/OVMF_VARS.fd

# Build artefacts
UEFI_EFI   = boot/uefi_loader.efi
UEFI_SO    = boot/uefi_loader.so
HBPKG_BIN  = tools/hbpkg

# UEFI disk image (FAT ESP + HBFS kernel payload)
UEFI_IMAGE = mellivora-uefi.img

# Output
IMAGE = mellivora.img
IMAGE_SIZE_MB = 2048

# Components
BOOT_BIN = boot.bin
STAGE2_BIN = stage2.bin
KERNEL_BIN = kernel.bin

# Source
BOOT_SRC = boot.asm
STAGE2_SRC = stage2.asm
KERNEL_SRC = kernel.asm

# QEMU settings
# Choose a host-compatible QEMU audio backend by default.
# Override manually when needed, e.g.:
#   make run QEMU_AUDIO_BACKEND=alsa
#   make run QEMU_AUDIO_BACKEND=pa
QEMU_AUDIO_BACKEND ?= pa
ifeq ($(UNAME_S),Darwin)
QEMU_AUDIO_BACKEND = coreaudio
endif

QEMU_AUDIO_FLAGS = -audiodev $(QEMU_AUDIO_BACKEND),id=snd0 \
			 -machine pcspk-audiodev=snd0,usb=off \
			 -device sb16,audiodev=snd0

# Reboot behavior: allow guest reboot by default.
# Set QEMU_NO_REBOOT=1 to make QEMU exit on guest reset.
QEMU_RESET_FLAGS =
ifeq ($(QEMU_NO_REBOOT),1)
QEMU_RESET_FLAGS += -no-reboot
endif

QEMU_FLAGS = -cpu 486 -m 128 -drive file=$(IMAGE),format=raw,if=ide,cache=writethrough -boot c -no-shutdown $(QEMU_AUDIO_FLAGS) $(QEMU_RESET_FLAGS) -netdev user,id=net0 -device rtl8139,netdev=net0

# For debugging
QEMU_DEBUG_FLAGS = $(QEMU_FLAGS) \
                   -monitor stdio \
                   -d int,cpu_reset

# Programs
PROG_DIR = programs
PROG_SRCS = $(wildcard $(PROG_DIR)/*.asm)
PROG_BINS = $(patsubst %.asm,%.bin,$(PROG_SRCS) $(TEST_PROGS))

TEST_PROGS = programs/sbrk_test.asm programs/hello_test.asm


# ISO packaging
ISO_STAGING = .build/iso-root
ISO_FILE = mellivora.iso
ISO_LITE_FILE = mellivora-lite.iso
ISO_LITE_IMG_MB = 64
ISO_DOCS = docs/INSTALL.md docs/USER_GUIDE.md docs/PROGRAMMING_GUIDE.md \
           docs/TECHNICAL_REFERENCE.md docs/TUTORIAL.md docs/API_REFERENCE.md \
           docs/NETWORKING_GUIDE.md Experimental/docs/ISO_README.txt

# Populate script
POPULATE = python3 populate.py

.PHONY: all clean run run-iso run-serial debug programs populate full check sanitize iso iso-lite iso-verify sizes help dev count uefi run-uefi 64bit hbpkg

all: $(IMAGE)

# Assemble stage 1 boot sector
$(BOOT_BIN): $(BOOT_SRC)
	$(NASM) -f bin -o $@ -l $(@:.bin=.lst) $<

# Kernel include files (split for readability)
KERNEL_INCS = $(wildcard kernel/*.inc)

# Assemble 32-bit kernel
$(KERNEL_BIN): $(KERNEL_SRC) $(KERNEL_INCS)
	$(NASM) -f bin -O0 -w-zeroing -o $@ -l $(@:.bin=.lst) $<

# Generate kernel_sectors.inc from kernel binary size
# This computes ceil(size / 512) so stage2 loads exactly the right amount.
kernel_sectors.inc: $(KERNEL_BIN)
	@KSECTORS=$$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )); \
		if [ $$KSECTORS -gt 4062 ]; then \
			echo "  ERROR: kernel is $$KSECTORS sectors (> 4062). HBFS_SUPERBLOCK_LBA=4096"; \
			echo "  Update HBFS_SUPERBLOCK_LBA in kernel.asm AND populate.py."; \
			exit 1; \
		fi; \
		if [ $$KSECTORS -gt 2048 ]; then \
			echo "  ERROR: kernel is $$KSECTORS sectors (> 2048 = 1 MB) \
			and will overflow the stage-2 load buffer at 0x20000."; \
			exit 1; \
		fi; \
		echo "KERNEL_SECTORS  equ $$KSECTORS" > $@
	@echo "  Kernel sectors: $$(cat $@)"

# Assemble stage 2 loader (depends on kernel_sectors.inc)
$(STAGE2_BIN): $(STAGE2_SRC) kernel_sectors.inc
	$(NASM) -f bin -o $@ -l $(@:.bin=.lst) $<

# Create the disk image
# Layout:
#   Offset 0x00000 (LBA 0):  boot.bin   (512 bytes)
#   Offset 0x00200 (LBA 1):  stage2.bin (16384 bytes = 32 sectors)
#   Offset 0x04200 (LBA 33): kernel.bin (padded to 192KB = 384 sectors)
$(IMAGE): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "=== Building Mellivora disk image ==="
	@echo "  Boot sector:  $(BOOT_BIN)"
	@echo "  Stage 2:      $(STAGE2_BIN)"
	@echo "  Kernel:       $(KERNEL_BIN)"
	@if [ ! -f $(IMAGE) ]; then \
		echo "  Creating empty $(IMAGE_SIZE_MB)MB disk image"; \
		$(DD) if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_SIZE_MB) status=none; \
	else \
		echo "  Preserving existing HBFS contents in $(IMAGE)"; \
	fi
	# Write boot sector at LBA 0
	$(DD) if=$(BOOT_BIN) of=$(IMAGE) bs=512 count=1 conv=notrunc status=none
	# Write stage 2 at LBA 1
	$(DD) if=$(STAGE2_BIN) of=$(IMAGE) bs=512 seek=1 conv=notrunc status=none
	# Write kernel at LBA 33
	$(DD) if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=33 conv=notrunc status=none
	@echo "=== $(IMAGE) updated ($(IMAGE_SIZE_MB) MB) ==="
	@ls -la $(IMAGE)

# Run in QEMU with i486 CPU
run: $(IMAGE)
	@echo "=== Launching Mellivora in QEMU (i486 CPU, 128MB RAM) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS)

# Run with serial console on TCP port (connect with: nc localhost 4555)
run-serial: $(IMAGE)
	@echo "=== Launching Mellivora with serial on TCP port 4555 ==="
	@echo "    Connect with:  nc localhost 4555"
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS) -serial tcp:127.0.0.1:4555,server=on,wait=off

# Boot the ISO in QEMU: CD-ROM provides El Torito boot, IDE disk provides HBFS.
run-iso: iso
	@echo "=== Launching Mellivora from ISO (El Torito CD boot + IDE disk) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_FILE) -boot d

# Run with debug output
debug: $(IMAGE)
	@echo "=== Launching Mellivora in QEMU (DEBUG MODE) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_DEBUG_FLAGS)

# Build all sample programs (parallel by default)
programs:
	$(MAKE) -j$(NPROC) $(PROG_BINS)

$(PROG_DIR)/%.bin: $(PROG_DIR)/%.asm $(PROG_DIR)/syscalls.inc $(wildcard $(PROG_DIR)/lib/*.inc)
	$(NASM) -f bin -I$(PROG_DIR)/ -o $@ -l $(@:.bin=.lst) $<

# Populate disk image with files and programs
populate: $(IMAGE) programs populate.py
	@echo "=== Populating filesystem ==="
	$(POPULATE) $(IMAGE) $(PROG_DIR)

# Full build: OS + programs + populated filesystem
full: $(IMAGE) programs
	# Always write the latest kernel before repopulating the filesystem.
	# populate.py updates mellivora.img's mtime, so the $(IMAGE) rule is
	# skipped on subsequent builds — this dd ensures kernel is always current.
	$(DD) if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=33 conv=notrunc status=none
	$(POPULATE) $(IMAGE) $(PROG_DIR)
	@echo "=== Full build complete ==="

# Rebuild kernel only and write it into the existing disk image.
# Skips program compilation and filesystem population — fastest
# iteration loop when only kernel/*.inc or kernel.asm changed.
kernel-only: $(KERNEL_BIN) kernel_sectors.inc $(STAGE2_BIN)
	@echo "=== Patching kernel into $(IMAGE) ==="
	$(DD) if=$(STAGE2_BIN) of=$(IMAGE) bs=512 seek=1  conv=notrunc status=none
	$(DD) if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=33 conv=notrunc status=none
	@echo "=== kernel-only update complete ==="

# Shared helper: copy docs into staging tree
define stage_iso_docs
	@rm -rf "$(ISO_STAGING)"
	@mkdir -p "$(ISO_STAGING)/boot" "$(ISO_STAGING)/docs"
	@cp README.md LICENSE CHANGELOG.md "$(ISO_STAGING)/"
	@cp docs/INSTALL.md docs/USER_GUIDE.md docs/PROGRAMMING_GUIDE.md \
		  docs/TECHNICAL_REFERENCE.md docs/TUTORIAL.md docs/API_REFERENCE.md \
		  docs/NETWORKING_GUIDE.md \
		  "$(ISO_STAGING)/docs/"
	@cp Experimental/docs/ISO_README.txt "$(ISO_STAGING)/README.txt"
	@chmod +x Experimental/tools/build_iso.sh
endef

# Build a bootable ISO that includes install docs and the user guide
iso: full Experimental/tools/build_iso.sh $(ISO_DOCS) README.md LICENSE CHANGELOG.md
	@echo "=== Preparing bootable ISO staging tree ==="
	$(call stage_iso_docs)
	@cp "$(IMAGE)" "$(ISO_STAGING)/boot/mellivora.img"
	@ISO_BOOT_SECTORS=$$(awk '/KERNEL_SECTORS/ {print $$3 + 33}' kernel_sectors.inc) \
		./Experimental/tools/build_iso.sh "$(ISO_STAGING)" "$(ISO_FILE)"
	@echo "=== Bootable ISO ready: $(ISO_FILE) ==="

# Build a smaller ISO with a truncated disk image (faster downloads)
iso-lite: full Experimental/tools/build_iso.sh $(ISO_DOCS) README.md LICENSE CHANGELOG.md
	@echo "=== Preparing lite ISO ($(ISO_LITE_IMG_MB) MB disk image) ==="
	$(call stage_iso_docs)
	@$(DD) if=$(IMAGE) of="$(ISO_STAGING)/boot/mellivora.img" \
		bs=1M count=$(ISO_LITE_IMG_MB) status=none
	@ISO_BOOT_SECTORS=$$(awk '/KERNEL_SECTORS/ {print $$3 + 33}' kernel_sectors.inc) \
		./Experimental/tools/build_iso.sh "$(ISO_STAGING)" "$(ISO_LITE_FILE)"
	@echo "=== Lite ISO ready: $(ISO_LITE_FILE) ==="

# Verify El Torito boot record in an existing ISO
iso-verify: $(ISO_FILE)
	@echo "=== Verifying ISO El Torito boot record ==="
	@command -v xorriso >/dev/null 2>&1 || { echo "  error: xorriso required for verification"; exit 1; }
	@xorriso -indev $(ISO_FILE) -report_el_torito plain 2>&1 \
		| grep -E 'Boot record|El Torito'
	@EXPECTED=$$(awk '/KERNEL_SECTORS/ {print $$3 + 33}' kernel_sectors.inc); \
	 ACTUAL=$$(xorriso -indev $(ISO_FILE) -report_el_torito plain 2>&1 \
		| awk '/El Torito boot img/{print $$(NF-1)}'); \
	 if [ "$$EXPECTED" = "$$ACTUAL" ]; then \
		echo "  PASS  boot-load-size: $$ACTUAL sectors (matches kernel_sectors + 33)"; \
	 else \
		echo "  WARN  boot-load-size: expected $$EXPECTED, found $$ACTUAL"; \
	 fi

# Run regression tests (requires full build)
check: full
	@bash tests/test_build.sh
	@python3 tests/test_hbfs.py

# Phase 4: nightly-style sanitize build. Defines KERNEL_DEBUG_BOUNDS so any
# compile-time bounds-check macros are enabled, then runs the regression
# suite. NASM treats unknown -D flags harmlessly so this is a no-op until
# kernel sources start guarding code with %ifdef KERNEL_DEBUG_BOUNDS.
sanitize:
	@echo "=== Sanitize build (KERNEL_DEBUG_BOUNDS=1) ==="
	$(MAKE) clean
	$(NASM) -f bin -O0 -DKERNEL_DEBUG_BOUNDS=1 \
		-o $(KERNEL_BIN) -l $(KERNEL_BIN:.bin=.lst) $(KERNEL_SRC)
	$(MAKE) full
	$(MAKE) check
	@echo "=== Sanitize run complete ==="

# Show component sizes
sizes: $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "=== Component Sizes ==="
	@echo -n "  Boot sector: " && wc -c < $(BOOT_BIN) && echo " bytes (max 512)"
	@echo -n "  Stage 2:     " && wc -c < $(STAGE2_BIN) && echo " bytes (max 16384)"
	@echo -n "  Kernel:      " && wc -c < $(KERNEL_BIN) && echo " bytes"
	@if [ -d "$(PROG_DIR)" ]; then \
		echo "=== Program Sizes ==="; \
		for f in $(PROG_DIR)/*.bin; do \
			[ -f "$$f" ] && printf "  %-20s %s bytes\n" "$$(basename $$f)" "$$(wc -c < $$f)"; \
		done; \
	fi
	@if [ -f "$(ISO_FILE)" ] || [ -f "$(ISO_LITE_FILE)" ]; then \
		echo "=== ISO Sizes ==="; \
		[ -f "$(ISO_FILE)" ] && printf "  %-20s %s\n" "$(ISO_FILE)" "$$(ls -lh $(ISO_FILE) | awk '{print $$5}')"; \
		[ -f "$(ISO_LITE_FILE)" ] && printf "  %-20s %s\n" "$(ISO_LITE_FILE)" "$$(ls -lh $(ISO_LITE_FILE) | awk '{print $$5}')"; \
	fi

# Quick dev loop: full build + run
dev: full
	$(QEMU) $(QEMU_FLAGS)

# -----------------------------------------------------------------------
# v10.0: UEFI bootloader build
# Requires: gnu-efi, x86_64-linux-gnu-gcc, x86_64-linux-gnu-ld, objcopy
# Install:  apt-get install gnu-efi gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu
# -----------------------------------------------------------------------
CFLAGS_EFI = -O2 -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
             -fshort-wchar -mno-red-zone -DGNU_EFI_USE_MS_ABI \
             -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/x86_64 -I$(EFI_INCLUDE)/protocol \
             -Wall -Wextra -std=c11

LDFLAGS_EFI = -shared -Bsymbolic -L$(EFI_LIB) \
              -T $(EFI_LIB)/elf_x86_64_efi.lds \
              $(EFI_LIB)/crt0-efi-x86_64.o

$(UEFI_SO): boot/uefi_loader.c
	@mkdir -p boot
	$(CC_EFI) $(CFLAGS_EFI) -c -o boot/uefi_loader.o $<
	$(LD_EFI) $(LDFLAGS_EFI) boot/uefi_loader.o -o $@ -lgnuefi -lefi

$(UEFI_EFI): $(UEFI_SO)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata -j .dynamic \
	           -j .dynsym -j .rel -j .rela -j .rel.* -j .rela.* \
	           -j .reloc --target=efi-app-x86_64 --subsystem=10 \
	           $< $@
	@echo "=== UEFI loader: $(UEFI_EFI) ==="
	@ls -lh $@

uefi: $(UEFI_EFI)

# Build a UEFI-bootable disk image:
#   ESP partition (FAT, 64 MB) with EFI/BOOT/BOOTX64.EFI
#   followed by the HBFS kernel payload (mellivora.img data)
$(UEFI_IMAGE): $(UEFI_EFI) $(KERNEL_BIN) programs
	@echo "=== Building UEFI disk image: $(UEFI_IMAGE) ==="
	@# Create 512 MB raw image
	$(DD) if=/dev/zero of=$(UEFI_IMAGE) bs=1M count=512 status=none
	@# Create GPT with ESP partition using sgdisk (or fdisk as fallback)
	@if command -v sgdisk >/dev/null 2>&1; then \
		sgdisk -n 1:2048:133119 -t 1:ef00 -c 1:"EFI System" \
		       -n 2:133120: -t 2:8300 -c 2:"HBFS Data" $(UEFI_IMAGE); \
	else \
		echo "  WARNING: sgdisk not found — image will lack GPT"; \
	fi
	@# Format ESP partition as FAT32 and install EFI application
	@LOOP=$$(sudo losetup -f --show -P $(UEFI_IMAGE)); \
	 sudo mkfs.vfat -F 32 $${LOOP}p1; \
	 MNT=$$(mktemp -d); \
	 sudo mount $${LOOP}p1 $$MNT; \
	 sudo mkdir -p $$MNT/EFI/BOOT; \
	 sudo cp $(UEFI_EFI) $$MNT/EFI/BOOT/BOOTX64.EFI; \
	 sudo cp $(KERNEL_BIN) $$MNT/mellivora.bin; \
	 sudo umount $$MNT; \
	 rmdir $$MNT; \
	 sudo losetup -d $$LOOP
	@echo "=== $(UEFI_IMAGE) ready ==="
	@ls -lh $(UEFI_IMAGE)

# Run the UEFI disk image under QEMU with OVMF firmware
run-uefi: $(UEFI_IMAGE)
	@echo "=== Launching Mellivora in UEFI mode (OVMF, qemu64, 512 MB) ==="
	@[ -f $(OVMF_CODE) ] || { echo "  ERROR: $(OVMF_CODE) not found. Install ovmf."; exit 1; }
	cp $(OVMF_VARS) /tmp/OVMF_VARS_rw.fd
	$(QEMU) \
		-cpu qemu64 -m 512 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=/tmp/OVMF_VARS_rw.fd \
		-drive file=$(UEFI_IMAGE),format=raw,if=ide \
		-boot menu=on \
		-no-shutdown -no-reboot \
		$(QEMU_AUDIO_FLAGS) \
		-netdev user,id=net0 -device rtl8139,netdev=net0

# -----------------------------------------------------------------------
# v10.0: 64-bit kernel build (experimental — requires 64-bit kernel.asm)
# Assembles stage2 with KERNEL_64BIT defined so it enters long mode.
# -----------------------------------------------------------------------
KERNEL_64BIN  = kernel64.bin
STAGE2_64BIN  = stage2-64.bin
IMAGE_64      = mellivora64.img

$(KERNEL_64BIN): $(KERNEL_SRC) $(KERNEL_INCS)
	$(NASM) -f bin -O0 -w-zeroing -DKERNEL_64BIT=1 -o $@ -l $(@:.bin=.lst) $<

$(STAGE2_64BIN): $(STAGE2_SRC) kernel_sectors.inc
	$(NASM) -f bin -DKERNEL_64BIT=1 -o $@ -l $(@:.bin=.lst) $<

$(IMAGE_64): $(BOOT_BIN) $(STAGE2_64BIN) $(KERNEL_64BIN)
	@echo "=== Building 64-bit Mellivora image ==="
	$(DD) if=/dev/zero of=$(IMAGE_64) bs=1M count=$(IMAGE_SIZE_MB) status=none
	$(DD) if=$(BOOT_BIN)    of=$(IMAGE_64) bs=512 count=1  conv=notrunc status=none
	$(DD) if=$(STAGE2_64BIN) of=$(IMAGE_64) bs=512 seek=1  conv=notrunc status=none
	$(DD) if=$(KERNEL_64BIN) of=$(IMAGE_64) bs=512 seek=33 conv=notrunc status=none
	@echo "=== $(IMAGE_64) ready ==="

64bit: $(IMAGE_64)
	@echo "=== Run with: make run IMAGE=$(IMAGE_64) ==="

# -----------------------------------------------------------------------
# v10.0: Package manager (tools/hbpkg)
# -----------------------------------------------------------------------
$(HBPKG_BIN): tools/hbpkg.c
	@mkdir -p tools
	$(CC) $(CFLAGS) -o $@ $<
	@echo "=== Package manager: $(HBPKG_BIN) ==="

hbpkg: $(HBPKG_BIN)

# Print available build targets
help:
	@echo "Mellivora OS — Build Targets"
	@echo ""
	@echo "  Build (32-bit BIOS, default)"
	@echo "    make all          Build disk image (boot + stage2 + kernel)"
	@echo "    make full         Full build: boot + kernel + programs + filesystem"
	@echo "    make programs     Build all user-space programs (parallel)"
	@echo "    make kernel-only  Rebuild kernel only and patch into image (fast)"
	@echo "    make populate     Populate filesystem (requires programs)"
	@echo ""
	@echo "  Build (v10.0 new targets)"
	@echo "    make uefi         Build UEFI PE32+ EFI application (boot/uefi_loader.efi)"
	@echo "    make uefi-image   Build UEFI-bootable GPT disk image (mellivora-uefi.img)"
	@echo "    make 64bit        Build 64-bit kernel + stage2 image (mellivora64.img)"
	@echo "    make hbpkg        Build package manager (tools/hbpkg)"
	@echo ""
	@echo "  Run"
	@echo "    make run          Launch in QEMU (i486, 128 MB RAM)"
	@echo "    make run-uefi     Launch UEFI image in QEMU with OVMF firmware"
	@echo "    make dev          Full build + launch in one step"
	@echo "    make debug        Launch with QEMU monitor on stdio"
	@echo "    make run-serial   Launch with serial on TCP port 4555"
	@echo "    make run-iso      Boot the ISO in QEMU (CD-ROM + IDE)"
	@echo ""
	@echo "  ISO"
	@echo "    make iso          Create bootable ISO (~2.1 GiB) with docs"
	@echo "    make iso-lite     Create smaller ISO (~65 MiB) with truncated image"
	@echo "    make iso-verify   Validate El Torito boot record in ISO"
	@echo ""
	@echo "  Test & Info"
	@echo "    make check        Run regression suite"
	@echo "    make sizes        Show component and ISO binary sizes"
	@echo "    make count        Lines of code statistics by component"
	@echo ""
	@echo "  Housekeeping"
	@echo "    make clean        Remove all build artifacts"
	@echo "    make help         Show this help"

# Lines of code statistics
count:
	@echo "=== Mellivora OS — Lines of Code ==="
	@echo ""
	@printf "  %-24s %6d lines\n" "Boot (boot.asm)" "$$(wc -l < boot.asm)"
	@printf "  %-24s %6d lines\n" "Stage 2 (stage2.asm)" "$$(wc -l < stage2.asm)"
	@printf "  %-24s %6d lines\n" "Kernel (kernel.asm)" "$$(wc -l < kernel.asm)"
	@printf "  %-24s %6d lines\n" "Kernel modules (*.inc)" "$$(cat kernel/*.inc | wc -l)"
	@echo "  ─────────────────────────────────"
	@printf "  %-24s %6d lines\n" "Kernel total" "$$(cat kernel.asm kernel/*.inc | wc -l)"
	@echo ""
	@printf "  %-24s %6d lines  (%d files)\n" "Programs (*.asm)" \
		"$$(cat $(PROG_DIR)/*.asm | wc -l)" \
		"$$(ls $(PROG_DIR)/*.asm | wc -l)"
	@printf "  %-24s %6d lines\n" "Syscalls lib" "$$(wc -l < $(PROG_DIR)/syscalls.inc)"
	@echo ""
	@printf "  %-24s %6d lines\n" "UEFI loader (C)" "$$(wc -l < boot/uefi_loader.c 2>/dev/null || echo 0)"
	@printf "  %-24s %6d lines\n" "Package mgr hbpkg (C)" "$$(wc -l < tools/hbpkg.c 2>/dev/null || echo 0)"
	@echo ""
	@printf "  %-24s %6d lines\n" "Build system" "$$(cat Makefile populate.py | wc -l)"
	@printf "  %-24s %6d lines\n" "Documentation" "$$(cat docs/*.md | wc -l)"
	@echo "  ─────────────────────────────────"
	@printf "  %-24s %6d lines\n" "GRAND TOTAL" \
		"$$(cat boot.asm stage2.asm kernel.asm kernel/*.inc $(PROG_DIR)/*.asm $(PROG_DIR)/syscalls.inc Makefile populate.py boot/uefi_loader.c tools/hbpkg.c 2>/dev/null | wc -l)"

clean:
	rm -f $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(IMAGE) $(ISO_FILE) $(ISO_LITE_FILE) kernel_sectors.inc
	rm -f *.lst
	rm -f $(PROG_DIR)/*.bin $(PROG_DIR)/*.lst
	rm -rf .build
	# v10.0 additions
	rm -f $(UEFI_EFI) $(UEFI_SO) $(HBPKG_BIN) $(UEFI_IMAGE)

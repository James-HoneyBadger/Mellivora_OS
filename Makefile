#
# Mellivora OS - Build System
#
# Produces a bootable disk image with:
#   Sector 0:      Stage 1 boot sector (512 bytes)
#   Sectors 1-32:  Stage 2 loader (16 KB)
#   Sectors 33+:   Kernel (size = kernel_sectors.inc, computed from the binary)
#   Remaining:     Filesystem area
#
# Target: i486+ emulation via QEMU
#
# Parallel program compilation is enabled automatically when make -j is
# not already set.  Override with:  make programs NPROC=1
#

NASM = $(if $(wildcard /usr/bin/flatpak-spawn),nasm,$(if $(shell command -v nasm 2>/dev/null),nasm,$(if $(shell command -v flatpak 2>/dev/null),flatpak run --command=nasm com.visualstudio.code,$(error NASM is required to build Mellivora; install NASM or Flatpak with the VS Code app))))
QEMU ?= $(shell \
	if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
		command -v qemu-system-x86_64; \
	elif command -v qemu-system-i386 >/dev/null 2>&1; then \
		command -v qemu-system-i386; \
	elif [ -x "$$HOME/.local/bin/qemu-system-x86_64" ]; then \
		printf '%s' "$$HOME/.local/bin/qemu-system-x86_64"; \
	elif [ -x /usr/bin/flatpak-spawn ]; then \
		printf '%s' '/usr/bin/flatpak-spawn --host qemu-system-x86_64'; \
	else \
		printf '%s' 'qemu-system-x86_64'; \
	fi)
DD = dd
UNAME_S := $(shell uname -s)
HOST_ARCH := $(shell uname -m)
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
# ELF32 form of the same source, used only as a GDB symbol table.
KERNEL_ELF = kernel.elf

# Source
BOOT_SRC = boot.asm
STAGE2_SRC = stage2.asm
KERNEL_SRC = kernel.asm

# QEMU settings
# Choose a host-compatible QEMU audio backend by default.
# Override manually when needed, e.g.:
#   make run QEMU_AUDIO_BACKEND=alsa
#   make run QEMU_AUDIO_BACKEND=pa
QEMU_AUDIO_BACKEND ?= $(if $(filter aarch64 arm64 arm%,$(HOST_ARCH)),none,pa)
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

.PHONY: all clean run run-iso run-serial debug gdb symbols fsck programs populate full check check-boot check-desktop check-longmode sanitize iso iso-lite iso-verify sizes help dev count uefi run-uefi 64bit 64bit-asm hbpkg lint validate-constants validate-syscalls validate-version validate-permissions qa-sprint1 syscalls-json syscalls-md check-syscalls-json check-syscalls-md app-catalog check-app-catalog launcher-catalog check-launcher-catalog bench bench-check bench-baseline bench-trend bench-trend-check validate-manifest validate-manifest-coverage check-samples check-qemu check-runtime check-heap crash-test check-faults

all: $(IMAGE)

# Assemble stage 1 boot sector
$(BOOT_BIN): $(BOOT_SRC)
	$(NASM) -f bin -o $@ -l $(@:.bin=.lst) $<

# Kernel include files (split for readability)
KERNEL_INCS = $(wildcard kernel/*.inc)

# Assemble 32-bit kernel.
#
# The image is loaded at 0x00100000 and the program window starts at
# PROGRAM_BASE (0x00300000), so the kernel must stay below 2 MB: past that it
# overwrites the program window and then the page directory at 0x00410000.
# Failing here beats a boot that silently corrupts itself.
KERNEL_MAX_BYTES = 2097152

$(KERNEL_BIN): $(KERNEL_SRC) $(KERNEL_INCS)
	$(NASM) -f bin -O0 -w-zeroing -o $@ -l $(@:.bin=.lst) $<
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -gt $(KERNEL_MAX_BYTES) ]; then \
		echo "  ERROR: $@ is $$SIZE bytes (limit $(KERNEL_MAX_BYTES)):"; \
		echo "  the image is loaded at 0x00100000 and PROGRAM_BASE is 0x00300000,"; \
		echo "  so the kernel cannot grow past 2 MB."; \
		exit 1; \
	fi; \
	echo "  Kernel size: $$SIZE / $(KERNEL_MAX_BYTES) bytes"

# Symbol-bearing ELF32 build. This is not booted; it exists so GDB can resolve
# addresses. Symbols are relative to 0 because the ELF has no ORG; gdb is told
# the load base via `add-symbol-file kernel.elf 0x00100000` (see tools/gdb_kernel.sh).
$(KERNEL_ELF): $(KERNEL_SRC) $(KERNEL_INCS)
	$(NASM) -f elf32 -dKERNEL_ELF -O0 -w-zeroing -o $@ $<

symbols: $(KERNEL_ELF)
	@echo "  Symbols: $(KERNEL_ELF) (load base 0x00100000)"

# Generate kernel_sectors.inc from kernel binary size
# This computes ceil(size / 512) so stage2 loads exactly the right amount.
kernel_sectors.inc: $(KERNEL_BIN)
	@KSECTORS=$$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )); \
		if [ $$KSECTORS -gt 4062 ]; then \
			echo "  ERROR: kernel is $$KSECTORS sectors (> 4062). HBFS_SUPERBLOCK_LBA=4096"; \
			echo "  Update HBFS_SUPERBLOCK_LBA in kernel.asm AND populate.py."; \
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
#   Offset 0x04200 (LBA 33): kernel.bin (KERNEL_SECTORS, not padded)
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
check-qemu:
	@QEMU_BIN='$(firstword $(QEMU))'; \
		if ! command -v "$$QEMU_BIN" >/dev/null 2>&1; then \
			echo "ERROR: QEMU x86 system emulator not found: $$QEMU_BIN"; \
			case "$(HOST_ARCH)" in \
				aarch64|arm64|arm*) echo "  ARM Linux: install qemu-system-x86, or build QEMU with --target-list=x86_64-softmmu; see docs/INSTALL.md." ;; \
				*) echo "  Install qemu-system-x86_64 (or qemu-system-i386), or set QEMU=/path/to/qemu-system-x86_64." ;; \
			esac; \
			exit 1; \
		fi

run: check-qemu $(IMAGE)
	@echo "=== Launching Mellivora in QEMU (i486 CPU, 128MB RAM) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS)

# Run with serial console on TCP port (connect with: nc localhost 4555)
run-serial: check-qemu $(IMAGE)
	@echo "=== Launching Mellivora with serial on TCP port 4555 ==="
	@echo "    Connect with:  nc localhost 4555"
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS) -serial tcp:127.0.0.1:4555,server=on,wait=off

# Boot the ISO in QEMU: CD-ROM provides El Torito boot, IDE disk provides HBFS.
run-iso: check-qemu iso
	@echo "=== Launching Mellivora from ISO (El Torito CD boot + IDE disk) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_FILE) -boot d

# Run with debug output
debug: check-qemu $(IMAGE)
	@echo "=== Launching Mellivora in QEMU (DEBUG MODE) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_DEBUG_FLAGS)

# Boot under QEMU's gdbstub with the kernel symbol table loaded
gdb: check-qemu $(IMAGE) $(KERNEL_ELF)
	@echo "=== Launching Mellivora under GDB ==="
	@bash tools/gdb_kernel.sh $(IMAGE)

# Build all sample programs (parallel by default)
programs:
	$(MAKE) -j$(NPROC) $(PROG_BINS)

$(PROG_DIR)/%.bin: $(PROG_DIR)/%.asm $(PROG_DIR)/syscalls.inc $(wildcard $(PROG_DIR)/lib/*.inc)
	$(NASM) -f bin -I$(PROG_DIR)/ -o $@ -l $(@:.bin=.lst) $<

programs/blauncher.bin: programs/generated_app_catalog.inc

programs/generated_app_catalog.inc: docs/app-catalog.json tools/generate_launcher_catalog.py
	@python3 tools/generate_launcher_catalog.py

# Stamp written once populate.py has finished writing the current kernel and
# programs into the image. The QEMU and fsck targets depend on it, so
# `make -j check` cannot test an image whose population step is still running
# (or was never run).
POPULATE_STAMP = .build/populated.stamp
POPULATE_DEPS = $(IMAGE) $(KERNEL_BIN) $(PROG_BINS) populate.py

$(POPULATE_STAMP): $(POPULATE_DEPS)
	@mkdir -p $(dir $@)
	@echo "=== Populating filesystem ==="
	# Always write the latest kernel before repopulating the filesystem.
	# populate.py updates mellivora.img's mtime, so the $(IMAGE) rule is
	# skipped on subsequent builds — this dd ensures kernel is always current.
	$(DD) if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=33 conv=notrunc status=none
	$(POPULATE) $(IMAGE) $(PROG_DIR)
	@touch $@

# Populate disk image with files and programs
populate: $(POPULATE_STAMP)

# Full build: OS + programs + populated filesystem + debug symbols
full: $(POPULATE_STAMP) symbols
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

# Headless QEMU boot smoke test: asserts the kernel reaches the shell prompt and
# reported no CPU exceptions on COM1. BOOT_ALLOW_SKIP=1 keeps 'make check'
# usable on hosts without QEMU; CI runs tests/test_boot.sh directly (strict).
check-boot: $(POPULATE_STAMP)
	@BOOT_ALLOW_SKIP=1 bash tests/test_boot.sh $(IMAGE)

# Runtime regression matrix: foreground/background programs, fork+exec,
# syscall validation and networking, all driven over COM1 in QEMU.
check-runtime: $(POPULATE_STAMP)
	@RUNTIME_ALLOW_SKIP=1 bash tests/test_runtime.sh $(IMAGE)

# P1 exit gate: a 100 MB heap workload. Twenty 5 MB demand-zero blocks are
# written page by page, read back, freed and re-checked, which needs most of a
# 128 MB guest. Skipped when QEMU is unavailable, like the other QEMU gates.
check-heap: $(POPULATE_STAMP)
	@if ! command -v qemu-system-x86_64 >/dev/null 2>&1 && \
	    [ ! -x "$$HOME/.local/bin/qemu-system-x86_64" ]; then \
		echo "  SKIP  heap workload: no QEMU found"; \
	else \
		python3 tools/qemu_drive.py $(IMAGE) "heapbig" --timeout 180 \
			--expect HEAPBIG_OK --reject "CPU EXCEPTION|HEAPBIG_FAIL"; \
	fi

# Burrows desktop: boots into VBE graphics, launches apps, and checks that the
# desktop survives Alt+Tab, workspace switches and Exit Desktop.
check-desktop: $(POPULATE_STAMP)
	@DESKTOP_ALLOW_SKIP=1 python3 tests/test_desktop.py $(IMAGE)

# Host-side HBFS validator: superblock, bitmap, directory tree, xattrs, journal.
fsck: $(POPULATE_STAMP) tools/fsck_hbfs.py
	@python3 tools/fsck_hbfs.py $(IMAGE)

# Crash consistency: kill QEMU at random points during metadata work and prove
# with fsck that the journal recovers. Slower than the rest of the gate, so it
# is a separate target rather than part of `make check`.
CRASH_ITERS ?= 5
crash-test: $(POPULATE_STAMP) tools/crash_inject.py tools/fsck_hbfs.py
	@CRASH_IMAGE=$(IMAGE); \
	python3 tools/crash_inject.py "$$CRASH_IMAGE" "fs_mkdir_test" \
		--iterations $(CRASH_ITERS)

# Fault injection: OOM, disk-full and dropped-frame paths. The fixture arms one
# fault at a time and requires both an error and an unchanged system; running it
# a few times keeps an intermittent leak in a failure path from hiding. Part of
# `make check-runtime`, and separately repeatable here.
FAULT_ITERS ?= 3
check-faults: $(POPULATE_STAMP)
	@set -e; \
	for i in $$(seq 1 $(FAULT_ITERS)); do \
	    printf '  fault injection run %d/%d ' "$$i" $(FAULT_ITERS); \
	    if python3 tools/qemu_drive.py $(IMAGE) "dhcp" \
	            --then "fault_inject_test" --expect FAULT_OK \
	            --reject "CPU EXCEPTION|FAULT_FAIL|fault" >/dev/null 2>&1; then \
	        echo PASS; \
	    else \
	        echo FAIL; exit 1; \
	    fi; \
	done; \
	echo "  PASS  all fault-injection runs"

# Run regression tests (requires full build)
check: full check-samples check-boot check-runtime check-heap check-desktop fsck
	@bash tests/test_build.sh
	@python3 tests/test_hbfs.py

validate-constants:
	@python3 tools/validate_constants.py

validate-syscalls:
	@python3 tools/validate_syscalls.py

validate-version:
	@python3 tools/validate_version_strings.py

validate-permissions:
	@python3 tools/validate_permissions.py

syscalls-json:
	@python3 tools/generate_syscalls_json.py

syscalls-md:
	@python3 tools/generate_syscalls_json.py

check-syscalls-json:
	@python3 tools/generate_syscalls_json.py --check-json

check-syscalls-md:
	@python3 tools/generate_syscalls_json.py --check-md

app-catalog:
	@python3 tools/generate_app_catalog.py

check-app-catalog:
	@python3 tools/generate_app_catalog.py --check

launcher-catalog: docs/app-catalog.json tools/generate_launcher_catalog.py
	@python3 tools/generate_launcher_catalog.py

check-launcher-catalog:
	@python3 tools/generate_launcher_catalog.py --check

lint: validate-constants validate-syscalls validate-version validate-permissions check-syscalls-json check-syscalls-md check-app-catalog check-launcher-catalog validate-manifest
	@bash tools/nasm_lint.sh boot.asm stage2.asm kernel.asm
	@echo "=== Lint checks complete ==="

qa-sprint1: lint check
	@echo "=== Sprint 1 quality gate complete ==="

# Benchmark targets
bench:
	@bash tests/bench.sh

bench-check:
	@BENCH_CHECK=1 bash tests/bench.sh

bench-trend:
	@python3 tools/bench_trend.py --dir benchmark --limit 20

bench-trend-check:
	@python3 tools/bench_trend.py --dir benchmark --limit 20 --check

bench-baseline:
	@bash tests/bench.sh
	@latest=$$(ls -1t benchmark/results-*.txt 2>/dev/null | head -1); \
	if [ -n "$$latest" ]; then \
		cp "$$latest" benchmark/baseline.txt; \
		echo "=== Benchmark baseline updated from $$latest ==="; \
	else \
		echo "=== No benchmark result file found; baseline unchanged ==="; \
	fi
	@echo "=== Benchmark baseline updated ==="

# App manifest schema validation (requires jsonschema: pip install jsonschema)
validate-manifest:
	@python3 tools/validate_manifest.py

validate-manifest-coverage:
	@python3 tools/validate_manifest.py --coverage

check-samples:
	@python3 tools/check_samples.py

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
	$(MAKE) check-qemu
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
run-uefi: check-qemu $(UEFI_IMAGE)
	@echo "=== Launching Mellivora in UEFI mode (OVMF, qemu64, 512 MB) ==="
	@[ -f "$(OVMF_CODE)" ] || { echo "  ERROR: x86 OVMF code image not found: $(OVMF_CODE)"; echo "  Install x86_64 OVMF firmware and set OVMF_CODE=/path/to/OVMF_CODE.fd."; exit 1; }
	@[ -f "$(OVMF_VARS)" ] || { echo "  ERROR: x86 OVMF variables image not found: $(OVMF_VARS)"; echo "  Set OVMF_VARS=/path/to/OVMF_VARS.fd."; exit 1; }
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
KERNEL_64ASM  = kernel64-asm.bin
STAGE2_64BIN  = stage2-64.bin
IMAGE_64      = mellivora64.img

# The 64-bit nucleus (roadmap P2) is freestanding C11. The toolchain is probed
# rather than assumed, because a native gcc on a non-x86 host compiles happily
# and produces the wrong architecture: tools/find_lm_toolchain.sh tries
# $LM_CC/$LM_LD, x86_64-elf-gcc, clang --target=x86_64-elf and finally the host
# gcc, and verifies each by checking e_machine in the object it produces.
LM_CC   := $(shell sh tools/find_lm_toolchain.sh cc 2>/dev/null)
LM_LD   := $(shell sh tools/find_lm_toolchain.sh ld 2>/dev/null)
# -mgeneral-regs-only keeps the compiler off the SSE registers: long mode is
# entered without the CR4.OSFXSR/OSXMMEXCPT setup that makes SSE legal, so the
# first auto-vectorised memcpy would be an invalid-opcode fault. The nucleus
# enables those bits anyway, for the FPU/SSE code that comes later.
LM_CFLAGS = -std=c11 -m64 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector \
            -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
            -mgeneral-regs-only -fno-asynchronous-unwind-tables \
            -fno-unwind-tables
LM_LDFLAGS = -T boot/lm64.ld --oformat binary

KERNEL_64_OBJS = boot/lm_nucleus.o boot/lm_pmm.o boot/lm_cpu.o boot/lm_user.o \
                  boot/lm_idt.o boot/lm_vmm.o boot/lm_kmem.o boot/lm_irq.o \
                  boot/lm_sched.o boot/lm_syscall.o boot/lm_pci.o boot/lm_ata.o \
                  boot/lm_hbfs.o boot/lm_vfs.o boot/lm_rtc.o \
		  boot/lm_pipe.o boot/lm_vga.o boot/lm_kbd.o boot/lm_mouse.o \
		  boot/lm_e1000.o boot/lm_net.o

# The user programs are files in the image now, not part of the nucleus.
INIT_BIN = initprog.bin
HELLO_BIN = helloprog.bin
HELLO32_BIN = hello32.bin
USERPROGS = $(INIT_BIN) $(HELLO_BIN) $(HELLO32_BIN) $(HELLO_TREE_BIN) \
	    $(LS_BIN) $(WR_BIN) $(SH_BIN) $(API_BIN) $(SPIN_BIN) $(MOUSE_BIN) \
	    $(NET_BIN)

$(INIT_BIN): boot/lm_initprog.asm
	$(NASM) -f bin -o $@ $<

$(HELLO_BIN): boot/lm_hello.asm
	$(NASM) -f bin -o $@ $<

$(HELLO32_BIN): boot/lm_hello32.asm
	$(NASM) -f bin -o $@ $<

# One of the 32-bit tree's own programs, built by the same NASM invocation the
# `programs` target uses: ORG 0x00300000 comes from syscalls.inc.
HELLO_TREE_BIN = hello_tree.bin
LS_BIN = ls.bin

$(LS_BIN): boot/lm_ls.asm
	$(NASM) -f bin -o $@ $<

WR_BIN = wr.bin

$(WR_BIN): boot/lm_wr.asm
	$(NASM) -f bin -o $@ $<

SPIN_BIN = spin.bin

$(SPIN_BIN): boot/lm_spin.asm
	$(NASM) -f bin -o $@ $<

MOUSE_BIN = mouse.bin

$(MOUSE_BIN): boot/lm_mouse.asm
	$(NASM) -f bin -o $@ $<

NET_BIN = net.bin

$(NET_BIN): boot/lm_net.asm
	$(NASM) -f bin -o $@ $<

API_BIN = api.bin

$(API_BIN): boot/lm_api.asm
	$(NASM) -f bin -o $@ $<

SH_BIN = sh.bin

$(SH_BIN): boot/lm_sh.asm
	$(NASM) -f bin -o $@ $<

$(HELLO_TREE_BIN): programs/hello.asm programs/syscalls.inc
	$(NASM) -f bin -I programs/ -o $@ $<

boot/%.o: boot/%.c boot/lm.h
	@[ -n "$(LM_CC)" ] || { \
	    echo "  ERROR: no x86-64 C compiler found."; \
	    echo "  Install x86_64-elf-gcc, or clang + ld.lld, or set LM_CC/LM_LD."; \
	    exit 1; }
	$(LM_CC) $(LM_CFLAGS) -c $< -o $@

$(KERNEL_64BIN): $(KERNEL_64_OBJS) boot/lm64.ld
	@[ -n "$(LM_LD)" ] || { echo "  ERROR: no x86-64 linker found (set LM_LD)."; exit 1; }
	$(LM_LD) $(LM_LDFLAGS) -o $@ $(KERNEL_64_OBJS)
	@echo "  64-bit nucleus: $$(wc -c < $@) bytes, linked at 0xFFFFFFFF80100000"

# The original hand-written nucleus, kept as a second target so the boot path
# can still be exercised without a C toolchain (make 64bit-asm).
$(KERNEL_64ASM): boot/lm_nucleus.asm
	$(NASM) -f bin -o $@ -l $(@:.bin=.lst) $<

# stage2 only needs one sector count, so the 64-bit file defines the same name.
kernel64_sectors.inc: $(KERNEL_64BIN)
	@KSECTORS=$$(( ($$(wc -c < $(KERNEL_64BIN)) + 511) / 512 )); \
		echo "KERNEL_SECTORS  equ $$KSECTORS" > $@; \
		echo "  64-bit kernel sectors: $$KSECTORS"

$(STAGE2_64BIN): $(STAGE2_SRC) kernel64_sectors.inc
	$(NASM) -f bin -DKERNEL_64BIT=1 -o $@ -l $(@:.bin=.lst) $<

$(IMAGE_64): $(BOOT_BIN) $(STAGE2_64BIN) $(KERNEL_64BIN) $(USERPROGS) boot/motd.txt
	@echo "=== Building 64-bit Mellivora image ==="
	$(DD) if=/dev/zero of=$(IMAGE_64) bs=1M count=$(IMAGE_SIZE_MB) status=none
	$(DD) if=$(BOOT_BIN)    of=$(IMAGE_64) bs=512 count=1  conv=notrunc status=none
	$(DD) if=$(STAGE2_64BIN) of=$(IMAGE_64) bs=512 seek=1  conv=notrunc status=none
	$(DD) if=$(KERNEL_64BIN) of=$(IMAGE_64) bs=512 seek=33 conv=notrunc status=none
	@echo "  64-bit HBFS: /init, /hello, /motd"
	@python3 tools/mk_hbfs64.py $(IMAGE_64) init=$(INIT_BIN) hello=$(HELLO_BIN) hello32=$(HELLO32_BIN) hello_tree=$(HELLO_TREE_BIN) ls=$(LS_BIN) wr=$(WR_BIN) sh=$(SH_BIN) api=$(API_BIN) spin=$(SPIN_BIN) mouse=$(MOUSE_BIN) net=$(NET_BIN) motd=boot/motd.txt docs/readme.txt=boot/motd.txt
	@echo "=== $(IMAGE_64) ready ==="

64bit: $(IMAGE_64)
	@echo "=== Run with: make run IMAGE=$(IMAGE_64) ==="

# Same boot path, hand-written nucleus: exercises stage2 and the higher-half
# mapping without needing a C toolchain at all.
IMAGE_64_ASM = mellivora64-asm.img

$(STAGE2_64BIN): kernel64_sectors.inc

$(IMAGE_64_ASM): $(BOOT_BIN) $(STAGE2_64BIN) $(KERNEL_64ASM)
	@echo "=== Building 64-bit image (asm nucleus) ==="
	$(DD) if=/dev/zero of=$(IMAGE_64_ASM) bs=1M count=$(IMAGE_SIZE_MB) status=none
	$(DD) if=$(BOOT_BIN)     of=$(IMAGE_64_ASM) bs=512 count=1  conv=notrunc status=none
	$(DD) if=$(STAGE2_64BIN) of=$(IMAGE_64_ASM) bs=512 seek=1  conv=notrunc status=none
	$(DD) if=$(KERNEL_64ASM) of=$(IMAGE_64_ASM) bs=512 seek=33 conv=notrunc status=none

64bit-asm: $(IMAGE_64_ASM)
	@echo "=== Built $(IMAGE_64_ASM) ==="

# Boot the 64-bit nucleus and assert it reached long mode and reported in.
check-longmode: $(IMAGE_64)
	@python3 tests/test_longmode.py $(IMAGE_64)

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
	@echo "    make 64bit        Build the 64-bit image: C11 nucleus, linked high (mellivora64.img)"
	@echo "    make 64bit-asm    Same, with the hand-written asm nucleus (no C toolchain needed)"
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
	@echo "    make check-desktop Run the Burrows desktop QEMU test"
	@echo "    make crash-test   Kill QEMU at random points and require fsck to pass (HBFS journal)"
	@echo "    make check-faults Inject OOM / disk-full / frame drops and require clean errors"
	@echo "    make lint         Run contract and NASM source lint checks"
	@echo "    make validate-version Validate repo version strings for the current release"
	@echo "    make syscalls-json Generate docs/syscalls.json + docs/syscalls.md"
	@echo "    make check-syscalls-json Verify docs/syscalls.json is up to date"
	@echo "    make check-syscalls-md Verify docs/syscalls.md is up to date"
	@echo "    make app-catalog  Generate docs/app-catalog.json from bundled manifests"
	@echo "    make check-app-catalog Verify docs/app-catalog.json is up to date"
	@echo "    make launcher-catalog Generate the launcher app table from app-catalog.json"
	@echo "    make check-launcher-catalog Verify the launcher app table is up to date"
	@echo "    make validate-permissions Validate app permissions against platform policy"
	@echo "    make bench        Run build-time benchmark suite"
	@echo "    make bench-check  Run benchmarks and fail on regression"
	@echo "    make bench-baseline Update benchmark/baseline.txt"
	@echo "    make bench-trend  Generate benchmark trend summary/json"
	@echo "    make bench-trend-check Fail on sustained trend regressions when enough history exists"
	@echo "    make validate-manifest Validate app manifest examples against schema"
	@echo "    make validate-manifest-coverage Check program sources for missing manifests"
	@echo "    make check-samples  Syntax-check sample programs across supported languages"
	@echo "    make qa-sprint1   Run Sprint 1 gate (lint + check)"
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
	rm -f $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(KERNEL_ELF) $(IMAGE) $(ISO_FILE) $(ISO_LITE_FILE) kernel_sectors.inc
	rm -f *.lst
	rm -f $(PROG_DIR)/*.bin $(PROG_DIR)/*.lst
	rm -rf .build
	rm -f $(KERNEL_64BIN) $(KERNEL_64ASM) $(STAGE2_64BIN) $(IMAGE_64) \
	      $(IMAGE_64_ASM) $(KERNEL_64_OBJS) kernel64_sectors.inc \
	      stage2-64.lst kernel64-asm.lst
	# v10.0 additions
	rm -f $(UEFI_EFI) $(UEFI_SO) $(HBPKG_BIN) $(UEFI_IMAGE)

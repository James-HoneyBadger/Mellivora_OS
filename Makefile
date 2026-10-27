#
# Mellivora OS - Build System
#
# Produces a bootable disk image with:
#   Sector 0:      Stage 1 boot sector (512 bytes)
#   Sectors 1-32:  Stage 2 loader (16 KB)
#   Sectors 33+:   Kernel (size = kernel_sectors.inc, computed from the binary)
#   Remaining:     Filesystem area
#
# Target: Pentium 4 (32-bit) emulation via QEMU. The machine - CPU, RAM,
# chipset, devices - is defined once in tools/machine_profiles.py.
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
# Host C toolchain (for tools/hbpkg.c)
# -----------------------------------------------------------------------
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L

# Build artefacts
HBPKG_BIN  = tools/hbpkg

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

PYTHON ?= python3

# The emulated machine - CPU model, RAM, chipset, guest devices - comes from
# tools/machine_profiles.py, so `make run`, `make check` and a hand-run QEMU are
# the same computer. MV_PROFILE picks a profile (p4 by default, i486 to see the
# unsupported-CPU refusal); QEMU_AUDIO_BACKEND is the *host* backend only.
MV_PROFILE ?= p4
export MV_PROFILE
MACHINE_PROFILES = tools/machine_profiles.py
MV_MACHINE_FLAGS = $(shell $(PYTHON) $(MACHINE_PROFILES) --qemu-flags --audio $(QEMU_AUDIO_BACKEND))
ifeq ($(strip $(MV_MACHINE_FLAGS)),)
$(error machine_profiles.py produced no QEMU flags for MV_PROFILE=$(MV_PROFILE))
endif

# Reboot behavior: allow guest reboot by default.
# Set QEMU_NO_REBOOT=1 to make QEMU exit on guest reset.
QEMU_RESET_FLAGS =
ifeq ($(QEMU_NO_REBOOT),1)
QEMU_RESET_FLAGS += -no-reboot
endif

QEMU_FLAGS = $(MV_MACHINE_FLAGS) -drive file=$(IMAGE),format=raw,if=ide,cache=writethrough -boot c -no-shutdown $(QEMU_RESET_FLAGS)

# For debugging
QEMU_DEBUG_FLAGS = $(QEMU_FLAGS) \
                   -monitor stdio \
                   -d int,cpu_reset

# Programs
PROG_DIR = programs
PROG_SRCS = $(wildcard $(PROG_DIR)/*.asm)
PROG_BINS = $(patsubst %.asm,%.bin,$(PROG_SRCS) $(TEST_PROGS)) programs/cprog.bin

TEST_PROGS = programs/sbrk_test.asm programs/hello_test.asm

# A program window is 1 MB (PROGRAM_BASE..PROGRAM_END_ADDR), but the top part
# of it holds the user stack, so an image has to stay well below the whole
# window. 768 KB leaves a 256 KB stack, which is more than USER_STACK_MAX_PAGES
# allows anyway.
PROGRAM_MAX_BYTES = 786432


# Installer disc. The ISO ships a right-sized disk image (populate.py derives
# the HBFS block count from the file size), so it fits on a CD and installs
# onto any disk at least that large.
ISO_FILE = mellivora.iso
ISO_LITE_FILE = mellivora-lite.iso
ISO_IMAGE_MB ?= 256
ISO_LITE_IMG_MB ?= 64
ISO_DISK_IMAGE = mellivora-install.img
ISO_LITE_DISK_IMAGE = mellivora-install-lite.img
ISO_TOOLS = install/tools/build_iso.sh install/tools/run_iso.sh install/tools/autoinstall.py
ISO_DOCS = docs/INSTALL.md docs/USER_GUIDE.md docs/PROGRAMMING_GUIDE.md \
           docs/TECHNICAL_REFERENCE.md docs/TUTORIAL.md docs/API_REFERENCE.md \
           docs/NETWORKING_GUIDE.md install/docs/ISO_README.txt

# Populate script
POPULATE = python3 populate.py

.PHONY: all clean run run-32 run-iso run-serial debug gdb symbols fsck programs populate image-rebuild full check check-parity check-parity-update check-toolchain check-boot check-desktop sanitize iso iso-check iso-lite iso-verify test-iso sizes help dev count hbpkg lint validate-constants validate-syscalls validate-version validate-permissions qa-sprint1 syscalls-json syscalls-md check-syscalls-json check-syscalls-md app-catalog check-app-catalog launcher-catalog check-launcher-catalog bench bench-check bench-baseline bench-trend bench-trend-check validate-manifest validate-manifest-coverage check-samples check-qemu check-runtime check-perf check-perf-update check-heap crash-test check-faults check-constants check-hbfs-checksum check-hbfs-extents check-hbfs-journal check-hbfs-v4 check-journal-crash check-docs check-font check-command-table check-cmodule syscall-header check-syscall-header FORCE

all: $(IMAGE)

# -----------------------------------------------------------------------
# Freestanding 32-bit C (docs/spec/ABI.md)
#
# New kernel subsystems are C, linked into the same flat image; the boot path,
# entry stubs, context switch and every existing module stay NASM.
# tools/find_c_toolchain.sh is the only thing that chooses the compiler: it
# compiles a probe and reads the object header back, because a native compiler
# on a 64-bit host builds happily and produces objects this kernel cannot call.
#
# The probe costs a compiler run, so it is skipped for the goals that do not
# build anything (`make clean`, `make help`): a host with no C toolchain can
# still tidy up or read the list of targets.
# -----------------------------------------------------------------------
C_GOALS := $(if $(MAKECMDGOALS),$(if $(filter-out clean help,$(MAKECMDGOALS)),1),1)

ifeq ($(C_GOALS),1)
# The probe runs once per make invocation and writes plain assignments, which
# are read back from a file: $(shell) turns newlines into spaces, so feeding
# two assignments through $(eval) would swallow one into the other on the
# command line of the next compiler call.
C_TOOLCHAIN_MK = .build/toolchain.mk
$(shell mkdir -p .build && sh tools/find_c_toolchain.sh --make > $(C_TOOLCHAIN_MK) 2>/dev/null)
-include $(C_TOOLCHAIN_MK)

ifeq ($(strip $(C_CC)),)
$(error No 32-bit C compiler found. Run 'sh tools/find_c_toolchain.sh' to see the candidates, then install gcc-multilib, a cross compiler such as i686-elf-gcc, or clang with lld)
endif
ifneq ($(filter none%,$(strip $(C_LD))),)
$(error '$(C_CC)' was found but no 32-bit linker was. Install binutils (ld -m elf_i386) or lld)
endif
endif

# -ffreestanding and -fno-builtin: there is no libc, and c/rt/string.c is the
# whole runtime. -fno-pic/-fno-pie: everything is linked at a fixed address.
# -fno-stack-protector: ring 0 has no TLS segment for a canary.
#
# -march=pentium4: the CPU floor is a Pentium 4, so cmov and the Netburst
# instruction selection are fair game. -mno-sse/-mno-mmx/-msoft-float stay:
# the kernel keeps its C at the scalar ABI on purpose. XMM state belongs to
# *tasks*, which the scheduler saves per task; if kernel C used XMM too, every
# C call would have to declare which half of the vector state it owns.
C_CFLAGS = -c -O2 -std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
           -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -march=pentium4 -mtune=pentium4 \
           -mno-sse -mno-mmx -msoft-float -Wall -Wextra -Ic/include

C_INCS    = $(wildcard c/include/mv/*.h)
C_OBJ_DIR = .build/c
C_RT_OBJS = $(C_OBJ_DIR)/rt/string.o

# The kernel module: a C subsystem linked at the cmod slot (kernel/data.inc).
CMOD_OBJS = $(C_OBJ_DIR)/kmod/kmod.o $(C_RT_OBJS)
CMOD_BLOB = c/build/kmod.bin

# The user program: the same runtime, linked at PROGRAM_BASE instead.
CPROG_OBJS = $(C_OBJ_DIR)/prog/cprog.o $(C_RT_OBJS)
CPROG_BIN  = programs/cprog.bin

$(C_OBJ_DIR)/%.o: c/%.c $(C_INCS)
	@mkdir -p $(dir $@)
	$(C_CC) $(C_CFLAGS) -o $@ $<

# Linked at the address the measuring pass of the kernel build reports for the
# cmod slot. Nothing relocates the blob afterwards, so this address is the
# address it runs at.
$(CMOD_BLOB): $(CMOD_OBJS) c/kmod/kmain.ld $(C_OBJ_DIR)/kmod.addr
	@mkdir -p $(dir $@)
	$(C_LD) -m elf_i386 -T c/kmod/kmain.ld \
	    --defsym=cmod_addr=`cat $(C_OBJ_DIR)/kmod.addr` \
	    --oformat binary -o $@ $(CMOD_OBJS)
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -gt 32768 ]; then \
	    echo "  ERROR: $@ is $$SIZE bytes; the slot is 32 KB (kernel/data.inc)"; \
	    exit 1; \
	fi

$(CPROG_BIN): $(CPROG_OBJS) c/prog/prog.ld
	@mkdir -p $(dir $@)
	$(C_LD) -m elf_i386 -T c/prog/prog.ld --oformat binary -o $@ $(CPROG_OBJS)
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -gt $(PROGRAM_MAX_BYTES) ]; then \
	    echo "  ERROR: $@ is $$SIZE bytes; a program window is $(PROGRAM_MAX_BYTES) bytes"; \
	    exit 1; \
	fi

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

# Two passes, because the C module has to be linked before it can be assembled
# in, and its link address depends on how big the kernel is:
#
#   1. assemble without the blob (-DCMOD_ABSENT) to find where the slot lands;
#   2. link the C module at that address (see the rule above);
#   3. assemble again with -DCMOD_ADDR, which pads to the slot, incbins the
#      blob, and refuses to assemble if the address has gone stale.
$(C_OBJ_DIR)/kernel-probe.bin: $(KERNEL_SRC) $(KERNEL_INCS)
	@mkdir -p $(dir $@)
	$(NASM) -f bin -O0 -w-zeroing -w-label-redef-late -DCMOD_ABSENT -o $@ $(KERNEL_SRC)

$(C_OBJ_DIR)/kmod.addr: $(C_OBJ_DIR)/kernel-probe.bin
	@SIZE=$$(wc -c < $<); \
	ADDR=$$(( (0x00100000 + SIZE + 15) & ~15 )); \
	printf '0x%08X\n' $$ADDR > $@; \
	echo "  [c] C module slot at $$(cat $@)"

$(KERNEL_BIN): $(KERNEL_SRC) $(KERNEL_INCS) $(CMOD_BLOB)
	$(NASM) -f bin -O0 -w-zeroing -w-label-redef-late -DCMOD_ADDR=`cat $(C_OBJ_DIR)/kmod.addr` -o $@ -l $(@:.bin=.lst) $<
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
# The C module slot is left out: it carries no symbols, and nothing in the
# kernel sits after it.
$(KERNEL_ELF): $(KERNEL_SRC) $(KERNEL_INCS)
	$(NASM) -f elf32 -dKERNEL_ELF -DCMOD_ABSENT -O0 -w-zeroing -w-label-redef-late -o $@ $<

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

# Boot the image under QEMU (machine comes from tools/machine_profiles.py)
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

# `make run` boots the 32-bit tree with its shell and GUI.
run: check-qemu $(IMAGE)
	@echo "=== Launching Mellivora in QEMU ($(MV_PROFILE) machine) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS)

run-32: check-qemu $(IMAGE)
	@echo "=== Launching Mellivora in QEMU ($(MV_PROFILE) machine) ==="
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS)

# Run with serial console on TCP port (connect with: nc localhost 4555)
run-serial: check-qemu $(IMAGE)
	@echo "=== Launching Mellivora with serial on TCP port 4555 ==="
	@echo "    Connect with:  nc localhost 4555"
	@echo "    Host: $(UNAME_S), audio backend: $(QEMU_AUDIO_BACKEND)"
	$(QEMU) $(QEMU_FLAGS) -serial tcp:127.0.0.1:4555,server=on,wait=off

# Boot the installer ISO in QEMU with a blank target disk.
run-iso: check-qemu iso
	@install/tools/run_iso.sh --iso $(ISO_FILE)

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

# Rebuild the disk image from scratch. populate.py preserves an existing volume
# and skips files its cache already knows, which is what a normal build wants —
# and precisely why a damaged image survives `make full` forever. This discards
# the image and the cache so every directory entry is written again.
image-rebuild:
	@rm -f $(IMAGE) $(POPULATE_STAMP) .populate_cache
	@$(MAKE) --no-print-directory full

# Run one build stage and print what it did, without make's own "is up to
# date" chatter. A stage that fails prints its whole log before stopping.
define run_stage
	@log=$$(mktemp); \
	if $(MAKE) --no-print-directory $(1) >"$$log" 2>&1; then \
		grep -v -e "is up to date" -e "Nothing to be done for" "$$log" || true; \
	else \
		cat "$$log"; rm -f "$$log"; exit 1; \
	fi; \
	rm -f "$$log"
endef

# Full build: every stage that produces a bootable system, in order and each
# one announced, so the log says what the image is made of.
full:
	@echo "=== Mellivora OS - full build ==="
	@echo ""
	@echo "  [1/6] Boot sector (real mode, A20, E820, protected mode)"
	$(call run_stage,$(BOOT_BIN))
	@echo ""
	@echo "  [2/6] Kernel (kernel.bin, kernel_sectors.inc) and symbol table"
	$(call run_stage,$(KERNEL_BIN) kernel_sectors.inc $(KERNEL_ELF))
	@echo ""
	@echo "  [3/6] Stage 2 loader (loads the kernel, enters protected mode)"
	$(call run_stage,$(STAGE2_BIN))
	@echo ""
	@echo "  [4/6] User programs ($(words $(PROG_BINS)) sources)"
	@$(MAKE) --no-print-directory -s programs
	@echo "        $(words $(PROG_BINS)) programs ready"
	@echo ""
	@echo "  [5/6] Disk image ($(IMAGE_SIZE_MB) MB)"
	$(call run_stage,$(IMAGE))
	@echo ""
	@echo "  [6/6] Filesystem (HBFS)"
	$(call run_stage,populate)
	@echo ""
	@echo "=== Full build complete (32-bit: make run) ==="

# Rebuild kernel only and write it into the existing disk image.
# Skips program compilation and filesystem population — fastest
# iteration loop when only kernel/*.inc or kernel.asm changed.
kernel-only: $(KERNEL_BIN) kernel_sectors.inc $(STAGE2_BIN)
	@echo "=== Patching kernel into $(IMAGE) ==="
	$(DD) if=$(STAGE2_BIN) of=$(IMAGE) bs=512 seek=1  conv=notrunc status=none
	$(DD) if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=33 conv=notrunc status=none
	@echo "=== kernel-only update complete ==="

# A disk image at a size that fits on a CD. populate.py derives the HBFS block
# count from the file size, so the volume matches the file exactly.
define build_iso_image
	@echo "=== Building $(1) ($(2) MB) ==="
	$(DD) if=/dev/zero of=$(1) bs=1M count=$(2) status=none
	$(DD) if=$(BOOT_BIN)   of=$(1) bs=512 count=1 conv=notrunc status=none
	$(DD) if=$(STAGE2_BIN) of=$(1) bs=512 seek=1  conv=notrunc status=none
	$(DD) if=$(KERNEL_BIN) of=$(1) bs=512 seek=33 conv=notrunc status=none
	$(POPULATE) $(1) $(PROG_DIR)
endef

ISO_IMAGE_DEPS = $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(PROG_BINS) populate.py

# populate.py derives the HBFS block count from the size of the file it writes,
# so an image is only correct for the size it was built at. These stamps record
# that size: when ISO_IMAGE_MB (or ISO_LITE_IMG_MB) changes, the stamp changes
# and the image is rebuilt. Without them 'make iso ISO_IMAGE_MB=64' silently
# reused an existing 256 MB image.
ISO_SIZE_STAMP = .build/iso-image-size
ISO_LITE_SIZE_STAMP = .build/iso-lite-image-size

# Always out of date, so the stamp recipes run on every build and touch the
# file only when its value really changed.
FORCE:

$(ISO_SIZE_STAMP): FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(ISO_IMAGE_MB)' | cmp -s - $@ || printf '%s\n' '$(ISO_IMAGE_MB)' > $@

$(ISO_LITE_SIZE_STAMP): FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(ISO_LITE_IMG_MB)' | cmp -s - $@ || printf '%s\n' '$(ISO_LITE_IMG_MB)' > $@

$(ISO_DISK_IMAGE): $(ISO_IMAGE_DEPS) $(ISO_SIZE_STAMP)
	$(call build_iso_image,$(ISO_DISK_IMAGE),$(ISO_IMAGE_MB))

$(ISO_LITE_DISK_IMAGE): $(ISO_IMAGE_DEPS) $(ISO_LITE_SIZE_STAMP)
	$(call build_iso_image,$(ISO_LITE_DISK_IMAGE),$(ISO_LITE_IMG_MB))

# Build the installer disc: a real-mode installer that writes the image to a
# hard disk. See install/README.md.
iso: $(ISO_DISK_IMAGE) $(ISO_TOOLS) $(ISO_DOCS) README.md LICENSE CHANGELOG.md
	@install/tools/build_iso.sh --image $(ISO_DISK_IMAGE) --out $(ISO_FILE)
	@echo "=== Installer ISO ready: $(ISO_FILE) ==="

# Same disc with a 64 MB payload: quicker to download, installs a smaller disk
iso-lite:
	@$(MAKE) --no-print-directory iso \
		ISO_IMAGE_MB=$(ISO_LITE_IMG_MB) \
		ISO_DISK_IMAGE=$(ISO_LITE_DISK_IMAGE) \
		ISO_FILE=$(ISO_LITE_FILE)

# Verify El Torito boot record and payload of an existing ISO
iso-verify:
	@echo "=== Verifying $(ISO_FILE) ==="
	@command -v xorriso >/dev/null 2>&1 || { echo "  error: xorriso required for verification"; exit 1; }
	@test -f $(ISO_FILE) || { echo "  error: $(ISO_FILE) not built - run 'make iso'"; exit 1; }
	@xorriso -indev $(ISO_FILE) -report_el_torito plain 2>&1 \
		| grep -E 'Boot record|El Torito'
	@LOAD=$$(xorriso -indev $(ISO_FILE) -report_el_torito plain 2>&1 \
		| awk '/El Torito boot img/{print $$(NF-1)}'); \
	 if [ "$$LOAD" = "4" ]; then \
		echo "  PASS  boot-load-size: $$LOAD sectors (2048 byte installer stub)"; \
	 else \
		echo "  FAIL  boot-load-size: expected 4, found $$LOAD"; exit 1; \
	 fi
	@IMG=$$(xorriso -indev $(ISO_FILE) -find / 2>&1 | grep -c '/MELLIVORA.IMG'); \
	 if [ "$$IMG" = "1" ]; then echo "  PASS  /MELLIVORA.IMG is present"; \
	 else echo "  FAIL  /MELLIVORA.IMG missing"; exit 1; fi

# Structure-only check: builds the disc, then reads it back without booting it.
iso-check: iso
	@python3 tests/test_iso.py

# The installer reads every chunk back after writing it. This builds a disc
# whose writes land one sector late and requires the installer to stop there
# instead of reporting success.
check-installer-verify: $(ISO_DISK_IMAGE)
	@python3 tests/test_installer_verify.py

# End-to-end: build the ISO, install it onto a blank disk in QEMU, check the
# bytes and boot the result. Slow (a full image copy) - not part of 'check'.
test-iso: iso
	@python3 tests/test_iso.py --end-to-end

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
# The durability gate: a handful of crash points on every build, and the same
# harness with a much larger count for the wave's exit test.
check-durability: $(POPULATE_STAMP) tools/crash_inject.py tools/fsck_hbfs.py
	@CRASH_ALLOW_SKIP=1 bash tests/test_powercut.sh $(IMAGE)

crash-soak: $(POPULATE_STAMP) tools/crash_inject.py tools/fsck_hbfs.py
	@CRASH_ITERS=$${CRASH_ITERS:-10000} bash tests/test_powercut.sh $(IMAGE)

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
check: full check-parity check-cmodule check-samples check-boot check-runtime check-perf check-heap check-desktop check-durability fsck check-hbfs-checksum check-hbfs-extents check-hbfs-journal
	@bash tests/test_build.sh
	@python3 tests/test_hbfs.py

# The fourteen user jobs docs/ROADMAP.md defines parity by. A row moves only when
# its named evidence exists in the tree, and --check fails when one regresses, so
# a capability cannot be lost quietly. The baseline is reviewed with the change
# that earns it: `make check-parity-update`.
check-parity:
	@python3 tools/parity_scorecard.py --check
	@python3 tests/test_parity_scorecard.py

check-parity-update:
	@python3 tools/parity_scorecard.py --update-baseline

# The Latency and Footprint gates docs/ROADMAP.md names. `make bench` times host
# make targets, so it says nothing about the machine under test; this runs
# programs/perf_test.asm inside the guest and compares the numbers with a
# baseline recorded on the same machine profile. `make check-perf-update` is the
# only way the baseline moves. GUEST_ALLOW_SKIP=1 keeps `make check` usable on a
# host without QEMU.
check-perf:
	@GUEST_ALLOW_SKIP=1 bash tests/bench_guest.sh

check-perf-update:
	@bash tests/bench_guest.sh --update

# The wave's exit test in compressed form: four processors under mixed load,
# with the machine required to still answer afterwards. `make soak` with
# SOAK_MINUTES set is the real thing - the gate form runs for a minute.
check-smp:
	@SOAK_MINUTES=1 SOAK_ALLOW_SKIP=1 bash tests/soak.sh

soak:
	@SOAK_MINUTES=$${SOAK_MINUTES:-1} bash tests/soak.sh

# Report which 32-bit C toolchain the build will use. The build itself now
# requires one (the kernel carries a C module and user programs are compiled
# from C), so this exists to explain a failure, not to predict one.
check-toolchain:
	@sh tools/find_c_toolchain.sh

# The C module is at the address the assembler measured and the linker was
# told, with the header kernel/cmod.inc reads. A mismatch there is a call into
# whatever the address happens to hold, so it is worth checking without QEMU.
check-cmodule: $(KERNEL_BIN)
	@python3 tools/check_cmodule.py

# The C view of the syscall numbers is generated from programs/syscalls.inc and
# checked in, like docs/syscalls.json: a number allocated on one side cannot go
# missing on the other.
syscall-header:
	@python3 tools/generate_syscall_header.py

check-syscall-header:
	@python3 tools/generate_syscall_header.py --check

validate-constants:
	@python3 tools/validate_constants.py

# The validator is only worth having if it has been seen to reject a changed
# number, so its own drift test runs with the lint checks.
check-constants:
	@python3 tests/test_validate_constants.py

# The v4 superblock's checksum and the offline audit that recomputes it. Both
# build their own images in a temporary directory, so this needs no build.
check-hbfs-checksum:
	@python3 tests/test_hbfs_checksum.py

# The extent form: a file whose entry names runs instead of data. Offline, like
# the checksum test - it builds its own images.
check-hbfs-extents:
	@python3 tests/test_hbfs_extents.py

# The journal, crashed at every write boundary it has and then replayed. This is
# the fast version; check-journal-crash is the exit gate.
check-hbfs-journal:
	@python3 tests/test_hbfs_journal.py

# HBFS v4 read-only mount: build a v4 volume, verify its checksums offline, then
# boot it and prove reads work, writes are refused and a bad checksum is refused.
# Slower than the other HBFS checks because it populates an image.
check-hbfs-v4:
	@python3 tests/test_v4_mount.py

check-docs:
	@python3 tools/check_docs.py

# The 5x7 VBE font is indexed by character offset, so a missing glyph shifts
# every character after it. This pins the geometry and the printable range.
check-font:
	@python3 tools/check_font.py

# The command table, the dispatcher and the help text must agree.
check-command-table:
	@python3 tools/check_command_table.py

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

lint: validate-constants check-constants check-hbfs-checksum check-hbfs-extents validate-syscalls validate-version validate-permissions check-syscalls-json check-syscalls-md check-syscall-header check-app-catalog check-launcher-catalog validate-manifest check-docs check-font check-command-table
	@bash tools/nasm_lint.sh boot.asm stage2.asm kernel.asm \
		install/boot/stage1.asm install/boot/installer.asm
	@echo "=== Lint checks complete ==="

# The sprint gate: ten thousand crash/replay iterations, unattended. It takes
# about twenty minutes, which is why it is not part of check.
check-journal-crash:
	@python3 tests/test_hbfs_journal.py --iterations 10000

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
	@echo "  Build"
	@echo "    make all          Build disk image (boot + stage2 + kernel)"
	@echo "    make full         Full build: boot + kernel + programs + filesystem"
	@echo "    make programs     Build all user-space programs (parallel)"
	@echo "    make kernel-only  Rebuild kernel only and patch into image (fast)"
	@echo "    make populate     Populate filesystem (requires programs)"
	@echo "    make image-rebuild Discard the image and cache, then build every file again"
	@echo ""
	@echo "  Build (tools)"
	@echo "    make hbpkg        Build package manager (tools/hbpkg)"
	@echo ""
	@echo "  Run"
	@echo "    make run          Launch in QEMU ($(MV_PROFILE) machine)"
	@echo "                      MV_PROFILE=p4|p4-noapic|i486 picks the machine"
	@echo "    make dev          Full build + launch in one step"
	@echo "    make debug        Launch with QEMU monitor on stdio"
	@echo "    make run-serial   Launch with serial on TCP port 4555"
	@echo "    make run-iso      Boot the ISO in QEMU (CD-ROM + IDE)"
	@echo ""
	@echo "  Installer disc"
	@echo "    make iso          Build the installer ISO ($(ISO_IMAGE_MB) MB payload)"
	@echo "    make iso-lite     Build it with a $(ISO_LITE_IMG_MB) MB payload instead"
	@echo "    make iso-verify   Validate the El Torito record and payload of the ISO"
	@echo "    make iso-check    Check the ISO's structure without booting it"
	@echo "    make test-iso     Install the ISO onto a blank disk in QEMU and boot it"
	@echo ""
	@echo "  Test & Info"
	@echo "    make check        Run regression suite"
	@echo "    make check-desktop Run the Burrows desktop QEMU test"
	@echo "    make crash-test   Kill QEMU at random points and require fsck to pass (HBFS journal)"
	@echo "    make check-faults Inject OOM / disk-full / frame drops and require clean errors"
	@echo "    make lint         Run contract and NASM source lint checks"
	@echo "    make validate-version Validate repo version strings for the current release"
	@echo "    make check-toolchain Report which 32-bit C toolchain the build uses"
	@echo "    make check-cmodule Verify the C kernel module is at its linked address"
	@echo "    make syscall-header Generate the C syscall numbers header"
	@echo "    make check-syscall-header Verify the C syscall header is up to date"
	@echo "    make syscalls-json Generate docs/syscalls.json + docs/syscalls.md"
	@echo "    make check-syscalls-json Verify docs/syscalls.json is up to date"
	@echo "    make check-syscalls-md Verify docs/syscalls.md is up to date"
	@echo "    make app-catalog  Generate docs/app-catalog.json from bundled manifests"
	@echo "    make check-app-catalog Verify docs/app-catalog.json is up to date"
	@echo "    make launcher-catalog Generate the launcher app table from app-catalog.json"
	@echo "    make check-launcher-catalog Verify the launcher app table is up to date"
	@echo "    make validate-permissions Validate app permissions against platform policy"
	@echo "    make bench        Run build-time benchmark suite"
	@echo "    make check-perf   Guest latency and footprint against the baseline"
	@echo "    make bench-check  Run host build-time benchmarks, fail on regression"
	@echo "    make bench-baseline Update benchmark/baseline.txt"
	@echo "    make bench-trend  Generate benchmark trend summary/json"
	@echo "    make bench-trend-check Fail on sustained trend regressions when enough history exists"
	@echo "    make validate-manifest Validate app manifest examples against schema"
	@echo "    make validate-manifest-coverage Check program sources for missing manifests"
	@echo "    make check-samples  Syntax-check sample programs across supported languages"
	@echo "    make check-hbfs-v4  Build an HBFS v4 volume, verify it and mount it read-only"
	@echo "    make check-docs   Check docs for duplicate headings, dead links and version drift"
	@echo "    make check-font   Check the 5x7 VBE font covers printable ASCII"
	@echo "    make check-command-table Keep help, completion and which in step with the dispatcher"
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
	@printf "  %-24s %6d lines\n" "Package mgr hbpkg (C)" "$$(wc -l < tools/hbpkg.c 2>/dev/null || echo 0)"
	@echo ""
	@printf "  %-24s %6d lines\n" "Build system" "$$(cat Makefile populate.py | wc -l)"
	@printf "  %-24s %6d lines\n" "Documentation" "$$(cat docs/*.md | wc -l)"
	@echo "  ─────────────────────────────────"
	@printf "  %-24s %6d lines\n" "GRAND TOTAL" \
		"$$(cat boot.asm stage2.asm kernel.asm kernel/*.inc $(PROG_DIR)/*.asm $(PROG_DIR)/syscalls.inc Makefile populate.py tools/hbpkg.c 2>/dev/null | wc -l)"

clean:
	# kernel_sectors.inc is a tracked file, not build output: removing it here
	# left the tree dirty after every clean.
	rm -f $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(KERNEL_ELF) $(IMAGE) $(ISO_FILE) $(ISO_LITE_FILE)
	rm -f $(ISO_DISK_IMAGE) $(ISO_LITE_DISK_IMAGE) mellivora-install-target.img
	rm -f *.lst
	rm -f $(PROG_DIR)/*.bin $(PROG_DIR)/*.lst
	rm -rf .build
	rm -rf $(dir $(CMOD_BLOB))
	rm -f $(HBPKG_BIN)

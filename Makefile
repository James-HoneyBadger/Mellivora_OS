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

NASM = nasm
QEMU = qemu-system-i386
DD = dd

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
QEMU_FLAGS = -cpu 486 \
             -m 128 \
             -drive file=$(IMAGE),format=raw,if=ide,cache=writethrough \
             -boot c \
             -no-reboot \
             -no-shutdown \
             -audiodev coreaudio,id=snd0 \
             -machine pcspk-audiodev=snd0,usb=off \
             -netdev user,id=net0 \
             -device rtl8139,netdev=net0

# For debugging
QEMU_DEBUG_FLAGS = $(QEMU_FLAGS) \
                   -monitor stdio \
                   -d int,cpu_reset

# Programs
PROG_DIR = programs
PROG_SRCS = $(wildcard $(PROG_DIR)/*.asm)
PROG_BINS = $(PROG_SRCS:.asm=.bin)

# ISO packaging
ISO_STAGING = .build/iso-root
ISO_FILE = mellivora.iso
ISO_DOCS = docs/INSTALL.md docs/USER_GUIDE.md docs/PROGRAMMING_GUIDE.md \
           docs/TECHNICAL_REFERENCE.md docs/TUTORIAL.md docs/API_REFERENCE.md \
           Experimental/docs/ISO_README.txt

# Populate script
POPULATE = python3 populate.py

.PHONY: all clean run debug programs populate full check iso

all: $(IMAGE)

# Assemble stage 1 boot sector
$(BOOT_BIN): $(BOOT_SRC)
	$(NASM) -f bin -o $@ -l $(@:.bin=.lst) $<

# Kernel include files (split for readability)
KERNEL_INCS = $(wildcard kernel/*.inc)

# Assemble 32-bit kernel
$(KERNEL_BIN): $(KERNEL_SRC) $(KERNEL_INCS)
	$(NASM) -f bin -O0 -o $@ -l $(@:.bin=.lst) $<

# Generate kernel_sectors.inc from kernel binary size
# This computes ceil(size / 512) so stage2 loads exactly the right amount.
kernel_sectors.inc: $(KERNEL_BIN)
	@KSECTORS=$$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )); \
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
	# Create empty 2GB disk image
	$(DD) if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_SIZE_MB) status=none
	# Write boot sector at LBA 0
	$(DD) if=$(BOOT_BIN) of=$(IMAGE) bs=512 count=1 conv=notrunc status=none
	# Write stage 2 at LBA 1
	$(DD) if=$(STAGE2_BIN) of=$(IMAGE) bs=512 seek=1 conv=notrunc status=none
	# Write kernel at LBA 33
	$(DD) if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=33 conv=notrunc status=none
	@echo "=== $(IMAGE) created ($(IMAGE_SIZE_MB) MB) ==="
	@ls -la $(IMAGE)

# Run in QEMU with i486 CPU
run: $(IMAGE)
	@echo "=== Launching Mellivora in QEMU (i486 CPU, 128MB RAM) ==="
	$(QEMU) $(QEMU_FLAGS)

# Run with serial console on TCP port (connect with: nc localhost 4555)
run-serial: $(IMAGE)
	@echo "=== Launching Mellivora with serial on TCP port 4555 ==="
	@echo "    Connect with:  nc localhost 4555"
	$(QEMU) $(QEMU_FLAGS) -serial tcp:127.0.0.1:4555,server=on,wait=off

# Run with debug output
debug: $(IMAGE)
	@echo "=== Launching Mellivora in QEMU (DEBUG MODE) ==="
	$(QEMU) $(QEMU_DEBUG_FLAGS)

# Build all sample programs
programs: $(PROG_BINS)

$(PROG_DIR)/%.bin: $(PROG_DIR)/%.asm $(PROG_DIR)/syscalls.inc $(wildcard $(PROG_DIR)/lib/*.inc)
	$(NASM) -f bin -I$(PROG_DIR)/ -o $@ -l $(@:.bin=.lst) $<

# Populate disk image with files and programs
populate: $(IMAGE) programs populate.py
	@echo "=== Populating filesystem ==="
	$(POPULATE) $(IMAGE) $(PROG_DIR)

# Full build: OS + programs + populated filesystem
full: $(IMAGE) programs populate
	@echo "=== Full build complete ==="

# Build a bootable ISO that includes install docs and the user guide
iso: full Experimental/tools/build_iso.sh $(ISO_DOCS) README.md LICENSE CHANGELOG.md
	@echo "=== Preparing bootable ISO staging tree ==="
	@rm -rf "$(ISO_STAGING)"
	@mkdir -p "$(ISO_STAGING)/boot" "$(ISO_STAGING)/docs"
	@cp "$(IMAGE)" "$(ISO_STAGING)/boot/mellivora.img"
	@cp README.md LICENSE CHANGELOG.md "$(ISO_STAGING)/"
	@cp docs/INSTALL.md docs/USER_GUIDE.md docs/PROGRAMMING_GUIDE.md \
		  docs/TECHNICAL_REFERENCE.md docs/TUTORIAL.md docs/API_REFERENCE.md \
		  "$(ISO_STAGING)/docs/"
	@cp Experimental/docs/ISO_README.txt "$(ISO_STAGING)/README.txt"
	@chmod +x Experimental/tools/build_iso.sh
	@ISO_BOOT_SECTORS=$$(awk '/KERNEL_SECTORS/ {print $$3 + 33}' kernel_sectors.inc) \
		./Experimental/tools/build_iso.sh "$(ISO_STAGING)" "$(ISO_FILE)"
	@echo "=== Bootable ISO ready: $(ISO_FILE) ==="

# Run regression tests (requires full build)
check: full
	@bash tests/test_build.sh
	@python3 tests/test_hbfs.py

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

clean:
	rm -f $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(IMAGE) $(ISO_FILE) kernel_sectors.inc
	rm -f *.lst
	rm -f $(PROG_DIR)/*.bin $(PROG_DIR)/*.lst
	rm -rf .build

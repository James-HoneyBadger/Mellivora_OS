Mellivora OS Bootable ISO
=========================

This ISO is a bootable distribution of Mellivora OS for BIOS/legacy-boot x86
systems and VMs.  It uses El Torito no-emulation boot so the BIOS preloads the
boot sector, stage 2 loader, and kernel directly into memory.

The HBFS filesystem lives on /boot/mellivora.img (a 2 GB raw disk image).  The
kernel requires that image to be accessible as an ATA hard disk, so the QEMU
command line must attach it on the primary IDE channel alongside the CD-ROM.

Contents
--------
- /boot/mellivora.img           Prebuilt 2 GB bootable system image (HBFS)
- /docs/INSTALL.md              Full build, install, VM, and physical-machine guide
- /docs/USER_GUIDE.md           Complete shell and usage guide
- /docs/PROGRAMMING_GUIDE.md
- /docs/TECHNICAL_REFERENCE.md
- /README.md and /LICENSE

Quick start in a VM
-------------------

**Recommended: use the helper script (extracts the image + launches QEMU):**

  ./Experimental/tools/run_iso.sh mellivora.iso

**Or with make (requires the build tree):**

  make run-iso

**Manual QEMU (both the ISO and the disk image must be present):**

  qemu-system-i386 -cpu 486 -m 128 \
    -cdrom mellivora.iso \
    -drive file=mellivora.img,format=raw,if=ide,cache=writethrough \
    -boot d -no-shutdown \
    -audiodev none,id=snd0 -machine pcspk-audiodev=snd0,usb=off \
    -netdev user,id=net0 -device rtl8139,netdev=net0

VirtualBox / VMware / UTM:
  1. Create a VM with Legacy BIOS boot, 128 MB RAM, i486/i686 CPU.
  2. Attach the ISO as the optical drive.
  3. Also attach /boot/mellivora.img as the primary IDE hard disk
     (extract it from the ISO first with xorriso, 7z, or bsdtar).
  4. Set the boot order to optical first.

Install to a USB drive or physical disk
---------------------------------------
Write /boot/mellivora.img to the target disk with dd, Raspberry Pi Imager, or
balenaEtcher.  This erases the target media.

  dd if=mellivora.img of=/dev/sdX bs=4M status=progress

See /docs/INSTALL.md for complete instructions.

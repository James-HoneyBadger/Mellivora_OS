Mellivora OS Bootable ISO
=========================

This ISO is bootable on BIOS/legacy-boot x86 systems and in VMs that support
El Torito hard-disk emulation.

Contents
--------
- /boot/mellivora.img      Prebuilt 64 MB bootable system image
- /docs/INSTALL.md         Full build, install, VM, and physical-machine guide
- /docs/USER_GUIDE.md      Complete shell and usage guide
- /docs/PROGRAMMING_GUIDE.md
- /docs/TECHNICAL_REFERENCE.md
- /README.md and /LICENSE

Quick start in a VM
-------------------
QEMU:
  qemu-system-i386 -m 128 -cdrom mellivora.iso -boot d -no-reboot -no-shutdown

VirtualBox / VMware / UTM:
  Attach the ISO as the optical drive and boot the VM in Legacy BIOS mode.

Install to a USB drive or physical disk
---------------------------------------
Write /boot/mellivora.img to the target disk with dd, Raspberry Pi Imager, or
balenaEtcher. This erases the target media.

See /docs/INSTALL.md for the complete instructions.

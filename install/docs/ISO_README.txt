Mellivora OS installer disc
===========================

This disc installs Mellivora OS onto a hard disk.  It does not run the
operating system from the CD; it writes a complete Mellivora disk, and the
machine boots from that disk afterwards.

Booting
-------
The disc boots on BIOS (legacy) systems and on virtual machines configured for
BIOS boot.  It uses El Torito no-emulation boot: the BIOS loads a 2 KB loader,
which reads the installer from the CD.  UEFI-only machines cannot boot this
disc - disable Secure Boot and enable CSM/legacy boot, or install from a VM.

In a virtual machine, attach this file as a CD-ROM device and attach an empty
disk as an IDE/SATA hard disk.

Installing
----------
The installer is a small text screen:

  1. It looks for MELLIVORA.IMG on the disc and prints its size.
  2. It lists every BIOS hard disk it can see, with its capacity in MiB.
     The CD itself is never listed.
  3. Press the number of the disk to install to.  Esc quits.
  4. It asks you to type YES.  This is the point of no return: everything on
     the chosen disk is overwritten, starting at sector 0.
  5. It writes MELLIVORA.IMG to the disk in 32 KB chunks (one dot per MiB) and
     reads each chunk straight back.  A chunk that does not match is written
     again once; if it still does not match, the install stops there rather
     than leaving a disk that looks finished.
  6. "Installation complete." - remove the disc and boot from the disk.

The disk must be at least as large as MELLIVORA.IMG.  A larger disk works:
Mellivora only uses the space the image describes, and the rest is left
untouched (it is not added to the filesystem).

What gets written
-----------------
MELLIVORA.IMG is a complete Mellivora disk image, byte for byte:

  sector 0        boot sector
  sectors 1-32    stage 2 loader
  sector 33       kernel (as many sectors as the kernel needs, up to 8192)
  sector 8481     HBFS superblock, bitmap and root directory
  sector 8866     first HBFS data block

Because the copy is exact, the installed disk behaves exactly like the image
it came from: same kernel, same programs, same files.

Troubleshooting
---------------
"No BIOS hard disk was found."
    The BIOS does not report any hard disk.  Check that the disk is attached
    and that the VM controller is IDE/SATA (not a bare NVMe passthrough the
    BIOS cannot see), then reboot the disc.

"That disk is too small"
    Pick a larger disk, or build a disc with a smaller image (make iso
    ISO_IMAGE_MB=64).

"MELLIVORA.IMG was not found on this CD."
    The disc was not produced by install/tools/build_iso.sh, or the image was
    stripped from it.  Rebuild with "make iso".

"Write failed, BIOS status 0x.."
    The BIOS refused a write.  The disk may be read-only, write-protected, or
    failing.  The hex value is the BIOS status from INT 13h AH=43h; 0x03 is
    write-protected, 0x0C is a media error.  A status of 0xFF is not a BIOS
    status: it means a 32 KB chunk did not read back as written, even after
    one rewrite - the disk or its cable is failing.

Booting the installed disk shows nothing
    Check the boot order in the BIOS/firmware setup.  Mellivora boots with
    legacy BIOS interrupts (INT 13h/INT 10h); it cannot boot on a machine
    that only offers UEFI.

More documentation is in the /docs directory of this disc and at
https://github.com/James-HoneyBadger/Mellivora_OS

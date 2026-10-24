# Installer disc

This directory builds the bootable disc that installs Mellivora OS onto a hard
disk. It is a real installer, not a live CD: it boots a small real-mode program
that copies a complete disk image onto the hard disk you choose.

```
make iso                  # mellivora.iso, ships a 256 MiB image
make iso ISO_IMAGE_MB=64  # smaller payload, quicker to copy
make iso-lite             # the same, as mellivora-lite.iso (64 MiB image)
make iso-verify           # report the El Torito record of the built ISO
make iso-check            # structure check of the ISO, no QEMU
make test-iso             # install it in QEMU, then boot the installed disk
make check-installer-verify  # prove a mis-addressed write is caught
install/tools/run_iso.sh --install
```

Set `SOURCE_DATE_EPOCH` to make the disc byte-for-byte reproducible; without
it xorriso stamps the volume with the current time.

## How it works

| File | Role |
|------|------|
| `boot/stage1.asm` | 2 KB El Torito boot image. The BIOS loads it at `0x7C00`. It reads the Boot Record Volume Descriptor (CD sector 17) to find the boot catalog, takes the load LBA of this same file from the catalog, and pulls the next 64 KB - the payload - to `0x1000:0000`. |
| `boot/installer.asm` | The payload at physical `0x10000`. Walks the ISO9660 tree for `MELLIVORA.IMG`, enumerates BIOS hard disks with INT 13h AH=41h/48h, asks which one to use, then copies the image in 32 KB chunks with AH=42h/43h - reading every chunk back with AH=42h, comparing it with the bytes that came off the CD, and rewriting it once if they differ. |
| `tools/build_iso.sh` | Assembles both, stages the image plus a few documents, runs `xorriso -as mkisofs`, and verifies the result (El Torito record, boot image path, image size inside the ISO). |
| `tools/run_iso.sh` | QEMU launcher. Creates a blank target disk, then either hands you a keyboard or (`--install`) drives the installer unattended. |
| `tools/autoinstall.py` | Unattended driver + verifier: boots the ISO, answers the prompts, compares the whole written image with the one inside the ISO and (with `--boot-check`) boots the result. |
| `../tests/test_installer_verify.py` | Builds the same disc with `write_chunk` moved one sector late and requires the installer to stop on the first chunk (`make check-installer-verify`). |

The disc needs no filesystem driver in the kernel: the installer reads the CD
with BIOS calls, so the kernel never has to understand ISO9660 or ATAPI.

## Why the old disc could not work

The previous packaging (`Experimental/`) put the 2 GB raw disk image in the El
Torito boot entry and expected the BIOS to preload boot sector + stage 2 +
kernel. Three things were wrong:

* the boot image was 2 GB, and the El Torito entry declared 3649 sectors to
  preload - far beyond the 640 KB of conventional memory;
* the kernel has no ATAPI driver (`kernel/ata.inc` rejects ATAPI devices), so
  with only a CD attached there was no HBFS volume to mount;
* `make iso-lite` truncated the 2 GB image to 64 MB, which cut the HBFS
  superblock's block count loose from the file size and produced a corrupt
  volume.

Booting that ISO only worked in QEMU when the same image was attached as a
separate hard disk, which is not what an install medium does.

## Disk image used in the ISO

`mellivora-install.img` is built from `boot.bin`, `stage2.bin`, `kernel.bin`
and `populate.py`, at `ISO_IMAGE_MB` (default 256). `populate.py` derives the
HBFS block count from the size of the file it populates, so any size produces a
consistent, fsck-clean volume - that is what makes a small install image
legitimate.

## Limits worth knowing

* BIOS only. The installer runs in real mode and uses INT 10h/13h/16h; UEFI
  systems need CSM/legacy boot enabled.
* CD/DVD only. The boot image reads 2048-byte CD sectors, which is why the ISO
  should be attached as a CD-ROM (or booted through a loader such as Ventoy
  that emulates one) rather than written raw to a USB stick.
* The installed filesystem is exactly as large as `ISO_IMAGE_MB`; extra space
  on the target disk is left alone.

/*
 * lm_ata.c - a polling ATA PIO block driver for the 64-bit nucleus
 *
 * Roadmap P2's first real device. The 32-bit tree's driver does interrupt-driven
 * transfers; this one polls, because the nucleus has no way to route an IRQ to a
 * driver yet and a boot-time block device is exactly the case where polling is
 * honest: the CPU has nothing else to do.
 *
 * Everything here is LBA28 PIO on the primary channel. That covers the images
 * the port boots (2 GB, far inside the 128 GiB LBA28 limit) and is the same
 * register interface AHCI emulates in compatibility mode, so the QEMU command
 * line the test already uses needs no change.
 */

#include "lm.h"

#define ATA_DATA      0x1F0
#define ATA_ERROR     0x1F1
#define ATA_SECCOUNT  0x1F2
#define ATA_LBA0      0x1F3
#define ATA_LBA1      0x1F4
#define ATA_LBA2      0x1F5
#define ATA_DRIVE     0x1F6
#define ATA_STATUS    0x1F7
#define ATA_COMMAND   0x1F7
#define ATA_CONTROL   0x3F6

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_SECTOR_SIZE 512

static int present;
static u64 sectors;
static char model[41];

static void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static u16 inw(u16 port)
{
    u16 value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outw(u16 port, u16 value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* Reading the status port four times is the architecturally defined 400 ns
 * delay after a drive-select; there is nothing to program for it. */
static void ata_delay400(void)
{
    for (int i = 0; i < 4; i++) {
        (void)inb(ATA_STATUS);
    }
}

/* Wait until the drive is idle (or has data, when `want_data`). */
static int ata_wait(int want_data)
{
    for (u32 i = 0; i < 4000000; i++) {
        u8 status = inb(ATA_STATUS);
        if ((status & ATA_SR_ERR) != 0 || (status & ATA_SR_DF) != 0) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0) {
            if (!want_data || (status & ATA_SR_DRQ) != 0) {
                return 0;
            }
        }
    }
    return -1;                      /* the drive never answered */
}

static int ata_select_lba(u64 lba)
{
    if (lba >= (1ULL << 28)) {
        return -1;
    }
    outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay400();
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (u8)(lba & 0xFF));
    outb(ATA_LBA1, (u8)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (u8)((lba >> 16) & 0xFF));
    return 0;
}

/* IDENTIFY gives the model string and the LBA28 sector count. */
static int ata_identify(void)
{
    outb(ATA_DRIVE, 0xA0);
    ata_delay400();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    u8 status = inb(ATA_STATUS);
    if (status == 0) {
        return -1;                  /* no drive on this channel */
    }
    if (ata_wait(1) != 0) {
        return -1;
    }

    u16 words[256];
    for (int i = 0; i < 256; i++) {
        words[i] = inw(ATA_DATA);
    }

    /* Words 27..46 hold the model as big-endian characters, space padded. */
    for (int i = 0; i < 20; i++) {
        model[i * 2] = (char)(words[27 + i] >> 8);
        model[i * 2 + 1] = (char)(words[27 + i] & 0xFF);
    }
    model[40] = 0;
    for (int i = 39; i >= 0 && (model[i] == ' ' || model[i] == 0); i--) {
        model[i] = 0;
    }

    sectors = (u64)words[60] | ((u64)words[61] << 16);
    return sectors != 0 ? 0 : -1;
}

int ata_init(void)
{
    present = 0;
    sectors = 0;
    model[0] = 0;

    /* The status register reads 0xFF on a machine with no primary channel. */
    if (inb(ATA_STATUS) == 0xFF) {
        kputs("[ata] no primary channel\r\n");
        return -1;
    }
    if (ata_identify() != 0) {
        kputs("[ata] no primary master\r\n");
        return -1;
    }

    present = 1;
    kputs("[ata] model=");
    kputs(model);
    kputs(" sectors=");
    kput_dec(sectors);
    kputs("\r\n");
    return 0;
}

int ata_present(void)
{
    return present;
}

u64 ata_sector_count(void)
{
    return sectors;
}

/*
 * One retry: a drive that has just been selected can miss the first command
 * while it spins up, and the retry is cheap because nothing has been transferred
 * when ata_wait fails.
 */
int ata_read(u64 lba, void *buffer)
{
    if (!present) {
        return -1;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (ata_select_lba(lba) != 0) {
            return -1;
        }
        outb(ATA_COMMAND, ATA_CMD_READ);
        if (ata_wait(1) != 0) {
            continue;
        }
        u16 *out = buffer;
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
            out[i] = inw(ATA_DATA);
        }
        return 0;
    }
    return -1;
}

int ata_write(u64 lba, const void *buffer)
{
    if (!present) {
        return -1;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (ata_select_lba(lba) != 0) {
            return -1;
        }
        outb(ATA_COMMAND, ATA_CMD_WRITE);
        if (ata_wait(1) != 0) {
            continue;
        }
        const u16 *in = buffer;
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
            outw(ATA_DATA, in[i]);
        }
        /* The write is complete only after the drive says so; without this a
         * read-back can see the old sector. */
        outb(ATA_COMMAND, ATA_CMD_FLUSH);
        return ata_wait(0);
    }
    return -1;
}

/*
 * Self-test: read the boot sector and check its signature, then write a pattern
 * to the last sector, read it back and check it. The second half proves the
 * write path and the flush; the first proves the read path against data the
 * image really contains.
 */
int ata_selftest(void)
{
    if (!present) {
        kputs("[blk] FAIL no block device\r\n");
        return -1;
    }

    u8 sector[ATA_SECTOR_SIZE];
    int ok = 1;

    if (ata_read(0, sector) != 0) {
        ok = 0;
    } else if (sector[510] != 0x55 || sector[511] != 0xAA) {
        ok = 0;                     /* stage1's boot signature */
    }

    u64 last = sectors - 1;
    for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
        sector[i] = (u8)(i ^ 0x5A);
    }
    u8 check[ATA_SECTOR_SIZE];
    if (ata_write(last, sector) != 0 || ata_read(last, check) != 0) {
        ok = 0;
    } else {
        for (int i = 0; i < ATA_SECTOR_SIZE; i++) {
            if (check[i] != sector[i]) {
                ok = 0;
                break;
            }
        }
    }

    kputs("[blk] boot=");
    kputs(ok ? "ok" : "FAIL");
    kputs(" sectors=");
    kput_dec(sectors);
    kputs("\r\n");
    kputs(ok ? "[blk] blk=ok\r\n" : "[blk] FAIL block round trip\r\n");
    return ok ? 0 : -1;
}

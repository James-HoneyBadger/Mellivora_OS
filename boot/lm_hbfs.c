/*
 * lm_hbfs.c - the HBFS metadata read path for the 64-bit nucleus
 *
 * Roadmap P2. HBFS is the filesystem the 32-bit tree already writes (populate.py
 * builds it, kernel/hbfs.inc reads it), so the 64-bit port reads the same on-disk
 * format rather than inventing one:
 *
 *   LBA 4096          superblock (one sector)
 *   LBA 4097..4224    allocation bitmap, 16 blocks of 4 KB
 *   LBA 4225..4480    root directory, 32 blocks of 4 KB
 *   LBA 4481..        data blocks, addressed by entry.start
 *
 * Each directory entry is 288 bytes: name[0..252], type at 253, size at 256,
 * start block at 260, block count at 264. Entries are not block-aligned (14 fit
 * in a block with 64 bytes left over), so the root directory is read as one 128
 * KB buffer and scanned linearly.
 *
 * Read-only for now: the write path needs the journal and allocation rules, which
 * belong with the VFS write work, not with getting a program off the disk.
 */

#include "lm.h"

#define HBFS_SUPERBLOCK_LBA  4096
#define HBFS_ROOT_DIR_BLOCKS 32
#define HBFS_BLOCK_SIZE      4096
#define HBFS_SECTORS_PER_BLOCK (HBFS_BLOCK_SIZE / 512)
#define HBFS_DATA_START      4481
#define HBFS_DIR_ENTRY_SIZE  288
#define HBFS_MAX_FILES       455
#define HBFS_MAGIC           0x48424653U      /* 'HBFS' */
#define HBFS_FTYPE_FREE      0
#define HBFS_NAME_BYTES      252

static u8 root_dir[HBFS_ROOT_DIR_BLOCKS * HBFS_BLOCK_SIZE];
static u8 sector_buffer[512];
static int ready;
static u32 root_lba;
static u32 data_lba;
static u32 file_count;

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int read_block(u64 lba, void *buffer)
{
    u8 *p = buffer;
    for (int i = 0; i < HBFS_SECTORS_PER_BLOCK; i++) {
        if (ata_read(lba + (u64)i, p + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

int hbfs_init(void)
{
    ready = 0;
    file_count = 0;

    if (!ata_present()) {
        return -1;
    }
    if (ata_read(HBFS_SUPERBLOCK_LBA, sector_buffer) != 0) {
        return -1;
    }
    if (le32(sector_buffer) != HBFS_MAGIC) {
        kputs("[hbfs] no superblock at LBA 4096\r\n");
        return -1;
    }
    if (le32(sector_buffer + 28) != HBFS_BLOCK_SIZE) {
        kputs("[hbfs] unexpected block size\r\n");
        return -1;
    }

    root_lba = le32(sector_buffer + 16);
    data_lba = le32(sector_buffer + 24);

    for (u32 i = 0; i < HBFS_ROOT_DIR_BLOCKS; i++) {
        if (read_block((u64)root_lba + (u64)i * HBFS_SECTORS_PER_BLOCK,
                       root_dir + i * HBFS_BLOCK_SIZE) != 0) {
            kputs("[hbfs] root directory unreadable\r\n");
            return -1;
        }
    }
    for (int i = 0; i < HBFS_MAX_FILES; i++) {
        if (root_dir[i * HBFS_DIR_ENTRY_SIZE + 253] != HBFS_FTYPE_FREE) {
            file_count++;
        }
    }

    ready = 1;
    kputs("[hbfs] root=");
    kput_dec(root_lba);
    kputs(" data=");
    kput_dec(data_lba);
    kputs(" files=");
    kput_dec(file_count);
    kputs("\r\n");
    return 0;
}

int hbfs_lookup(const char *name, struct hbfs_entry *out)
{
    if (!ready) {
        return -1;
    }

    for (int i = 0; i < HBFS_MAX_FILES; i++) {
        const u8 *entry = root_dir + i * HBFS_DIR_ENTRY_SIZE;
        if (entry[253] == HBFS_FTYPE_FREE) {
            continue;
        }

        int match = 1;
        for (int n = 0; n < HBFS_NAME_BYTES; n++) {
            char want = name[n];
            u8 have = entry[n];
            if (want == '\0') {
                match = have == 0;  /* the entry's name ends where ours does */
                break;
            }
            if (have != (u8)want) {
                match = 0;
                break;
            }
        }
        if (!match) {
            continue;
        }

        out->type = entry[253];
        out->size = le32(entry + 256);
        out->start = le32(entry + 260);
        out->blocks = le32(entry + 264);
        return 0;
    }
    return -1;
}

/*
 * Copy `count` bytes at `offset` out of a file. The transfer goes through one
 * sector of bounce buffer, so a short tail never writes past the caller's
 * buffer and an unaligned offset is just a partial copy.
 */
int hbfs_read_at(const struct hbfs_entry *entry, u64 offset, void *buffer,
                 u64 count)
{
    if (offset >= entry->size) {
        return 0;
    }
    if (count > entry->size - offset) {
        count = entry->size - offset;
    }

    u8 *out = buffer;
    u64 done = 0;
    while (done < count) {
        u64 pos = offset + done;
        u64 lba = (u64)data_lba + (u64)entry->start * HBFS_SECTORS_PER_BLOCK
                + pos / 512;
        u64 in_sector = pos % 512;
        if (ata_read(lba, sector_buffer) != 0) {
            return -1;
        }
        u64 step = 512 - in_sector;
        if (step > count - done) {
            step = count - done;
        }
        for (u64 i = 0; i < step; i++) {
            out[done + i] = sector_buffer[in_sector + i];
        }
        done += step;
    }
    return (int)done;
}

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
#define HBFS_MAGIC           0x48424653U      /* 'HBFS', the volume this port writes */
/*
 * HBFS v4 keeps its superblock in the same sector and puts its own magic there,
 * so a reader can tell the two apart without knowing anything else. Its fields
 * start at 60, past everything v3 writes - the journal state and pointer at 40
 * and 44, the feature word at 48, the root uid/gid at 52 and the journal epoch
 * at 56 - so a v3 reader looking at a v4 volume still finds the geometry where
 * it expects it, and no offset means two things.
 *
 * The checksum at 76 is a CRC-32C over the whole 512-byte sector with that
 * field read as zero, the same polynomial the v3 journal uses. Nothing writes
 * v4 yet; populate.py can build one and tools/fsck_hbfs.py --complain
 * recomputes this value from the bytes on disk.
 */
#define HBFS4_MAGIC          0x34424653U      /* 'HBF4' */
#define HBFS4_OFF_FEATURES   60
#define HBFS4_OFF_GENERATION 64
#define HBFS4_OFF_BLOCKS_HI  68
#define HBFS4_OFF_DATA_HI    72
#define HBFS4_OFF_CHECKSUM   76
/*
 * Every metadata run - the bitmap, the root directory and each subdirectory -
 * keeps a CRC-32C in its last four bytes, over the run with the trailer read as
 * zero. The runs already had the room: the bitmap uses 65466 of its 65536 bytes
 * for the volume's blocks, the root directory 131040 of its 131072 for its 455
 * entries and a subdirectory 64512 of its 65536 for 224. Nothing verifies these
 * here yet; tools/fsck_hbfs.py --complain does.
 */
#define HBFS4_RUN_CHECKSUM_BYTES 4
/*
 * Extents (roadmap P4). An entry that sets HBFS4_DE_EXTENTS in its flags word
 * keeps its data in several runs instead of one: `start` then names a metadata
 * block holding {block, count} records, and `blocks` is still the number of data
 * blocks the file occupies. A file that is one run - every small file, and
 * every file this port writes - stays inline and is read exactly as it always
 * was, which is the case that must not get slower.
 */
#define HBFS4_DE_EXTENTS       0x0001U
#define HBFS4_EXTENT_MAGIC     0x34584248U   /* on disk: 'H','B','X','4' */
#define HBFS4_EXTENT_HDR       8             /* magic, record count */
#define HBFS4_EXTENT_RECORD    8             /* start block, block count */
#define HBFS4_EXTENT_MAX \
    ((HBFS_BLOCK_SIZE - HBFS4_EXTENT_HDR - HBFS4_RUN_CHECKSUM_BYTES) / \
     HBFS4_EXTENT_RECORD)

#define HBFS4_FEATURE_EXTENTS  (1U << 0)
#define HBFS4_FEATURE_JOURNAL  (1U << 1)
#define HBFS4_FEATURE_SNAPSHOT (1U << 2)

/* 3, or 4 when the volume carries the new magic. 0 before a successful mount. */
static u32 volume_version;
/*
 * A v4 volume is writable only when it advertises a journal, because a write has
 * to keep two things true at once: the runs it touches have to be restamped, and
 * a crash half way through has to be replayable. Without the feature bit the
 * mount is read-only and every write entry point refuses it; a journal that
 * cannot be replayed (a torn pre-image) also lands here, so nothing makes a
 * half-applied volume worse.
 */
static int read_only;

#define HBFS_WRITABLE (ready && !read_only)


/* Which extent block is in the buffer below, so that a read of one 4 KB block
 * does not fetch it eight times. Invalidated by a mount. */
static u32 extent_cached;
#define HBFS_NAME_BYTES      252
#define HBFS_SUBDIR_BLOCKS   16
#define HBFS_ROOT_SENTINEL   0xFFFFFFFFU       /* `start` of the root entry */

#define HBFS_BITMAP_BLOCKS   16
#define HBFS_BITMAP_BYTES    (HBFS_BITMAP_BLOCKS * HBFS_BLOCK_SIZE)
#define HBFS_JOURNAL_BLOCKS  4         /* never handed to a file */

/*
 * The v4 write-ahead journal (roadmap P4, sprint 2). An undo log in the first
 * HBFS_JOURNAL_BLOCKS data blocks: a metadata sector's old contents are written
 * there, with the target LBA, the epoch and a CRC-32C, before the new contents
 * are written; commit writes a record carrying the record count and its own
 * CRC-32C, so a torn commit cannot pass for a commit. Recovery either clears the
 * state or writes the pre-images back, and refuses to write back one whose CRC
 * fails. tools/hbfs_journal.py is the same protocol in Python and
 * tests/test_hbfs_journal.py crashes it at every write it makes.
 *
 * Only v4 volumes are journalled here: a v3 volume has no checksums, so the
 * pre-image records could not be validated, and v3 images are what the 32-bit
 * tree's own journal already covers.
 */
#define HBFS4_JRNL_MAGIC       0x4A524E4CU    /* 'JRNL', as the 32-bit tree */
#define HBFS4_JRNL_T_PRE       1
#define HBFS4_JRNL_T_COMMIT    2
#define HBFS4_JRNL_SECTS       (HBFS_JOURNAL_BLOCKS * HBFS_SECTORS_PER_BLOCK)
/* Two sectors per record, and the commit record needs one of its own. */
#define HBFS4_JRNL_MAX_RECS    ((HBFS4_JRNL_SECTS - 2) / 2)
#define HBFS4_SB_JRNL_STATE    40              /* dword: 0 clean, 1 open */
#define HBFS4_SB_JRNL_PTR      44              /* dword: next free journal sector */
#define HBFS4_SB_JRNL_EPOCH    56              /* dword: transaction counter */
#define HBFS4_JRNL_PRE_CRC     16              /* dword: CRC of the pre-image */
#define HBFS4_JRNL_OFF_RECORDS 20              /* dword, commit: record count */
#define HBFS4_JRNL_OFF_CRC     24              /* dword, commit: CRC of itself */

/* The journal's own state. One transaction at a time; the outermost caller of a
 * metadata operation owns it, which is why there is no nesting counter: nothing
 * inside this file starts a second one. */
static u32 journal_lba;          /* data area start, from the superblock */
static u32 jrnl_epoch;
static u32 jrnl_ptr;
static u32 jrnl_targets[HBFS4_JRNL_MAX_RECS];
static u32 jrnl_count;
static int jrnl_open;
static int jrnl_failed;
static u8 jrnl_sector[512];      /* pre-image or header, one at a time */
static u8 meta_sector[512];      /* the read-back used to skip unchanged sectors */
static u32 crc_table[256];

#define HBFS_MODE_FILE       0644
#define HBFS_MODE_EXEC       0755

#define HBFS_ROOT_ENTRIES \
    (HBFS_ROOT_DIR_BLOCKS * HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE)
#define HBFS_SUBDIR_ENTRIES \
    (HBFS_SUBDIR_BLOCKS * HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE)

static u8 root_dir[HBFS_ROOT_DIR_BLOCKS * HBFS_BLOCK_SIZE];
static u8 sub_dir[HBFS_SUBDIR_BLOCKS * HBFS_BLOCK_SIZE];
static u8 sector_buffer[512];
static u8 bitmap[HBFS_BITMAP_BLOCKS * HBFS_BLOCK_SIZE];
static int ready;
static u32 root_lba;
static u32 bitmap_lba;
static u32 data_lba;
static u32 file_count;
static u32 total_blocks;
static u32 free_blocks;

/*
 * A working directory per task, indexed by task id (0 is the kernel's own, used
 * before the first task runs). Two tasks listing different directories must not
 * tread on each other, and in the 32-bit tree the same variable was shared
 * because there was only ever one program running.
 */
#define HBFS_MAX_TASKS 8
#define SECTOR_SIZE     512
#define HBFS_PATH_BYTES 128

static struct hbfs_entry cwd_of[HBFS_MAX_TASKS];
static char cwd_path[HBFS_MAX_TASKS][HBFS_PATH_BYTES];
static int cwd_ready;

/* The working directory of the running task. */
struct hbfs_cwd {
    struct hbfs_entry dir;
    char *path;
};

static struct hbfs_cwd this_cwd(void)
{
    u64 id = sched_current_id();
    u32 slot = (u32)(id % HBFS_MAX_TASKS);

    if (!cwd_ready) {
        for (u32 i = 0; i < HBFS_MAX_TASKS; i++) {
            hbfs_root(&cwd_of[i]);
            cwd_path[i][0] = '/';
            cwd_path[i][1] = '\0';
        }
        cwd_ready = 1;
    }

    struct hbfs_cwd out;
    out.dir = cwd_of[slot];
    out.path = cwd_path[slot];
    return out;
}

static void set_cwd(u32 slot, const struct hbfs_entry *dir, const char *path)
{
    cwd_of[slot] = *dir;
    for (u32 i = 0; i < HBFS_PATH_BYTES; i++) {
        cwd_path[slot][i] = path[i];
        if (path[i] == '\0') {
            break;
        }
    }
    cwd_path[slot][HBFS_PATH_BYTES - 1] = '\0';
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void put32(u8 *p, u32 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
    p[2] = (u8)(value >> 16);
    p[3] = (u8)(value >> 24);
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

static int write_block(u64 lba, const void *buffer)
{
    const u8 *p = buffer;
    for (int i = 0; i < HBFS_SECTORS_PER_BLOCK; i++) {
        if (ata_write(lba + (u64)i, p + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

/*---------------------------------------
 * CRC-32C, and the journal that depends on it
 *---------------------------------------*/

/*
 * The Castagnoli polynomial, the same one the journal's commit record, every
 * pre-image and every v4 metadata trailer use. The table is built once at mount
 * because a checksum that walks a 128 KB directory bit by bit is a checksum that
 * costs more than the write it protects.
 */
static void crc_init(void)
{
    for (u32 i = 0; i < 256; i++) {
        u32 crc = i;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0x82F63B78U & (0U - (crc & 1U)));
        }
        crc_table[i] = crc;
    }
}

static u32 crc32c(const void *data, u32 length)
{
    const u8 *p = data;
    u32 crc = 0xFFFFFFFFU;
    for (u32 i = 0; i < length; i++) {
        crc = crc_table[(crc ^ p[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

/* A v4 metadata run's trailer, recomputed after its contents changed. */
static void stamp_run(u8 *run, u32 bytes)
{
    put32(run + bytes - HBFS4_RUN_CHECKSUM_BYTES, 0);
    put32(run + bytes - HBFS4_RUN_CHECKSUM_BYTES,
          crc32c(run, bytes));
}

/* The superblock keeps its checksum at a fixed offset rather than in a trailer. */
static void stamp_superblock(u8 *sb)
{
    put32(sb + HBFS4_OFF_CHECKSUM, 0);
    put32(sb + HBFS4_OFF_CHECKSUM, crc32c(sb, 512));
}

static int bytes_equal(const u8 *a, const u8 *b, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/*
 * The superblock's journal state, pointer and epoch. These writes are the
 * protocol's markers, so they are never journalled themselves - but they are
 * restamped, because on v4 the marker is inside the checksummed sector.
 */
static int sb_journal_state(int open)
{
    if (ata_read(HBFS_SUPERBLOCK_LBA, jrnl_sector) != 0) {
        return -1;
    }
    put32(jrnl_sector + HBFS4_SB_JRNL_STATE, open ? 1U : 0U);
    put32(jrnl_sector + HBFS4_SB_JRNL_PTR, open ? jrnl_ptr : 0U);
    put32(jrnl_sector + HBFS4_SB_JRNL_EPOCH, jrnl_epoch);
    if (volume_version == 4) {
        stamp_superblock(jrnl_sector);
    }
    return ata_write(HBFS_SUPERBLOCK_LBA, jrnl_sector);
}

static int journal_begin(void)
{
    if (volume_version != 4) {
        return 0;                   /* v3 is not journalled by this port */
    }
    if (jrnl_open) {
        return 0;
    }
    if (ata_read(HBFS_SUPERBLOCK_LBA, jrnl_sector) != 0) {
        return -1;
    }
    jrnl_epoch = le32(jrnl_sector + HBFS4_SB_JRNL_EPOCH) + 1;
    jrnl_ptr = 0;
    jrnl_count = 0;
    jrnl_failed = 0;
    jrnl_open = 1;
    if (sb_journal_state(1) != 0) {
        jrnl_open = 0;
        return -1;
    }
    return 0;
}

/* Save one sector's old contents. A second log of the same sector is a no-op:
 * the first record already holds the pre-transaction bytes. */
static int journal_log(u32 lba)
{
    for (u32 i = 0; i < jrnl_count; i++) {
        if (jrnl_targets[i] == lba) {
            return 0;
        }
    }
    if (!jrnl_open || jrnl_failed) {
        return -1;
    }
    if (jrnl_count >= HBFS4_JRNL_MAX_RECS) {
        jrnl_failed = 1;
        kputs("[hbfs] journal full\r\n");
        return -1;
    }
    if (ata_read(lba, jrnl_sector) != 0) {
        jrnl_failed = 1;
        return -1;
    }
    u8 header[512];
    memset(header, 0, sizeof(header));
    put32(header + 0, HBFS4_JRNL_MAGIC);
    put32(header + 4, HBFS4_JRNL_T_PRE);
    put32(header + 8, lba);
    put32(header + 12, jrnl_epoch);
    put32(header + HBFS4_JRNL_PRE_CRC, crc32c(jrnl_sector, 512));
    /* Payload first: a crash between the two writes leaves a record whose
     * header is not valid yet, and replay ignores those. */
    if (ata_write(journal_lba + jrnl_ptr + 1, jrnl_sector) != 0 ||
        ata_write(journal_lba + jrnl_ptr, header) != 0) {
        jrnl_failed = 1;
        return -1;
    }
    jrnl_targets[jrnl_count++] = lba;
    jrnl_ptr += 2;
    return 0;
}

static int journal_commit(void)
{
    if (volume_version != 4 || !jrnl_open) {
        return 0;
    }
    if (jrnl_failed) {
        return -1;
    }
    u8 header[512];
    memset(header, 0, sizeof(header));
    put32(header + 0, HBFS4_JRNL_MAGIC);
    put32(header + 4, HBFS4_JRNL_T_COMMIT);
    put32(header + 8, 0);
    put32(header + 12, jrnl_epoch);
    put32(header + HBFS4_JRNL_OFF_RECORDS, jrnl_count);
    put32(header + HBFS4_JRNL_OFF_CRC, 0);
    put32(header + HBFS4_JRNL_OFF_CRC, crc32c(header, 512));
    int result = ata_write(journal_lba + jrnl_ptr, header);
    jrnl_open = 0;
    jrnl_ptr = 0;
    jrnl_count = 0;
    if (sb_journal_state(0) != 0) {
        result = -1;
    }
    return result;
}

/* Roll back what this transaction logged, newest first. The pre-images are on
 * disk, so they are re-read rather than kept in memory. */
static void journal_abort(void)
{
    if (volume_version != 4 || !jrnl_open) {
        return;
    }
    for (u32 i = jrnl_count; i-- > 0; ) {
        if (ata_read(journal_lba + (u64)i * 2 + 1, jrnl_sector) == 0) {
            ata_write(jrnl_targets[i], jrnl_sector);
        }
    }
    jrnl_open = 0;
    jrnl_ptr = 0;
    jrnl_count = 0;
    sb_journal_state(0);
}

/*
 * A metadata sector: journal it if it is changing, then write it. A sector whose
 * bytes are already what the buffer says is not written at all, which is what
 * keeps a transaction small enough for the journal - the allocator rewrites the
 * whole bitmap otherwise.
 */
static int meta_sync(u32 lba, const u8 *data)
{
    if (ata_read(lba, meta_sector) == 0 && bytes_equal(meta_sector, data, 512)) {
        return 0;
    }
    if (volume_version == 4 && journal_log(lba) != 0) {
        return -1;
    }
    return ata_write(lba, data);
}

/*
 * Recover a volume whose last transaction may not have finished, before
 * anything else trusts its metadata. A valid commit record means the writes
 * landed and only the state needs clearing; otherwise every pre-image goes back.
 */
static int journal_replay(void)
{
    if (volume_version != 4) {
        return 0;
    }
    if (ata_read(HBFS_SUPERBLOCK_LBA, sector_buffer) != 0) {
        return -1;
    }
    u32 state = le32(sector_buffer + HBFS4_SB_JRNL_STATE);
    if (state == 0) {
        return 0;
    }
    u32 epoch = le32(sector_buffer + HBFS4_SB_JRNL_EPOCH);
    u32 records = 0;
    int committed = 0;
    int torn = 0;

    for (u32 index = 0; index + 1 < HBFS4_JRNL_SECTS; index += 2) {
        if (ata_read(journal_lba + index, jrnl_sector) != 0) {
            break;
        }
        if (le32(jrnl_sector + 0) != HBFS4_JRNL_MAGIC ||
            le32(jrnl_sector + 12) != epoch) {
            break;
        }
        u32 type = le32(jrnl_sector + 4);
        if (type == HBFS4_JRNL_T_COMMIT) {
            u32 declared = le32(jrnl_sector + HBFS4_JRNL_OFF_RECORDS);
            u32 crc = le32(jrnl_sector + HBFS4_JRNL_OFF_CRC);
            put32(jrnl_sector + HBFS4_JRNL_OFF_CRC, 0);
            committed = (declared == records) &&
                        (crc == 0 || crc32c(jrnl_sector, 512) == crc);
            break;
        }
        if (type != HBFS4_JRNL_T_PRE) {
            break;
        }
        records++;
    }

    if (!committed) {
        for (u32 i = records; i-- > 0; ) {
            if (ata_read(journal_lba + i * 2, meta_sector) != 0 ||
                ata_read(journal_lba + i * 2 + 1, jrnl_sector) != 0) {
                torn = 1;
                break;
            }
            u32 crc = le32(meta_sector + HBFS4_JRNL_PRE_CRC);
            if (crc != 0 && crc32c(jrnl_sector, 512) != crc) {
                torn = 1;
                break;
            }
            if (ata_write(le32(meta_sector + 8), jrnl_sector) != 0) {
                torn = 1;
                break;
            }
        }
    }

    if (torn) {
        /* Writing back a pre-image that failed its own checksum would trade a
         * half-applied transaction for a corrupt volume. Leave it alone and
         * mount read-only so nothing makes it worse. */
        kputs("[hbfs] journal=unreplayable (torn pre-image), mounting read-only\r\n");
        return 1;
    }

    /* Re-read: if the superblock was one of the sectors rolled back, the copy
     * read at entry holds the modified free count, and patching that copy would
     * quietly undo the undo. */
    if (ata_read(HBFS_SUPERBLOCK_LBA, jrnl_sector) != 0) {
        return -1;
    }
    put32(jrnl_sector + HBFS4_SB_JRNL_STATE, 0);
    put32(jrnl_sector + HBFS4_SB_JRNL_PTR, 0);
    if (volume_version == 4) {
        stamp_superblock(jrnl_sector);
    }
    if (ata_write(HBFS_SUPERBLOCK_LBA, jrnl_sector) != 0) {
        return -1;
    }
    kputs("[hbfs] journal=");
    kputs(committed ? "committed" : "rolled-back");
    kputs(" records=");
    kput_dec(records);
    kputs("\r\n");
    return 0;
}

int hbfs_init(void)
{
    ready = 0;
    read_only = 0;
    jrnl_open = 0;
    extent_cached = HBFS_ROOT_SENTINEL;
    file_count = 0;

    if (!ata_present()) {
        return -1;
    }
    if (ata_read(HBFS_SUPERBLOCK_LBA, sector_buffer) != 0) {
        return -1;
    }
    crc_init();

    u32 magic = le32(sector_buffer);
    if (magic == HBFS_MAGIC) {
        volume_version = 3;
    } else if (magic == HBFS4_MAGIC) {
        volume_version = 4;
        /* Writable only if the volume says it has a journal: without one the
         * writer could not keep the checksums true across a crash. */
        read_only = (le32(sector_buffer + HBFS4_OFF_FEATURES)
                     & HBFS4_FEATURE_JOURNAL) == 0;
    } else {
        kputs("[hbfs] no superblock at LBA 4096\r\n");
        return -1;
    }
    if (le32(sector_buffer + 28) != HBFS_BLOCK_SIZE) {
        kputs("[hbfs] unexpected block size\r\n");
        return -1;
    }

    root_lba = le32(sector_buffer + 16);
    bitmap_lba = le32(sector_buffer + 20);
    data_lba = le32(sector_buffer + 24);
    total_blocks = le32(sector_buffer + 8);
    free_blocks = le32(sector_buffer + 12);
    journal_lba = data_lba;

    /* Before anything trusts the metadata: finish or undo the last transaction.
     * Then re-read the superblock, because replay may have restored it. */
    int replayed = journal_replay();
    if (replayed < 0) {
        kputs("[hbfs] journal replay failed\r\n");
        return -1;
    }
    if (replayed > 0) {
        read_only = 1;
    }
    if (ata_read(HBFS_SUPERBLOCK_LBA, sector_buffer) != 0) {
        return -1;
    }
    free_blocks = le32(sector_buffer + 12);

    for (u32 i = 0; i < HBFS_BITMAP_BLOCKS; i++) {
        if (read_block((u64)bitmap_lba + (u64)i * HBFS_SECTORS_PER_BLOCK,
                       bitmap + i * HBFS_BLOCK_SIZE) != 0) {
            kputs("[hbfs] allocation bitmap unreadable\r\n");
            return -1;
        }
    }

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

    cwd_ready = 0;      /* every task starts at the root */

    kputs("[hbfs] root=");
    kput_dec(root_lba);
    kputs(" data=");
    kput_dec(data_lba);
    kputs(" files=");
    kput_dec(file_count);
    kputs(" free=");
    kput_dec(free_blocks);
    kputs(" version=");
    kput_dec(volume_version);
    if (volume_version == 4) {
        kputs(" features=0x");
        kput_hex(le32(sector_buffer + HBFS4_OFF_FEATURES), 8);
        if (read_only) {
            kputs(" ro");
        }
    }
    kputs("\r\n");
    return 0;
}

u32 hbfs_version(void)
{
    return volume_version;
}

/*
 * Read one directory into a buffer and say how many entries it holds.
 *
 * The root directory's blocks are found through the superblock and a
 * subdirectory's through its entry's data-relative block number, so both land
 * in a flat buffer that the 288-byte entries can be scanned out of - they are
 * not block-aligned, and an entry can straddle two blocks.
 */
static u8 *dir_buffer(const struct hbfs_entry *dir, u32 *entries, u32 *lba,
                      u32 *blocks)
{
    if (dir->start == HBFS_ROOT_SENTINEL) {
        *entries = HBFS_ROOT_ENTRIES;
        *lba = root_lba;
        *blocks = HBFS_ROOT_DIR_BLOCKS;
        return root_dir;
    }
    if (dir->type != HBFS_FTYPE_DIR || dir->blocks == 0 ||
        dir->blocks > HBFS_SUBDIR_BLOCKS) {
        return NULL;
    }

    for (u32 i = 0; i < dir->blocks; i++) {
        u64 block_lba = (u64)data_lba
                      + (u64)dir->start * HBFS_SECTORS_PER_BLOCK
                      + (u64)i * HBFS_SECTORS_PER_BLOCK;
        if (read_block(block_lba, sub_dir + i * HBFS_BLOCK_SIZE) != 0) {
            return NULL;
        }
    }
    *entries = dir->blocks * HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE;
    *lba = (u32)(data_lba + dir->start * HBFS_SECTORS_PER_BLOCK);
    *blocks = dir->blocks;
    return sub_dir;
}

static const u8 *dir_data(const struct hbfs_entry *dir, u32 *entries)
{
    u32 lba, blocks;
    return dir_buffer(dir, entries, &lba, &blocks);
}

static void entry_from(const u8 *raw, struct hbfs_entry *out)
{
    out->type = raw[253];
    out->size = le32(raw + 256);
    out->start = le32(raw + 260);
    out->blocks = le32(raw + 264);
    out->flags = (u32)raw[254] | ((u32)raw[255] << 8);
}

/* Compare a NUL-terminated wanted name against an entry's fixed-width one. */
static int name_matches(const u8 *raw, const char *name)
{
    for (int n = 0; n < HBFS_NAME_BYTES; n++) {
        char want = name[n];
        u8 have = raw[n];
        if (want == '\0') {
            return have == 0;
        }
        if (have != (u8)want) {
            return 0;
        }
    }
    return 1;
}

void hbfs_root(struct hbfs_entry *out)
{
    out->type = HBFS_FTYPE_DIR;
    out->size = 0;
    out->start = HBFS_ROOT_SENTINEL;
    out->blocks = HBFS_ROOT_DIR_BLOCKS;
    out->flags = 0;
}

static int hbfs_lookup_in_locked(const struct hbfs_entry *dir, const char *name,
                   struct hbfs_entry *out)
{
    if (!ready) {
        return -1;
    }

    u32 entries;
    const u8 *data = dir_data(dir, &entries);
    if (data == NULL) {
        return -1;
    }

    for (u32 i = 0; i < entries; i++) {
        const u8 *raw = data + i * HBFS_DIR_ENTRY_SIZE;
        if (raw[253] == HBFS_FTYPE_FREE || !name_matches(raw, name)) {
            continue;
        }
        entry_from(raw, out);
        return 0;
    }
    return -1;
}

int hbfs_lookup_in(const struct hbfs_entry *dir, const char *name,
                         struct hbfs_entry *out)
{
    sched_lock();
    int result = hbfs_lookup_in_locked(dir, name, out);
    sched_unlock();
    return result;
}

int hbfs_lookup(const char *name, struct hbfs_entry *out)
{
    struct hbfs_entry root;
    hbfs_root(&root);
    return hbfs_lookup_in(&root, name, out);
}

static int hbfs_readdir_locked(const struct hbfs_entry *dir, u32 index,
                 struct hbfs_entry *out, char *name, u32 name_bytes)
{
    u32 entries;
    const u8 *data = dir_data(dir, &entries);
    if (data == NULL) {
        return -1;
    }
    if ((u64)index * HBFS_DIR_ENTRY_SIZE >= (u64)entries * HBFS_DIR_ENTRY_SIZE) {
        return -1;
    }

    const u8 *raw = data + index * HBFS_DIR_ENTRY_SIZE;
    entry_from(raw, out);
    if (name != NULL && name_bytes > 0) {
        u32 n = 0;
        for (; n + 1 < name_bytes && n < HBFS_NAME_BYTES; n++) {
            name[n] = (char)raw[n];
            if (name[n] == '\0') {
                break;
            }
        }
        name[n] = '\0';
    }
    return out->type == HBFS_FTYPE_FREE ? 0 : 1;
}

int hbfs_readdir(const struct hbfs_entry *dir, u32 index,
                 struct hbfs_entry *out, char *name, u32 name_bytes)
{
    sched_lock();
    int result = hbfs_readdir_locked(dir, index, out, name, name_bytes);
    sched_unlock();
    return result;
}

/* Drop trailing separators, then either keep an absolute path or prefix the
 * working directory. */
static int build_path(const char *path, char *out, u32 out_bytes)
{
    int absolute = path[0] == '/';
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    u32 n = 0;
    struct hbfs_cwd cwd = this_cwd();
    if (!absolute && cwd.dir.start != HBFS_ROOT_SENTINEL) {
        /* Relative name inside the working directory. */
        for (const char *p = cwd.path; *p != '\0'; p++) {
            if (n + 1 >= out_bytes) {
                return -1;
            }
            out[n++] = *p;
        }
        while (n > 1 && out[n - 1] == '/') {
            n--;
        }
        if (n + 1 < out_bytes) {
            out[n++] = '/';
        }
    }
    for (; *path != '\0'; path++) {
        if (n + 1 >= out_bytes) {
            return -1;
        }
        out[n++] = *path;
    }
    out[n] = '\0';
    return 0;
}

/* Make `path` start at the root, resolving it against the working directory
 * first: "docs" in /docs is /docs/docs, "docs" in / is /docs. */
static int canonical_path(const char *path, char *out, u32 out_bytes)
{
    if (path[0] == '/') {
        return build_path(path, out, out_bytes);
    }
    struct hbfs_cwd cwd = this_cwd();
    if (cwd.dir.start == HBFS_ROOT_SENTINEL) {
        out[0] = '/';
        return build_path(path, out + 1, out_bytes - 1) == 0 ? 0 : -1;
    }

    u32 base = 0;
    for (const char *p = cwd.path; *p != '\0' && base + 1 < out_bytes; p++) {
        out[base++] = *p;
    }
    if (base + 1 >= out_bytes) {
        return -1;
    }
    out[base++] = '/';
    for (; *path != '\0' && base + 1 < out_bytes; path++) {
        out[base++] = *path;
    }
    out[base] = '\0';
    return 0;
}

static int split_first(const char *path, char *component, u32 bytes,
                       const char **rest)
{
    u32 n = 0;
    while (*path != '\0' && *path != '/') {
        if (n + 1 >= bytes) {
            return -1;
        }
        component[n++] = *path++;
    }
    component[n] = '\0';
    while (*path == '/') {
        path++;
    }
    *rest = path;
    return (int)n;
}

int hbfs_resolve(const char *path, struct hbfs_entry *out)
{
    if (!ready) {
        return -1;
    }

    char full[128];
    if (build_path(path, full, sizeof(full)) != 0) {
        return -1;
    }
    struct hbfs_entry dir;
    hbfs_root(&dir);
    if (full[0] == '\0') {
        *out = dir;
        return 0;
    }

    const char *rest = full;
    while (*rest == '/') {
        rest++;
    }
    char component[HBFS_NAME_BYTES + 1];
    for (;;) {
        int n = split_first(rest, component, sizeof(component), &rest);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;      /* path was "/" or ended in a separator */
        }
        if (n == 1 && component[0] == '.') {
            if (*rest == '\0') {
                break;
            }
            continue;
        }
        if (n == 2 && component[0] == '.' && component[1] == '.') {
            hbfs_root(&dir);
            if (*rest == '\0') {
                break;
            }
            continue;
        }
        if (hbfs_lookup_in(&dir, component, &dir) != 0) {
            return -1;
        }
        if (*rest == '\0') {
            break;
        }
        if (dir.type != HBFS_FTYPE_DIR) {
            return -1;
        }
    }

    *out = dir;
    return 0;
}

int hbfs_chdir(const char *path)
{
    struct hbfs_entry dir;
    if (hbfs_resolve(path, &dir) != 0) {
        return -1;
    }
    if (dir.type != HBFS_FTYPE_DIR) {
        return -1;
    }

    u32 slot = (u32)(sched_current_id() % HBFS_MAX_TASKS);
    if (dir.start == HBFS_ROOT_SENTINEL) {
        set_cwd(slot, &dir, "/");
        return 0;
    }

    char full[HBFS_PATH_BYTES];
    if (canonical_path(path, full, sizeof(full)) != 0) {
        return -1;
    }
    set_cwd(slot, &dir, full);
    return 0;
}

const char *hbfs_getcwd(void)
{
    return this_cwd().path;
}

/*
 * Which data block holds block number `block` of a file.
 *
 * An inline entry is one run starting at entry->start, so this is an addition;
 * an extent entry names a block of {start, count} records and the block is
 * found by walking them. The extent block is cached on its number because a
 * read of one 4 KB block asks eight times whether it has moved.
 */
static int file_block(const struct hbfs_entry *entry, u32 block, u32 *out)
{
    if ((entry->flags & HBFS4_DE_EXTENTS) == 0) {
        *out = entry->start + block;
        return 0;
    }

    static u8 extent_buffer[HBFS_BLOCK_SIZE];
    if (extent_cached != entry->start) {
        if (read_block((u64)data_lba + (u64)entry->start * HBFS_SECTORS_PER_BLOCK,
                       extent_buffer) != 0) {
            return -1;
        }
        if (le32(extent_buffer) != HBFS4_EXTENT_MAGIC) {
            extent_cached = HBFS_ROOT_SENTINEL;
            return -1;
        }
        extent_cached = entry->start;
    }

    u32 count = le32(extent_buffer + 4);
    if (count == 0 || count > HBFS4_EXTENT_MAX) {
        return -1;
    }
    u32 at = 0;
    for (u32 i = 0; i < count; i++) {
        const u8 *record = extent_buffer + HBFS4_EXTENT_HDR + i * HBFS4_EXTENT_RECORD;
        u32 start = le32(record);
        u32 blocks = le32(record + 4);
        if (blocks == 0 || blocks > entry->blocks) {
            return -1;      /* a corrupt record, not a shorter file */
        }
        if (block >= at && block - at < blocks) {
            *out = start + (block - at);
            return 0;
        }
        at += blocks;
    }
    return -1;
}

/*
 * Copy `count` bytes at `offset` out of a file. The transfer goes through one
 * sector of bounce buffer, so a short tail never writes past the caller's
 * buffer and an unaligned offset is just a partial copy.
 */
static int hbfs_read_at_locked(const struct hbfs_entry *entry, u64 offset, void *buffer,
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
        u32 physical;
        if (file_block(entry, (u32)(pos / HBFS_BLOCK_SIZE), &physical) != 0) {
            return -1;
        }
        u64 lba = (u64)data_lba + (u64)physical * HBFS_SECTORS_PER_BLOCK
                + (pos % HBFS_BLOCK_SIZE) / 512;
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

int hbfs_read_at(const struct hbfs_entry *entry, u64 offset, void *buffer,
                 u64 count)
{
    sched_lock();
    int result = hbfs_read_at_locked(entry, offset, buffer, count);
    sched_unlock();
    return result;
}

/*---------------------------------------
 * Writing: blocks, entries, files
 *---------------------------------------*/

/*
 * A directory being modified. The buffer is one of the two statics, so a second
 * open_dir() invalidates the first: everything below takes the handle rather
 * than re-deriving it, which is what keeps a modification from being thrown
 * away by a reload.
 */
struct hbfs_dirbuf {
    u8 *data;
    u32 entries;
    u32 lba;
    u32 blocks;
};

static int open_dir(const struct hbfs_entry *dir, struct hbfs_dirbuf *db)
{
    db->data = dir_buffer(dir, &db->entries, &db->lba, &db->blocks);
    return db->data != NULL ? 0 : -1;
}

static int sync_dirbuf(const struct hbfs_dirbuf *db)
{
    u32 bytes = db->blocks * HBFS_BLOCK_SIZE;
    if (volume_version == 4) {
        /* A directory entry does not align to a sector, so the trailer of the
         * run has to be restamped here, where the whole run is in one buffer. */
        stamp_run(db->data, bytes);
    }
    for (u32 sector = 0; sector < bytes / 512; sector++) {
        u32 lba = db->lba + sector;
        if (meta_sync(lba, db->data + sector * 512) != 0) {
            kputs("[hbfs] FAIL the directory write at sector ");
            kput_dec(lba);
            kputs("\r\n");
            return -1;
        }
    }
    return 0;
}

static int block_used(u32 block)
{
    return (bitmap[block / 8] & (1u << (block % 8))) != 0;
}

static void mark_block(u32 block, int used)
{
    if (used) {
        bitmap[block / 8] |= (u8)(1u << (block % 8));
    } else {
        bitmap[block / 8] &= (u8)~(1u << (block % 8));
    }
}

/* Write the superblock's free count and the whole bitmap back. The count is the
 * one field the 32-bit tree keeps in the superblock, so it stays right for
 * whoever mounts this volume next. */
static int sync_allocator(void)
{
    if (volume_version == 4) {
        stamp_run(bitmap, HBFS_BITMAP_BYTES);
    }
    /* Sector by sector, so that only what changed is journalled and written. */
    for (u32 sector = 0; sector < HBFS_BITMAP_BYTES / 512; sector++) {
        if (meta_sync(bitmap_lba + sector, bitmap + sector * 512) != 0) {
            return -1;
        }
    }

    if (ata_read(HBFS_SUPERBLOCK_LBA, sector_buffer) != 0) {
        return -1;
    }
    put32(sector_buffer + 12, free_blocks);
    if (volume_version == 4) {
        stamp_superblock(sector_buffer);
    }
    if (meta_sync(HBFS_SUPERBLOCK_LBA, sector_buffer) != 0) {
        return -1;
    }
    return 0;
}

/*
 * Allocate `count` contiguous data blocks, returning the first one. Contiguous
 * because a directory entry stores one start block and a count - there is no
 * extents list to fall back on.
 */
static int alloc_blocks(u32 count, u32 *start)
{
    u32 run = 0;
    u32 run_start = 0;

    if (free_blocks < count) {
        return -1;
    }

    for (u32 b = HBFS_JOURNAL_BLOCKS; b < total_blocks; b++) {
        if (block_used(b)) {
            run = 0;
            continue;
        }
        if (run == 0) {
            run_start = b;
        }
        if (++run == count) {
            for (u32 i = 0; i < count; i++) {
                mark_block(run_start + i, 1);
            }
            free_blocks -= count;
            *start = run_start;
            return 0;
        }
    }
    return -1;
}

static void release_blocks(u32 start, u32 count)
{
    for (u32 i = 0; i < count; i++) {
        if (start + i < total_blocks && block_used(start + i)) {
            mark_block(start + i, 0);
            free_blocks++;
        }
    }
}

/* Write `size` bytes at data block `start`, leaving the rest of the last block
 * zeroed so a later read of the slack cannot see an older file's bytes. */
static int write_data(u32 start, const void *data, u32 size, u32 blocks)
{
    const u8 *in = data;
    u32 done = 0;

    for (u32 i = 0; i < blocks; i++) {
        u64 lba = (u64)data_lba + (u64)(start + i) * HBFS_SECTORS_PER_BLOCK;
        for (u32 s = 0; s < HBFS_SECTORS_PER_BLOCK; s++) {
            memset(sector_buffer, 0, sizeof(sector_buffer));
            for (u32 k = 0; k < 512 && done + k < size; k++) {
                sector_buffer[k] = in[done + k];
            }
            if (ata_write(lba + s, sector_buffer) != 0) {
                return -1;
            }
            done += 512;
            if (done >= size) {
                done = size;
            }
        }
    }
    return 0;
}

static void entry_to(u8 *raw, const char *name, u32 type, u32 size, u32 start,
                     u32 blocks)
{
    memset(raw, 0, HBFS_DIR_ENTRY_SIZE);
    for (u32 i = 0; i < HBFS_NAME_BYTES && name[i] != '\0'; i++) {
        raw[i] = (u8)name[i];
    }
    raw[253] = (u8)type;
    put32(raw + 256, size);
    put32(raw + 260, start);
    put32(raw + 264, blocks);
    put32(raw + 268, 0);            /* created: no clock yet */
    put32(raw + 272, 0);            /* modified */
    u16 mode = (type == HBFS_FTYPE_DIR || type == HBFS_FTYPE_EXEC)
             ? HBFS_MODE_EXEC : HBFS_MODE_FILE;
    raw[276] = (u8)mode;
    raw[277] = (u8)(mode >> 8);
}

/* The entry for `name` in an open directory: its index, or -1. */
static int find_entry(const struct hbfs_dirbuf *db, const char *name,
                      struct hbfs_entry *out, u32 *index)
{
    for (u32 i = 0; i < db->entries; i++) {
        const u8 *raw = db->data + i * HBFS_DIR_ENTRY_SIZE;
        if (raw[253] == HBFS_FTYPE_FREE || !name_matches(raw, name)) {
            continue;
        }
        if (out != NULL) {
            entry_from(raw, out);
        }
        if (index != NULL) {
            *index = i;
        }
        return 0;
    }
    return -1;
}

static int first_free(const struct hbfs_dirbuf *db)
{
    for (u32 i = 0; i < db->entries; i++) {
        if (db->data[i * HBFS_DIR_ENTRY_SIZE + 253] == HBFS_FTYPE_FREE) {
            return (int)i;
        }
    }
    return -1;
}

/* Split a path into the directory that would hold it and its last component. */
static int split_parent(const char *path, struct hbfs_entry *parent, char *name)
{
    char full[HBFS_PATH_BYTES];
    if (build_path(path, full, sizeof(full)) != 0) {
        return -1;
    }

    const char *rest = full;
    while (*rest == '/') {
        rest++;
    }
    if (*rest == '\0') {
        return -1;                  /* the root has no parent */
    }

    struct hbfs_entry dir;
    hbfs_root(&dir);
    char component[HBFS_NAME_BYTES + 1];
    for (;;) {
        const char *next;
        int n = split_first(rest, component, sizeof(component), &next);
        if (n <= 0) {
            return -1;
        }
        if (*next == '\0') {
            for (u32 i = 0; i < HBFS_NAME_BYTES; i++) {
                name[i] = component[i];
                if (component[i] == '\0') {
                    break;
                }
            }
            break;
        }
        if (!(n == 1 && component[0] == '.') &&
            !(n == 2 && component[0] == '.' && component[1] == '.')) {
            if (hbfs_lookup_in(&dir, component, &dir) != 0) {
                return -1;
            }
        }
        rest = next;
    }

    if (dir.type != HBFS_FTYPE_DIR || name[0] == '\0') {
        return -1;
    }
    *parent = dir;
    return 0;
}

/*
 * Writing into an existing file, which is what makes `open` worth having: a
 * whole-file CREATE throws the old contents away, and a program that appends a
 * line - a log, a note, the tree's own `bcal` - has to be able to keep them.
 *
 * HBFS stores a file in one contiguous run of blocks, so a write can either
 * patch sectors in place or have to grow the run, and growing in place is only
 * possible when the blocks after it are free. Either way the file's size and
 * block count live in its directory entry, so the entry is rewritten at the end.
 */

/* Write `size` bytes at `offset` inside the extent that starts at `start`,
 * reading and rewriting any sector the write does not cover completely.
 * `zero_tail` clears the rest of the last sector, so the slack past a file's end
 * never shows what an older file left there. */
static int write_extent(u32 start, u64 offset, const void *data, u32 size,
                        int zero_tail)
{
    const u8 *in = data;
    u32 done = 0;

    while (done < size) {
        u64 at = offset + (u64)done;
        u32 sector = (u32)(at / SECTOR_SIZE);
        u32 within = (u32)(at % SECTOR_SIZE);
        u32 chunk = SECTOR_SIZE - within;
        if (chunk > size - done) {
            chunk = size - done;
        }
        u64 lba = (u64)data_lba
                + (u64)start * HBFS_SECTORS_PER_BLOCK + sector;

        if (within == 0 && chunk == SECTOR_SIZE) {
            memcpy(sector_buffer, in + done, SECTOR_SIZE);
        } else {
            if (ata_read(lba, sector_buffer) != 0) {
                return -1;
            }
            memcpy(sector_buffer + within, in + done, chunk);
            if (zero_tail && done + chunk == size) {
                memset(sector_buffer + within + chunk, 0,
                       SECTOR_SIZE - within - chunk);
            }
        }
        if (ata_write(lba, sector_buffer) != 0) {
            return -1;
        }
        done += chunk;
    }
    return 0;
}

/* Copy `bytes` from one extent to another, a sector at a time. */
static int copy_extent(u32 from, u32 to, u64 bytes)
{
    for (u64 done = 0; done < bytes; done += SECTOR_SIZE) {
        u64 src = (u64)data_lba + (u64)from * HBFS_SECTORS_PER_BLOCK
                + done / SECTOR_SIZE;
        u64 dst = (u64)data_lba + (u64)to * HBFS_SECTORS_PER_BLOCK
                + done / SECTOR_SIZE;
        if (ata_read(src, sector_buffer) != 0) {
            return -1;
        }
        if (ata_write(dst, sector_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Take `count` free blocks directly after `after`, or -1 if that is not
 * possible. Cheaper than moving the file, so it is tried first. */
static int extend_in_place(u32 after, u32 count)
{
    if (after + count >= total_blocks) {
        return -1;
    }
    for (u32 i = 0; i < count; i++) {
        if (block_used(after + i)) {
            return -1;
        }
    }
    for (u32 i = 0; i < count; i++) {
        mark_block(after + i, 1);
    }
    free_blocks -= count;
    return 0;
}

static int hbfs_write_locked(const char *path, u64 offset, const void *data,
                             u32 size)
{
    if (!HBFS_WRITABLE || size == 0) {
        return -1;
    }

    struct hbfs_entry parent;
    char name[HBFS_NAME_BYTES + 1];
    if (split_parent(path, &parent, name) != 0) {
        return -1;
    }

    struct hbfs_dirbuf db;
    if (open_dir(&parent, &db) != 0) {
        return -1;
    }

    struct hbfs_entry entry;
    u32 index;
    if (find_entry(&db, name, &entry, &index) != 0 ||
        entry.type == HBFS_FTYPE_DIR) {
        return -1;
    }

    u64 end = offset + (u64)size;
    u64 new_size = end > entry.size ? end : entry.size;
    u32 need = (u32)((new_size + HBFS_BLOCK_SIZE - 1) / HBFS_BLOCK_SIZE);
    u32 start = entry.start;
    u32 blocks = entry.blocks;

    if (need > blocks) {
        u32 grow = need - blocks;
        if (extend_in_place(entry.start + blocks, grow) != 0) {
            u32 moved = 0;
            if (alloc_blocks(need, &moved) != 0) {
                return -1;
            }
            if (copy_extent(entry.start, moved, entry.size) != 0) {
                release_blocks(moved, need);
                return -1;
            }
            release_blocks(entry.start, entry.blocks);
            start = moved;
        }
        blocks = need;
    }

    if (write_extent(start, offset, data, size, end >= new_size) != 0) {
        return -1;
    }

    entry_to(db.data + index * HBFS_DIR_ENTRY_SIZE, name, entry.type,
             (u32)new_size, start, blocks);
    if (sync_dirbuf(&db) != 0 || sync_allocator() != 0) {
        return -1;
    }
    return 0;
}

int hbfs_write(const char *path, u64 offset, const void *data, u32 size)
{
    sched_lock();
    int result = -1;
    if (journal_begin() == 0) {
        if (hbfs_write_locked(path, offset, data, size) == 0) {
            result = journal_commit();
        } else {
            journal_abort();
        }
    }
    sched_unlock();
    return result;
}

static int hbfs_create_locked(const char *path, u32 type, const void *data, u32 size)
{
    if (!HBFS_WRITABLE || size == 0) {
        return -1;
    }

    struct hbfs_entry parent;
    char name[HBFS_NAME_BYTES + 1];
    if (split_parent(path, &parent, name) != 0) {
        return -1;
    }

    struct hbfs_dirbuf db;
    if (open_dir(&parent, &db) != 0) {
        return -1;
    }

    /* Replacing a name frees its old blocks first, so re-running a program does
     * not leak the volume one copy at a time. */
    struct hbfs_entry old;
    u32 index;
    int exists = find_entry(&db, name, &old, &index) == 0;
    if (exists) {
        if (old.type == HBFS_FTYPE_DIR) {
            return -1;
        }
        release_blocks(old.start, old.blocks);
    } else {
        int slot = first_free(&db);
        if (slot < 0) {
            return -1;              /* directory full */
        }
        index = (u32)slot;
    }

    u32 blocks = (size + HBFS_BLOCK_SIZE - 1) / HBFS_BLOCK_SIZE;
    u32 start = 0;
    if (alloc_blocks(blocks, &start) != 0) {
        return -1;
    }
    if (write_data(start, data, size, blocks) != 0) {
        release_blocks(start, blocks);
        return -1;
    }

    entry_to(db.data + index * HBFS_DIR_ENTRY_SIZE, name, type, size, start,
             blocks);
    if (sync_dirbuf(&db) != 0) {
        return -1;
    }
    if (sync_allocator() != 0) {
        return -1;
    }
    return 0;
}

int hbfs_create(const char *path, u32 type, const void *data, u32 size)
{
    sched_lock();
    int result = -1;
    if (journal_begin() == 0) {
        if (hbfs_create_locked(path, type, data, size) == 0) {
            result = journal_commit();
        } else {
            journal_abort();
        }
    }
    sched_unlock();
    return result;
}

static int hbfs_mkdir_locked(const char *path)
{
    if (!HBFS_WRITABLE) {
        return -1;
    }

    struct hbfs_entry parent;
    char name[HBFS_NAME_BYTES + 1];
    if (split_parent(path, &parent, name) != 0) {
        return -1;
    }

    struct hbfs_dirbuf db;
    if (open_dir(&parent, &db) != 0) {
        return -1;
    }

    struct hbfs_entry existing;
    if (find_entry(&db, name, &existing, NULL) == 0) {
        return existing.type == HBFS_FTYPE_DIR ? 0 : -1;
    }
    int slot = first_free(&db);
    if (slot < 0) {
        return -1;
    }

    u32 start = 0;
    if (alloc_blocks(HBFS_SUBDIR_BLOCKS, &start) != 0) {
        return -1;
    }
    /* A fresh directory's entries must all read as free, so zero its blocks. */
    static u8 zero[HBFS_BLOCK_SIZE];
    memset(zero, 0, sizeof(zero));
    for (u32 i = 0; i < HBFS_SUBDIR_BLOCKS; i++) {
        u64 lba = (u64)data_lba
                + (u64)(start + i) * HBFS_SECTORS_PER_BLOCK;
        if (write_block(lba, zero) != 0) {
            release_blocks(start, HBFS_SUBDIR_BLOCKS);
            return -1;
        }
    }

    entry_to(db.data + (u32)slot * HBFS_DIR_ENTRY_SIZE, name, HBFS_FTYPE_DIR,
             0, start, HBFS_SUBDIR_BLOCKS);
    if (sync_dirbuf(&db) != 0 || sync_allocator() != 0) {
        return -1;
    }
    return 0;
}

int hbfs_mkdir(const char *path)
{
    sched_lock();
    int result = -1;
    if (journal_begin() == 0) {
        if (hbfs_mkdir_locked(path) == 0) {
            result = journal_commit();
        } else {
            journal_abort();
        }
    }
    sched_unlock();
    return result;
}

static int hbfs_remove_locked(const char *path)
{
    if (!HBFS_WRITABLE) {
        return -1;
    }

    struct hbfs_entry parent;
    char name[HBFS_NAME_BYTES + 1];
    if (split_parent(path, &parent, name) != 0) {
        return -1;
    }

    struct hbfs_dirbuf db;
    if (open_dir(&parent, &db) != 0) {
        return -1;
    }

    struct hbfs_entry entry;
    u32 index;
    if (find_entry(&db, name, &entry, &index) != 0) {
        return -1;
    }
    if (entry.type == HBFS_FTYPE_DIR) {
        /* A directory can go, but only when nothing is left in it: a recursive
         * delete would have to walk the tree, and a caller that wanted one can
         * walk it itself with readdir. The parent's entry is in one buffer and
         * the child's blocks load into the other, so this cannot clobber it. */
        u32 child_entries;
        const u8 *child = dir_data(&entry, &child_entries);
        if (child == NULL) {
            return -1;
        }
        for (u32 i = 0; i < child_entries; i++) {
            if (child[i * HBFS_DIR_ENTRY_SIZE + 253] != HBFS_FTYPE_FREE) {
                return -1;          /* not empty */
            }
        }
    }

    release_blocks(entry.start, entry.blocks);
    memset(db.data + index * HBFS_DIR_ENTRY_SIZE, 0, HBFS_DIR_ENTRY_SIZE);
    if (sync_dirbuf(&db) != 0 || sync_allocator() != 0) {
        return -1;
    }
    return 0;
}

int hbfs_remove(const char *path)
{
    sched_lock();
    int result = -1;
    if (journal_begin() == 0) {
        if (hbfs_remove_locked(path) == 0) {
            result = journal_commit();
        } else {
            journal_abort();
        }
    }
    sched_unlock();
    return result;
}

/*
 * A write self-test, run once at boot before any task exists. Two scratch files
 * are created side by side, so the block after the first one belongs to the
 * second; appending to the first therefore *cannot* grow it in place and has to
 * move it - which is the path a person would never think to try and the one most
 * likely to leak a block or lose a sector. Both files are removed afterwards, so
 * the volume the host test reads is the one the demos wrote.
 */
int hbfs_writetest(void)
{
    static u8 first[4000];
    static u8 more[200];
    static u8 check[4200];

    for (u32 i = 0; i < sizeof(first); i++) {
        first[i] = (u8)('a' + (i % 26));
    }
    memset(more, 'Z', sizeof(more));

    /* Nothing below may leak: every block taken is given back when the scratch
     * files go, so the count has to be exactly where it started. */
    u32 before = free_blocks;

    if (read_only) {
        kputs("[hbfs] append=skipped (read-only volume)\r\n");
        return 0;
    }

    if (hbfs_create("/grow1", HBFS_FTYPE_FILE, first, sizeof(first)) != 0 ||
        hbfs_create("/grow2", HBFS_FTYPE_FILE, first, sizeof(first)) != 0) {
        kputs("[hbfs] FAIL the write test could not create its files\r\n");
        return -1;
    }

    /* 4000 bytes fit in one block; 200 more need a second one. */
    struct hbfs_entry entry;
    int grown = hbfs_write("/grow1", sizeof(first), more, sizeof(more));
    if (grown != 0 || hbfs_resolve("/grow1", &entry) != 0) {
        kputs("[hbfs] FAIL appending to a file that has to move\r\n");
        return -1;
    }
    if (entry.size != sizeof(first) + sizeof(more) || entry.blocks != 2) {
        kputs("[hbfs] FAIL the appended file is the wrong size\r\n");
        return -1;
    }

    /* The old bytes have to survive the move and the new ones have to be there. */
    sched_lock();
    int got = hbfs_read_at(&entry, 0, check, sizeof(check));
    sched_unlock();
    if (got != (int)sizeof(check)) {
        kputs("[hbfs] FAIL the appended file would not read back\r\n");
        return -1;
    }
    for (u32 i = 0; i < sizeof(first); i++) {
        if (check[i] != first[i]) {
            kputs("[hbfs] FAIL the move lost the old contents\r\n");
            return -1;
        }
    }
    for (u32 i = 0; i < sizeof(more); i++) {
        if (check[sizeof(first) + i] != 'Z') {
            kputs("[hbfs] FAIL the appended bytes are not there\r\n");
            return -1;
        }
    }

    /* And a second append now has room after it, so it grows in place. */
    if (hbfs_write("/grow2", sizeof(first), more, sizeof(more)) != 0 ||
        hbfs_resolve("/grow2", &entry) != 0 ||
        entry.size != sizeof(first) + sizeof(more)) {
        kputs("[hbfs] FAIL appending to a file that has room\r\n");
        return -1;
    }

    if (hbfs_remove("/grow1") != 0 || hbfs_remove("/grow2") != 0) {
        kputs("[hbfs] FAIL the write test could not clean up\r\n");
        return -1;
    }
    if (free_blocks != before) {
        kputs("[hbfs] FAIL the write test leaked blocks\r\n");
        return -1;
    }

    kputs("[hbfs] append=ok move=ok");
    kputs("\r\n");
    return 0;
}

u32 hbfs_free_blocks(void)
{
    return free_blocks;
}

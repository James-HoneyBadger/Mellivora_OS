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
#define HBFS_NAME_BYTES      252
#define HBFS_SUBDIR_BLOCKS   16
#define HBFS_ROOT_SENTINEL   0xFFFFFFFFU       /* `start` of the root entry */

#define HBFS_ROOT_ENTRIES \
    (HBFS_ROOT_DIR_BLOCKS * HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE)
#define HBFS_SUBDIR_ENTRIES \
    (HBFS_SUBDIR_BLOCKS * HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE)

static u8 root_dir[HBFS_ROOT_DIR_BLOCKS * HBFS_BLOCK_SIZE];
static u8 sub_dir[HBFS_SUBDIR_BLOCKS * HBFS_BLOCK_SIZE];
static u8 sector_buffer[512];
static int ready;
static u32 root_lba;
static u32 data_lba;
static u32 file_count;

/*
 * A working directory per task, indexed by task id (0 is the kernel's own, used
 * before the first task runs). Two tasks listing different directories must not
 * tread on each other, and in the 32-bit tree the same variable was shared
 * because there was only ever one program running.
 */
#define HBFS_MAX_TASKS 8
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

    cwd_ready = 0;      /* every task starts at the root */

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

/*
 * Read one directory into a buffer and say how many entries it holds.
 *
 * The root directory's blocks are found through the superblock and a
 * subdirectory's through its entry's data-relative block number, so both land
 * in a flat buffer that the 288-byte entries can be scanned out of - they are
 * not block-aligned, and an entry can straddle two blocks.
 */
static const u8 *dir_data(const struct hbfs_entry *dir, u32 *entries)
{
    if (dir->start == HBFS_ROOT_SENTINEL) {
        *entries = HBFS_ROOT_ENTRIES;
        return root_dir;
    }
    if (dir->type != HBFS_FTYPE_DIR || dir->blocks == 0 ||
        dir->blocks > HBFS_SUBDIR_BLOCKS) {
        return NULL;
    }

    for (u32 i = 0; i < dir->blocks; i++) {
        u64 lba = (u64)data_lba + (u64)dir->start * HBFS_SECTORS_PER_BLOCK
                + (u64)i * HBFS_SECTORS_PER_BLOCK;
        if (read_block(lba, sub_dir + i * HBFS_BLOCK_SIZE) != 0) {
            return NULL;
        }
    }
    *entries = dir->blocks * HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE;
    return sub_dir;
}

static void entry_from(const u8 *raw, struct hbfs_entry *out)
{
    out->type = raw[253];
    out->size = le32(raw + 256);
    out->start = le32(raw + 260);
    out->blocks = le32(raw + 264);
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
}

int hbfs_lookup_in(const struct hbfs_entry *dir, const char *name,
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

int hbfs_lookup(const char *name, struct hbfs_entry *out)
{
    struct hbfs_entry root;
    hbfs_root(&root);
    return hbfs_lookup_in(&root, name, out);
}

int hbfs_readdir(const struct hbfs_entry *dir, u32 index,
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

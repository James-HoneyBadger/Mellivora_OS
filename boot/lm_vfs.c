/*
 * lm_vfs.c - a read-only VFS front-end over HBFS
 *
 * The 32-bit tree's VFS dispatches to several back-ends (HBFS, procfs, devfs,
 * tmpfs). This is the front-end shape with the one back-end the 64-bit tree has
 * so far: open a name, get a handle, read bytes. Paths are root-level names;
 * directories and the other back-ends arrive with the rest of the syscall port.
 */

#include "lm.h"

#define VFS_MAX_FILES 8
#define SECTOR_SIZE 512

struct vfs_file {
    int used;
    u64 size;
    u64 pos;
    struct hbfs_entry entry;
};

static struct vfs_file files[VFS_MAX_FILES];

int vfs_init(void)
{
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].pos = 0;
    }
    return hbfs_init();
}

int vfs_open(const char *name)
{
    /* Root-level names for now, so a leading '/' is just a prefix. */
    while (*name == '/') {
        name++;
    }

    struct hbfs_entry entry;
    if (hbfs_lookup(name, &entry) != 0) {
        return -1;
    }

    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (!files[i].used) {
            files[i].used = 1;
            files[i].size = entry.size;
            files[i].pos = 0;
            files[i].entry = entry;
            return i;
        }
    }
    return -1;
}

int vfs_close(int handle)
{
    if (handle < 0 || handle >= VFS_MAX_FILES || !files[handle].used) {
        return -1;
    }
    files[handle].used = 0;
    return 0;
}

u64 vfs_size(int handle)
{
    if (handle < 0 || handle >= VFS_MAX_FILES || !files[handle].used) {
        return 0;
    }
    return files[handle].size;
}

int vfs_read(int handle, void *buffer, u64 count)
{
    if (handle < 0 || handle >= VFS_MAX_FILES || !files[handle].used) {
        return -1;
    }

    struct vfs_file *f = &files[handle];
    int got = hbfs_read_at(&f->entry, f->pos, buffer, count);
    if (got > 0) {
        f->pos += (u64)got;
    }
    return got;
}

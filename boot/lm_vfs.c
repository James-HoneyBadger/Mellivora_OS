/*
 * lm_vfs.c - a read-only VFS front-end over HBFS
 *
 * The 32-bit tree's VFS dispatches to several back-ends (HBFS, procfs, devfs,
 * tmpfs). This is the front-end shape with the one back-end the 64-bit tree has
 * so far: resolve a path, open it, then read, seek or stat it, and list the
 * directories on the way. The other back-ends arrive with the rest of the
 * syscall port.
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
    struct hbfs_entry entry;
    if (hbfs_resolve(name, &entry) != 0 || entry.type == HBFS_FTYPE_DIR) {
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

int vfs_seek(int handle, s64 offset, int whence)
{
    if (handle < 0 || handle >= VFS_MAX_FILES || !files[handle].used) {
        return -1;
    }

    s64 base = 0;
    if (whence == 1) {
        base = (s64)files[handle].pos;
    } else if (whence == 2) {
        base = (s64)files[handle].size;
    } else if (whence != 0) {
        return -1;
    }

    s64 target = base + offset;
    if (target < 0) {
        return -1;
    }
    if ((u64)target > files[handle].size) {
        target = (s64)files[handle].size;
    }
    files[handle].pos = (u64)target;
    return (int)target;
}

int vfs_stat(const char *path, struct vfs_stat *out)
{
    struct hbfs_entry entry;
    if (hbfs_resolve(path, &entry) != 0) {
        return -1;
    }

    out->type = entry.type;
    out->size = entry.size;
    out->blocks = entry.blocks;
    out->start = entry.start;
    out->name[0] = '\0';
    for (u32 i = 0; i < sizeof(out->name) - 1; ) {
        char c = path[i];
        if (c == '\0') {
            break;
        }
        if (c != '/') {
            out->name[i++] = c;
        } else {
            i++;
        }
    }
    return 0;
}

int vfs_readdir(const char *path, u32 index, struct vfs_stat *out)
{
    struct hbfs_entry dir;
    if (path == NULL || *path == '\0') {
        if (hbfs_resolve(".", &dir) != 0) {
            return -1;
        }
    } else if (hbfs_resolve(path, &dir) != 0) {
        return -1;
    }
    if (dir.type != HBFS_FTYPE_DIR) {
        return -1;
    }
    struct hbfs_entry entry;
    int got = hbfs_readdir(&dir, index, &entry, out->name, sizeof(out->name));
    if (got < 0) {
        return -1;
    }
    out->type = entry.type;
    out->size = entry.size;
    out->blocks = entry.blocks;
    out->start = entry.start;
    return got;
}

int vfs_chdir(const char *path)
{
    return hbfs_chdir(path);
}

const char *vfs_getcwd(void)
{
    return hbfs_getcwd();
}

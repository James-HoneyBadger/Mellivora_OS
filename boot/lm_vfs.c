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

#define VFS_MAX_FILES 16
#define SECTOR_SIZE 512
#define VFS_PATH_BYTES 128

struct vfs_file {
    int used;
    int writable;
    u64 owner;          /* the task that opened it */
    u64 size;
    u64 pos;
    struct hbfs_entry entry;
    /* The path, because a write has to find the directory entry again to
     * change the file's size - the handle's copy of the entry is a snapshot. */
    char path[VFS_PATH_BYTES];
};

static struct vfs_file files[VFS_MAX_FILES];

int vfs_init(void)
{
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].owner = 0;
        files[i].size = 0;
        files[i].pos = 0;
    }
    return hbfs_init();
}

/*
 * Handles are owned by the task that opened them. The table is shared - there is
 * one file object per open file - so without an owner two tasks opening the same
 * file would be handed the same handle and would share its position, and one of
 * them would read from wherever the other had left it. Finding a slot and
 * claiming it is one critical section for the same reason.
 */
int vfs_open_mode(const char *name, int write)
{
    struct hbfs_entry entry;

    sched_lock();
    if (hbfs_resolve(name, &entry) != 0 || entry.type == HBFS_FTYPE_DIR) {
        sched_unlock();
        return -1;
    }

    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (!files[i].used) {
            files[i].used = 1;
            files[i].writable = write != 0;
            files[i].owner = sched_current_id();
            files[i].size = entry.size;
            files[i].pos = 0;
            files[i].entry = entry;
            for (u32 k = 0; k < VFS_PATH_BYTES; k++) {
                files[i].path[k] = name[k];
                if (name[k] == '\0') {
                    break;
                }
            }
            sched_unlock();
            return i;
        }
    }
    sched_unlock();
    return -1;
}

int vfs_open(const char *name)
{
    return vfs_open_mode(name, 0);
}

int vfs_close(int handle)
{
    sched_lock();
    if (handle < 0 || handle >= VFS_MAX_FILES || !files[handle].used ||
        files[handle].owner != sched_current_id()) {
        sched_unlock();
        return -1;
    }
    files[handle].used = 0;
    sched_unlock();
    return 0;
}

/* A task that exits without closing its files must not leak them: the loader
 * runs the next program, and nothing would ever free those slots. */
void vfs_release_task(u64 owner)
{
    sched_lock();
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (files[i].used && files[i].owner == owner) {
            files[i].used = 0;
        }
    }
    sched_unlock();
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

/* Write at the handle's position and advance it, which is what appending is:
 * seek to the end, then write. The directory entry is reloaded afterwards, so a
 * file that grew - or moved to a new extent - is still readable through the same
 * handle and vfs_size() tells the truth. */
int vfs_write(int handle, const void *data, u64 count)
{
    struct hbfs_entry updated;

    sched_lock();
    if (handle < 0 || handle >= VFS_MAX_FILES || !files[handle].used ||
        files[handle].owner != sched_current_id() || !files[handle].writable ||
        count > 4096) {
        sched_unlock();
        return -1;
    }

    struct vfs_file *f = &files[handle];
    if (hbfs_write(f->path, f->pos, data, (u32)count) != 0 ||
        hbfs_resolve(f->path, &updated) != 0) {
        sched_unlock();
        return -1;
    }

    f->entry = updated;
    f->size = updated.size;
    f->pos += count;
    sched_unlock();
    return (int)count;
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
    /*
     * `index` counts the entries a caller has been given, not the slots in the
     * directory. A slot whose type is FREE is a file that was deleted, and treating
     * one as the end of the listing hides every entry after it - which is exactly
     * what happened the first time this suite deleted a file and then listed the
     * directory again.
     */
    u32 slot = 0;
    for (u32 ordinal = 0;; slot++) {
        struct hbfs_entry entry;
        int got = hbfs_readdir(&dir, slot, &entry, out->name, sizeof(out->name));
        if (got < 0) {
            return -1;              /* the end of the directory */
        }
        if (got == 0) {
            continue;               /* deleted: not one of the entries */
        }
        if (ordinal == index) {
            out->type = entry.type;
            out->size = entry.size;
            out->blocks = entry.blocks;
            out->start = entry.start;
            return 1;
        }
        ordinal++;
    }
}

int vfs_chdir(const char *path)
{
    return hbfs_chdir(path);
}

const char *vfs_getcwd(void)
{
    return hbfs_getcwd();
}

int vfs_create(const char *path, u32 type, const void *data, u32 size)
{
    return hbfs_create(path, type, data, size);
}

int vfs_mkdir(const char *path)
{
    return hbfs_mkdir(path);
}

int vfs_unlink(const char *path)
{
    return hbfs_remove(path);
}

u32 vfs_free_blocks(void)
{
    return hbfs_free_blocks();
}

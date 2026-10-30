/*
 * sys/stat.h - the file information this filesystem actually has
 *
 * The VFS fills twelve bytes: size, type and modification time. It does not
 * report mode, owner, inode number or link count, so this structure does not
 * pretend to: a POSIX program that reads st_mode will not compile, which is
 * the honest failure. The mode is reachable through access() while the kernel
 * grows the stat, and EXEC.md's gap table records it.
 */
#ifndef ESYS_STAT_H
#define ESYS_STAT_H

#include <mv/types.h>

struct stat {
    u32 st_size;        /* bytes */
    u32 st_type;        /* FTYPE_* from the filesystem */
    u32 st_mtime;       /* ticks since boot, as the kernel records it */
};

/* st_type holds the filesystem's own FTYPE_* number, not a POSIX mode. These
 * names make the test read the way the POSIX one does, and stay honest about
 * the fact that a type is all there is: there are no permission bits in it. */
#define MV_FT_TEXT   1
#define MV_FT_DIR    2
#define MV_FT_EXEC   3
#define MV_FT_BATCH  4
#define MV_FT_LINK   5

#define S_ISDIR(m)  ((m) == MV_FT_DIR)
#define S_ISREG(m)  ((m) == MV_FT_TEXT || (m) == MV_FT_EXEC || (m) == MV_FT_BATCH)

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int mkdir(const char *path, unsigned mode);
int chmod(const char *path, unsigned mode);

#endif /* ESYS_STAT_H */

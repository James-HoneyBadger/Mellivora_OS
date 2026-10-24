/*
 * lm_pipe.c - pipes
 *
 * A pipe here is a fixed ring buffer with a reader and a writer: what the tree
 * gives a process pair, minus the processes. The tree's numbers are:
 *
 *   create -> id      write(id, buf, len) -> bytes
 *   read(id, buf, max) -> bytes (0 when empty)     close(id) -> 0
 *
 * Reading never blocks: a caller that wants to wait polls, which is the same
 * contract getchar has. The table is small and the buffers are static, so a pipe
 * costs 4 KB whether or not anyone uses it.
 */

#include "lm.h"

#define PIPE_MAX 4
#define PIPE_BYTES 4096

struct pipe {
    int used;
    u64 owner;                  /* the task that created it */
    u32 read_at;
    u32 count;
    u8 data[PIPE_BYTES];
};

static struct pipe pipes[PIPE_MAX];

void pipe_init(void)
{
    for (int i = 0; i < PIPE_MAX; i++) {
        pipes[i].used = 0;
        pipes[i].owner = 0;
        pipes[i].read_at = 0;
        pipes[i].count = 0;
    }
}

int pipe_create(void)
{
    sched_lock();
    for (int i = 0; i < PIPE_MAX; i++) {
        if (!pipes[i].used) {
            pipes[i].used = 1;
            pipes[i].owner = sched_current_id();
            pipes[i].read_at = 0;
            pipes[i].count = 0;
            sched_unlock();
            return i;
        }
    }
    sched_unlock();
    return -1;
}

static int valid(int id)
{
    return id >= 0 && id < PIPE_MAX && pipes[id].used;
}

int pipe_write(int id, const void *data, u32 len)
{
    if (!valid(id)) {
        return -1;
    }
    sched_lock();
    struct pipe *p = &pipes[id];
    const u8 *in = data;
    u32 written = 0;
    while (written < len && p->count < PIPE_BYTES) {
        u32 at = (p->read_at + p->count) % PIPE_BYTES;
        p->data[at] = in[written++];
        p->count++;
    }
    sched_unlock();
    return (int)written;
}

int pipe_read(int id, void *buffer, u32 max)
{
    if (!valid(id)) {
        return -1;
    }
    sched_lock();
    struct pipe *p = &pipes[id];
    u8 *out = buffer;
    u32 got = 0;
    while (got < max && p->count > 0) {
        out[got++] = p->data[p->read_at];
        p->read_at = (p->read_at + 1) % PIPE_BYTES;
        p->count--;
    }
    sched_unlock();
    return (int)got;
}

int pipe_close(int id)
{
    if (!valid(id)) {
        return -1;
    }
    sched_lock();
    pipes[id].used = 0;
    pipes[id].count = 0;
    sched_unlock();
    return 0;
}

int pipe_selftest(void)
{
    u8 got[8];
    int id = pipe_create();
    if (id < 0) {
        kputs("[pipe] FAIL no free pipe\r\n");
        return -1;
    }
    if (pipe_write(id, "abcdef", 6) != 6 || pipe_read(id, got, 4) != 4) {
        kputs("[pipe] FAIL round trip\r\n");
        pipe_close(id);
        return -1;
    }
    if (got[0] != 'a' || got[3] != 'd' || pipe_read(id, got, 4) != 2 ||
        got[0] != 'e' || got[1] != 'f') {
        kputs("[pipe] FAIL contents\r\n");
        pipe_close(id);
        return -1;
    }
    int again = pipe_create();
    pipe_close(id);
    if (again < 0 || pipe_create() != id) {
        kputs("[pipe] FAIL slot was not reused\r\n");
        return -1;
    }
    pipe_close(again);
    pipe_close(id);
    kputs("[pipe] pipe=ok\r\n");
    return 0;
}

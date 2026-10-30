/*
 * iface.c - the objects behind <mv/iface.h>
 *
 * Two objects, and both are thin because the work is the kernel's: the
 * chronicle writes to the boot ring, the Sett reads and writes the store. What
 * this file adds is the *shape* - a vtable, an identifier, a reference count -
 * so a program asks for an interface rather than calling a number, and so the
 * next object costs a vtable and an entry in the table below.
 */
#include <mv/iface.h>
#include <mv/syscall.h>
#include <mv/string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ */
/* The object descriptor table                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    mv_iface *(*create)(void);
    const char *name;
    u8   in_use;
} iface_factory;

static mv_iface *chronicle_create(void);
static mv_iface *sett_create(void);

static iface_factory factories[] = {
    { chronicle_create, "chronicle", 0 },
    { sett_create,      "sett",      0 },
};

i32 mv_iface_get(const char *name, mv_iface **out)
{
    unsigned i;

    if (out == 0)
        return MV_E_POINTER;
    *out = 0;
    if (name == 0)
        return MV_E_POINTER;

    for (i = 0; i < ARRAY_LEN(factories); i++) {
        if (strcmp(factories[i].name, name) == 0) {
            mv_iface *obj = factories[i].create();

            if (obj == 0)
                return MV_E_FAIL;
            *out = obj;
            return MV_S_OK;
        }
    }
    return MV_E_NOINTERFACE;
}

int mv_guid_equal(const mv_guid *a, const mv_guid *b)
{
    return a->a == b->a && a->b == b->b && a->c == b->c && a->d == b->d;
}

i32 mv_iface_query(mv_iface *self, const mv_guid *iid, void **out)
{
    if (self == 0 || self->vtbl == 0 || iid == 0 || out == 0)
        return MV_E_POINTER;
    return self->vtbl->query(self, iid, (mv_iface **)out);
}

i32 mv_iface_addref(mv_iface *self)
{
    if (self == 0 || self->vtbl == 0)
        return MV_E_POINTER;
    return self->vtbl->addref(self);
}

i32 mv_iface_release(mv_iface *self)
{
    if (self == 0 || self->vtbl == 0)
        return MV_E_POINTER;
    return self->vtbl->release(self);
}

/* ------------------------------------------------------------------ */
/* IChronicle                                                          */
/* ------------------------------------------------------------------ */

static mv_chronicle the_chronicle;
static int chronicle_refs;

static i32 chronicle_query(mv_iface *self, const mv_guid *iid, mv_iface **out)
{
    mv_guid mine = MV_IID_CHRONICLE;

    if (out == 0 || iid == 0)
        return MV_E_POINTER;
    if (mv_guid_equal(iid, &mine)) {
        *out = self;
        chronicle_refs++;
        return MV_S_OK;
    }
    *out = 0;
    return MV_E_NOINTERFACE;
}

static i32 chronicle_addref(mv_iface *self)
{
    (void)self;
    return ++chronicle_refs;
}

static i32 chronicle_release(mv_iface *self)
{
    (void)self;
    if (chronicle_refs > 0)
        chronicle_refs--;
    if (chronicle_refs == 0)
        factories[0].in_use = 0;
    return chronicle_refs;
}

static i32 chronicle_write(mv_chronicle *self, int level, const char *text)
{
    (void)self;
    if (text == 0)
        return MV_E_POINTER;
    return mv_syscall(SYS_LOG_WRITE, (u32)level, (u32)text, 0, 0, 0) == 0
               ? MV_S_OK
               : MV_E_FAIL;
}

static const mv_chronicle_vtbl chronicle_vtbl = {
    chronicle_query,
    chronicle_addref,
    chronicle_release,
    chronicle_write
};

static mv_iface *chronicle_create(void)
{
    chronicle_refs++;
    factories[0].in_use = 1;
    the_chronicle.vtbl = &chronicle_vtbl;
    return (mv_iface *)&the_chronicle;
}

/* ------------------------------------------------------------------ */
/* ISett                                                               */
/* ------------------------------------------------------------------ */

static mv_sett the_sett;
static int sett_refs;

static i32 sett_query(mv_iface *self, const mv_guid *iid, mv_iface **out)
{
    mv_guid mine = MV_IID_SETT;

    if (out == 0 || iid == 0)
        return MV_E_POINTER;
    if (mv_guid_equal(iid, &mine)) {
        *out = self;
        sett_refs++;
        return MV_S_OK;
    }
    *out = 0;
    return MV_E_NOINTERFACE;
}

static i32 sett_addref(mv_iface *self)
{
    (void)self;
    return ++sett_refs;
}

static i32 sett_release(mv_iface *self)
{
    (void)self;
    if (sett_refs > 0)
        sett_refs--;
    if (sett_refs == 0)
        factories[1].in_use = 0;
    return sett_refs;
}

static i32 sett_get(mv_sett *self, const char *path, char *buf, unsigned size)
{
    (void)self;
    if (path == 0 || buf == 0 || size == 0)
        return MV_E_POINTER;
    return mv_syscall(SYS_SETT, 0, (u32)path, (u32)buf, size, 0) == 0
               ? MV_S_OK
               : MV_E_FAIL;
}

static i32 sett_set(mv_sett *self, const char *path, const char *value)
{
    (void)self;
    if (path == 0 || value == 0)
        return MV_E_POINTER;
    return mv_syscall(SYS_SETT, 1, (u32)path, (u32)value, 0, 0) == 0
               ? MV_S_OK
               : MV_E_FAIL;
}

static const mv_sett_vtbl sett_vtbl = {
    sett_query,
    sett_addref,
    sett_release,
    sett_get,
    sett_set
};

static mv_iface *sett_create(void)
{
    sett_refs++;
    factories[1].in_use = 1;
    the_sett.vtbl = &sett_vtbl;
    return (mv_iface *)&the_sett;
}

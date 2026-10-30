/*
 * iface.h - interfaces over the Warren
 *
 * M5 asks for "a COM-flavoured typed dispatch layer on top of the Warren, so
 * applications call interfaces instead of raw syscall numbers". This is that
 * layer, in the shape the idea actually had in 2000: an object is a pointer to
 * a vtable, the first three slots are QueryInterface/AddRef/Release, and a
 * caller asks for the interface it wants by identifier instead of being handed
 * whatever the object happened to be.
 *
 * Three things are deliberately different from COM, and they are the honest
 * ones:
 *
 *   * There is no registry and no class factory. `mv_iface_get` resolves a name
 *     to the object the system already has - the chronicle ring, the Sett store
 *     - because this is an operating system's own surface, not an extensible
 *     plug-in model with a database behind it.
 *   * Identifiers are four dwords, not the 128-bit GUID a distributed COM
 *     needed. Nothing here leaves the machine.
 *   * Reference counts are real and checked: an interface that is released to
 *     zero stops being handed out, and the next mv_iface_get fails rather than
 *     resurrecting a dead object.
 */
#ifndef MV_IFACE_H
#define MV_IFACE_H

#include <mv/types.h>

#define MV_S_OK             0
#define MV_E_NOINTERFACE    (-2)
#define MV_E_POINTER        (-3)
#define MV_E_FAIL           (-1)

typedef struct mv_guid {
    u32 a;
    u32 b;
    u32 c;
    u32 d;
} mv_guid;

typedef struct mv_iface mv_iface;
typedef struct mv_iface_vtbl mv_iface_vtbl;

/* Every interface begins with the same three methods, at the same offsets, so
 * a caller can query and release an interface it does not recognise. */
struct mv_iface {
    const mv_iface_vtbl *vtbl;
};

struct mv_iface_vtbl {
    i32 (*query)(mv_iface *self, const mv_guid *iid, mv_iface **out);
    i32 (*addref)(mv_iface *self);
    i32 (*release)(mv_iface *self);
};

/* --- the identifiers this build knows ------------------------------- */
#define MV_IID_CHRONICLE  { 0x43485230u, 1u, 0u, 0u }   /* 'CHR0' */
#define MV_IID_SETT       { 0x53455430u, 1u, 0u, 0u }   /* 'SET0' */

#define MV_IID_NAME(name) \
    { 0x4D564946u, 1u, 0u, 0u }     /* 'MVIF' - reserved */

/* --- IChronicle ------------------------------------------------------ */
typedef struct mv_chronicle mv_chronicle;

typedef struct mv_chronicle_vtbl {
    i32 (*query)(mv_iface *self, const mv_guid *iid, mv_iface **out);
    i32 (*addref)(mv_iface *self);
    i32 (*release)(mv_iface *self);
    /* level: 0 error, 1 warning, 2 info, 3 debug - the ring's own scale */
    i32 (*write)(mv_chronicle *self, int level, const char *text);
} mv_chronicle_vtbl;

struct mv_chronicle {
    const mv_chronicle_vtbl *vtbl;
};

/* --- ISett ----------------------------------------------------------- */
typedef struct mv_sett mv_sett;

typedef struct mv_sett_vtbl {
    i32 (*query)(mv_iface *self, const mv_guid *iid, mv_iface **out);
    i32 (*addref)(mv_iface *self);
    i32 (*release)(mv_iface *self);
    i32 (*get)(mv_sett *self, const char *path, char *buf, unsigned size);
    i32 (*set)(mv_sett *self, const char *path, const char *value);
} mv_sett_vtbl;

struct mv_sett {
    const mv_sett_vtbl *vtbl;
};

/* --- resolution ------------------------------------------------------ */
i32 mv_iface_get(const char *name, mv_iface **out);
i32 mv_iface_query(mv_iface *self, const mv_guid *iid, void **out);
i32 mv_iface_addref(mv_iface *self);
i32 mv_iface_release(mv_iface *self);
int mv_guid_equal(const mv_guid *a, const mv_guid *b);

#endif /* MV_IFACE_H */

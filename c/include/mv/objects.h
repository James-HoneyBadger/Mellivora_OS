/*
 * objects.h - the object header, as the C side sees it
 *
 * docs/spec/OBJECTS.md §2 fixes this layout and kernel/object.inc reads every
 * field by number. Both sides are written out in full here with the offset of
 * each field asserted, so a field inserted in the middle of the header is a
 * compile error rather than a kernel that reads an object's type where its
 * reference count used to be.
 *
 * The C module includes this header for its assertions only: the object
 * manager itself is assembly, and nothing in this tree constructs one from C
 * yet. That is deliberate - the value is the check, and it costs nothing at
 * run time.
 */
#ifndef MV_OBJECTS_H
#define MV_OBJECTS_H

#include <mv/types.h>

/* The header is 64 bytes and 16-byte aligned; a type body starts after it. */
#define MV_OBJECT_HEADER_SIZE 64u
#define MV_OBJECT_NAME_OFFSET 64u
#define MV_OBJECT_NAME_MAX    255u
#define MV_OBJECT_BODY_OFFSET 320u
#define MV_OBJECT_BODY_SIZE   192u
#define MV_OBJECT_SIZE        512u

struct mv_object_header {
    u32 type;          /* index into the type table; immutable          */
    u32 flags;         /* OF_*                                          */
    u32 refs;          /* pointer references                            */
    u32 handles;       /* open handles, for accounting                  */
    u32 name_offset;   /* offset to the name, 0 = unnamed               */
    u16 name_length;   /* code units, without the terminator            */
    u16 reserved;      /* pads the name to a 4-byte boundary            */
    u32 security;      /* security descriptor                           */
    u32 waiters;       /* head of the waiter list                       */
    u64 created;       /* mv_time                                       */
    u32 owner_pid;     /* creator                                       */
    u32 quota_charge;  /* pool bytes charged to the creator             */
    u32 type_data[4];  /* per-type word: signalled state first          */
};

/* Object flags (kernel/object.inc: OF_*). */
#define MV_OBJ_PERMANENT    0x0001u
#define MV_OBJ_SESSION      0x0002u
#define MV_OBJ_KERNEL_ONLY  0x0004u
#define MV_OBJ_INHERITABLE  0x0008u
#define MV_OBJ_DELETING     0x0010u
#define MV_OBJ_NAMED        0x0020u

/* Object types, in the order the type table lists them. */
#define MV_OBJ_DIRECTORY 0u
#define MV_OBJ_EVENT     1u
#define MV_OBJ_MUTANT    2u
#define MV_OBJ_SEMAPHORE 3u
#define MV_OBJ_TIMER     4u
#define MV_OBJ_TOKEN     5u
#define MV_OBJ_PROCESS   6u
#define MV_OBJ_SYMLINK   7u
#define MV_OBJ_FILE      8u
#define MV_OBJ_TYPE_COUNT 9u

/* Generic rights (ABI.md §6, OBJECTS.md §4). MV_RIGHTS_ALL is a request the
 * type maps, not a mask a handle ever holds. */
#define MV_RIGHT_QUERY_STATE  0x0001u
#define MV_RIGHT_SET_STATE    0x0002u
#define MV_RIGHT_READ_DATA    0x0004u
#define MV_RIGHT_WRITE_DATA   0x0008u
#define MV_RIGHT_EXECUTE      0x0010u
#define MV_RIGHT_DELETE       0x0020u
#define MV_RIGHT_READ_ATTR    0x0040u
#define MV_RIGHT_WRITE_ATTR   0x0080u
#define MV_RIGHT_SYNCHRONIZE  0x0100u
#define MV_RIGHTS_ALL         0x00010000u

/* A handle is (generation << 18) | (index << 2). The low two bits are zero so
 * a handle can never be mistaken for a small integer; the index is 16 bits and
 * the generation 14. */
#define MV_HANDLE_INDEX_SHIFT 2u
#define MV_HANDLE_GEN_SHIFT   18u
#define MV_HANDLE_INDEX_MASK  0x0000FFFFu
#define MV_HANDLE_GEN_MASK    0x00003FFFu

#define MV_HANDLE_MAKE(index, gen) \
    ((((u32)(gen) & MV_HANDLE_GEN_MASK) << MV_HANDLE_GEN_SHIFT) | \
     (((u32)(index) & MV_HANDLE_INDEX_MASK) << MV_HANDLE_INDEX_SHIFT))
#define MV_HANDLE_INDEX(h) (((u32)(h) >> MV_HANDLE_INDEX_SHIFT) & MV_HANDLE_INDEX_MASK)
#define MV_HANDLE_GEN(h)   (((u32)(h) >> MV_HANDLE_GEN_SHIFT) & MV_HANDLE_GEN_MASK)

/* ---------------------------------------------------------------------
 * The assertions. Every offset below is read by number in kernel/object.inc.
 * --------------------------------------------------------------------- */
MV_STATIC_ASSERT(sizeof(struct mv_object_header) == MV_OBJECT_HEADER_SIZE,
                 object_header_is_64_bytes);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, type) == 0,
                 object_type_at_0);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, flags) == 4,
                 object_flags_at_4);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, refs) == 8,
                 object_refs_at_8);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, handles) == 12,
                 object_handles_at_12);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, name_offset) == 16,
                 object_name_offset_at_16);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, name_length) == 20,
                 object_name_length_at_20);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, reserved) == 22,
                 object_reserved_at_22);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, security) == 24,
                 object_security_at_24);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, waiters) == 28,
                 object_waiters_at_28);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, created) == 32,
                 object_created_at_32);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, owner_pid) == 40,
                 object_owner_pid_at_40);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, quota_charge) == 44,
                 object_quota_at_44);
MV_STATIC_ASSERT(__builtin_offsetof(struct mv_object_header, type_data) == 48,
                 object_type_data_at_48);

#endif /* MV_OBJECTS_H */

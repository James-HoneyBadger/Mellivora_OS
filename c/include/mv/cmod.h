/*
 * cmod.h - the contract between the kernel and a C-built kernel module
 *
 * A C subsystem is compiled and linked on its own and then assembled into the
 * kernel image at an address the assembler reports (kernel/cmod.inc and the
 * cmod slot in kernel/data.inc). Because it is linked on its own it has no
 * undefined symbols: everything it may call arrives in the host table passed
 * to init(). That keeps the boundary to one structure that both sides check,
 * rather than a linker symbol list that neither side can verify.
 *
 * Layout rules come from docs/spec/ABI.md section 3. Every offset and size
 * below is asserted, because kernel/cmod.inc reads these fields by number:
 * a field inserted in the middle must be a compile error, not a kernel that
 * calls something that is not there.
 */
#ifndef MV_CMOD_H
#define MV_CMOD_H

#include <mv/types.h>

#define CMOD_MAGIC   0x444F4D43u /* 'CMOD', little-endian */
#define CMOD_VERSION 1u

#define CMOD_HOST_API_VERSION 1u

/* What the kernel offers the module. `size` lets either side refuse a table
 * built against a different header instead of reading past it. */
typedef struct {
    u32 api_version;              /* offset 0  */
    u32 size;                     /* offset 4  sizeof(cmod_host) */
    u32 (*add)(u32, u32);         /* offset 8  NASM, cdecl */
    void (*print)(const char *);  /* offset 12 NASM, cdecl */
} cmod_host;

/* What the module publishes, at the very start of its blob. */
typedef struct {
    u32 magic;                        /* offset 0  */
    u32 version;                      /* offset 4  */
    u32 size;                         /* offset 8  sizeof(cmod_header) */
    i32 (*init)(const cmod_host *);   /* offset 12 */
    i32 (*selftest)(void);            /* offset 16 */
    const char *name;                 /* offset 20 */
} cmod_header;

#define MV_OFFSETOF(type, field) ((u32)__builtin_offsetof(type, field))

/* The assembler sees these as plain numbers; cmod_load refuses the module at
 * boot when they disagree, so a mismatch fails closed and early. */
MV_STATIC_ASSERT(sizeof(u32) == 4, u32_size);
MV_STATIC_ASSERT(sizeof(void *) == 4, pointer_size);
MV_STATIC_ASSERT(sizeof(cmod_host) == 16, cmod_host_size);
MV_STATIC_ASSERT(sizeof(cmod_header) == 24, cmod_header_size);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_host, api_version) == 0, cmod_host_api);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_host, size) == 4, cmod_host_size_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_host, add) == 8, cmod_host_add_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_host, print) == 12, cmod_host_print_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_header, magic) == 0, cmod_magic_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_header, version) == 4, cmod_version_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_header, size) == 8, cmod_size_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_header, init) == 12, cmod_init_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_header, selftest) == 16, cmod_test_off);
MV_STATIC_ASSERT(MV_OFFSETOF(cmod_header, name) == 20, cmod_name_off);

#endif /* MV_CMOD_H */

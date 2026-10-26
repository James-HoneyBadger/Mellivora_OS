# Images, loading and process creation (normative)

**Normative from:** M5. **Governs:** what an executable is, how it is mapped,
how shared libraries are found and bound, what a process starts with, and what
happens when an image is wrong.

Today a program is a flat binary with a fixed load address, no relocations, no
libraries and no metadata; the loader copies it into a 1 MB window and jumps to
the first byte. That is enough for a shell tool and not enough for an
application ([S1](README.md#decisions-this-set-records)).

## 1. Image formats

| Format | Purpose | Support |
|---|---|---|
| **HBX** (native) | The system's own programs; the format this document defines | First-class |
| **PE32** | Programs built for the parity target's ecosystem — the reason M5 exists | The subset in §2; PE32+ is out of scope ([S5](README.md#decisions-this-set-records)) |
| **ELF32** | Development builds from the host toolchain | Development and tests only, not a supported user format |
| **Script** | A text file with a declared interpreter ([§7](#7-scripts-and-associations)) | First-class |

A file's format is decided by its header, never by its extension. An extension
that disagrees with the header is reported and the header wins.

## 2. HBX

```text
mv_image_header
  u32 magic            'MVBX'
  u32 version          1
  u32 header_size
  u32 flags            executable, dll, driver, 32-bit-only
  u64 entry            RVA of the entry point
  u64 image_base       preferred load address
  u64 image_size       bytes, including zero-filled tail for BSS
  u32 section_count
  u32 import_count
  u32 export_count
  u32 tls_index        RVA of the TLS directory, 0 if none
  u32 reloc_rva        RVA of the relocation table
  u32 resource_rva     RVA of the manifest and version resources
  u64 checksum         CRC-32C over the image with this field zeroed
  u32 subsystem        console, windows, driver
  u32 stack_reserve, stack_commit
  u32 heap_reserve
```

Sections declare `{name, rva, size, characteristics}` with the same
characteristics vocabulary the loader enforces: read, write, execute,
discardable, shared. **The loader refuses an image that asks for a section that
is both writable and executable** ([SECURITY.md](SECURITY.md#8-kernel-self-protection)).

## 3. Loading

```text
1. Open the file, verify the header, the checksum and the architecture.
2. Reserve image_size at the preferred base if free, else at a random base
   (ASLR, §4) and record the delta.
3. Create a section over the file (VMM.md §6) and map each section with its
   protection; the zero-filled tail is committed, not mapped.
4. Apply relocations if the delta is not zero; refuse an image with relocations
   stripped that did not load at its base.
5. Resolve imports (§5); a missing import fails the load with the module and
   symbol named.
6. Initialise TLS for the main thread (§6).
7. Create the process object: token, job, session, priority class, environment
   block, current directory, standard handles (§5 below).
8. Set the entry point and start the main thread.
```

A failed load unwinds every step: sections unmapped, imports dereferenced,
handle table discarded. A half-created process is never left for the SCM or the
shell to find.

## 4. Address space randomisation

| Thing | Randomised | Range |
|---|---|---|
| Main image base | Yes, when the image is relocatable | `0x00010000`–`0x40000000` ([VMM.md](VMM.md#21-user-space)) |
| Shared library bases | Yes | `0x80000000`–`0xA0000000` |
| Main thread stack top | Yes | `0x60000000`–`0x7FFF0000`, 64 KiB granularity |
| Heap start | Yes, after the image | Anywhere in the private zone |
| Section views | Yes, unless `MAP_FIXED` | Anywhere in the private zone |
| Kernel image base | Yes, once at boot | Inside the kernel window ([SECURITY.md](SECURITY.md#8-kernel-self-protection)) |

The random seed comes from the platform's entropy at boot and per process, and
is available to a debugger with `SE_DEBUG` so a crash can be reproduced. An
image that is not relocatable keeps its base and says so in its audit record —
"this program cannot be randomised" is information the user is entitled to.

## 5. Process creation

| Input | Default | Notes |
|---|---|---|
| Image | required | §1 |
| Arguments | from the caller | Canonical layout in §5.1 |
| Environment | from the parent, filtered | The Sett synthesises the variables a legacy program expects ([CONFIG.md](CONFIG.md#9-migration-from-what-exists)) |
| Current directory | parent's, unless a path is given | Where a relative argv[0] resolves |
| Token | parent's | A create call may supply an **restricted** token and a list of capabilities ([SECURITY.md](SECURITY.md#6-application-capability-manifests)) |
| Job | parent's, unless an explicit job is given | Quotas arrive with it ([SCHED.md](SCHED.md#7-jobs)) |
| Session | parent's | Cross-session creation needs `SE_SESSION_CROSS` |
| Handles | inheritable only, and only if the caller asks | Never by default |
| Standard handles | inherited, or `NUL` | A service gets `NUL` for all three |
| Priority class | normal | Real-time needs the privilege |
| Mitigations | DEP/ASLR on, SEH-analogue on | A program may opt out only if its manifest was granted the exception, and the opt-out is audited |

### 5.1 Argument and environment block

One contiguous block, UTF-8, `\0` separated, terminated by a double `\0`, with a
header giving counts and offsets:

```text
u32 version, u32 argc, u32 argv_off[], u32 env_count, u32 env_off[], u32 cwd_off
```

- The kernel builds it, so a program cannot be handed a block that disagrees
  with the counts it was given.
- `argv[0]` is the image path as the caller wrote it, which is what
  `programs/argv_test.asm` checks ([as built](#11-as-built-the-gap)).
- `getargs` ([S3](README.md#decisions-this-set-records)) returns a flattened,
  space-separated rendering of the same block, so legacy programs keep working.

### 5.2 exec and exit

`exec` **replaces the image in the existing process**: same handle table (minus
handles marked close-on-exec), same token, same pid, same session. It is a
different operation from create, and a program that used the old in-place flat
reload keeps working ([S3](README.md#decisions-this-set-records)).

Exit ordering is fixed, because teardown order is where lifetime bugs live
([OBJECTS.md](OBJECTS.md#7-lifetime)): threads terminated, address space
released, handle table closed, completion-port messages queued, exit status
recorded, parent signalled.

## 6. Shared libraries

| Property | Contract |
|---|---|
| Finding | The image's own directory, the system directory, then `PATH`-equivalent keys in the Sett; a search order a program can query and an administrator can restrict |
| Loading | Demand-loaded at import resolution; delay-loaded on first call |
| Unloading | `FreeLibrary` unloads when the reference count reaches zero, and only then; a thread inside it keeps it loaded |
| Versioning | An export carries a version; an import names the version it was built against and the loader prefers an exact match, then the highest compatible one |
| Relocations | Base-relative; a library that must load at a fixed base is recorded and reported |
| TLS | Per-thread slots allocated at thread creation; the index is allocated per module and freed at unload |
| Initialisation | A library's entry point runs once per process (`PROCESS_ATTACH`), once per thread (`THREAD_ATTACH`), and may fail the load |
| Single instance | One mapping per image per process, regardless of how many modules import it |

## 7. Scripts and associations

A script is a text file whose first line names its interpreter
(`#! /bin/sh`-style), or whose extension has a handler
([SHELL.md](SHELL.md#3-associations)). The loader opens the interpreter, passes
the script path as the first argument and the script's arguments after it, and
the process's token is the caller's. A script that is not executable and has no
handler is refused with the extension named.

## 8. What a wrong image produces

| Condition | Result |
|---|---|
| Bad magic or checksum | `MV_BAD_IMAGE`, naming the offset |
| 64-bit image | `MV_BAD_IMAGE`, "this system is 32-bit" |
| Writable and executable section | Refused, naming the section |
| Missing import | `MV_ENTRYPOINT_NOT_FOUND`-class, naming module and symbol |
| Cannot load at its base and has no relocations | Refused, naming the base |
| Entry point outside an executable section | Refused |
| Manifest requests an ungranted capability | Starts, then the first use is refused ([SECURITY.md](SECURITY.md#6-application-capability-manifests)) |

Every refusal is a status the caller can act on, not a crash and not a generic
"cannot execute".

## 9. Boot information

The loader hands the kernel one versioned block, at `0x00000500`, containing:
memory map (`E820` entries and their count), boot device and partition, the
video mode requested and the mode granted, the command line, the boot hive's
location ([CONFIG.md](CONFIG.md#6-boot-configuration)), the dump device, and the
debug flags. The kernel copies it into its own memory before it is overwritten,
and the copy's layout is asserted against the loader's with the same
compile-time discipline as every other shared structure.

## 10. Conformance

M5 does not close until:

1. `argv_test` and `argv_test`'s extension prove the argument and environment
   block layout, including a zero-argument and a 4 096-argument case.
2. `loader_test` loads an image at a random base, one at a fixed base, and one
   with a relocated import, and refuses a writable-executable section, a bad
   checksum and a 64-bit image — each with its own status.
3. A program and a library built from the SDK samples load, call across the
   boundary, use TLS in two threads, and unload in the right order.
4. ASLR: 1 000 launches of the same image produce 1 000 distinct main-image
   bases, and a non-relocatable image reports why it cannot be randomised.
5. `fork`/`exec`/`waitpid` and `argv_test` pass unchanged
   ([S3](README.md#decisions-this-set-records)).
6. A missing import names the module and the symbol, and the process is not
   left behind.

## 11. As built (the gap)

| Contract | Tree today |
|---|---|
| HBX and PE32 images | Flat binaries at a fixed address, no headers |
| Shared libraries | None |
| TLS per module | A kernel-managed thread-local area for one module |
| ASLR | Per-launch randomised heap and stack top only |
| Section-based mapping | The loader copies bytes into the program window |
| Imports/exports | None |
| Mitigations | DEP and W^X exist ([VMM.md](VMM.md)); no SEH-analogue |
| env/argv block | A flat command-line buffer and `getargs` |

The loader works and is tested; this document is what it becomes.

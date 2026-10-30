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
| **ELF32** | Development builds from the host toolchain, and the utilities built that way | Placed from its own program headers (§1.1); the ABI underneath is Mellivora's, not Linux's |
| **Script** | A text file with a declared interpreter ([§7](#7-scripts-and-associations)) | First-class |

A file's format is decided by its header, never by its extension. An extension
that disagrees with the header is reported and the header wins.

### 1.1 How an ELF32 image is placed

An ELF32 image is not relocated: every `PT_LOAD` segment is copied to the address
in its own program header, so its link address is its load address and its entry
point is `e_entry` rather than the first byte of the file. The program header
table is read into the kernel before any copy is made, because a normally linked
image puts that table inside the first segment's destination - reading it in
place would read whatever the copy had just written over it. Images with more
than 16 program headers are refused rather than half-loaded. What this does not
buy is Linux compatibility: the container is ELF32, the ABI beneath it is the
system's own ([docs/POSIX.md](../POSIX.md)).

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
  u32 import_rva       RVA of the import table, 0 if none
  u32 export_rva       RVA of the export table, 0 if none
  u32 tls_index        RVA of the TLS directory, 0 if none
  u32 reloc_rva        RVA of the relocation table
  u32 resource_rva     RVA of the manifest and version resources
  u64 checksum         CRC-32C over the file with this field zeroed
  u32 subsystem        console, windows, driver
  u32 stack_reserve, stack_commit
  u32 heap_reserve
```

The header is not part of the image: `header_size` is where the image bytes
begin and every RVA is measured from the first of them. An image therefore
carries its headers in front of itself, and loading at RVA 0 means skipping
`header_size` bytes - which is what lets a flat payload be wrapped without
being reassembled.

Sections declare `{name, rva, size, characteristics}` with the same
characteristics vocabulary the loader enforces: read, write, execute,
discardable, shared. **The loader refuses an image that asks for a section that
is both writable and executable** ([SECURITY.md](SECURITY.md#8-kernel-self-protection)).

**As built** (`kernel/hbx.inc`, `tools/mkhbx.py`). The fixed header is 96 bytes
and the section table follows it at offset 96, so `header_size` is at least
`96 + 20 x section_count`; a section entry is
`{name[8], rva u32, size u32, characteristics u32}`. The `u64` fields are
32-bit values whose high half must be zero - a high half that is not is a 64-bit
image, whatever the flags say. The bits:

| Field | Values |
|---|---|
| `flags` | `01` executable, `02` dll, `04` driver, `08` 32-bit-only, `10` 64-bit |
| section `characteristics` | `01` read, `02` write, `04` execute, `08` discardable, `10` shared |

The relocation table is a `u32` count followed by that many `u32` RVAs, each
naming a dword inside the image to which the load delta is added - a code
displacement is such a dword as much as a data pointer is. The import table is
an array of `{module_rva u32, thunk_rva u32, thunk_count u32}`; each thunk is
`{name_rva u32, iat_rva u32}` and the IAT slot starts at zero. The export table
is a `u32` count followed by that many `{name_rva u32, rva u32}` entries - the
same pair a thunk carries, so a resolver reads one shape in both directions.
The thread-local directory `tls_index` points at is four dwords:
`{raw_size, data_rva, index_rva, base_rva}` - the initialised template, and the
two slots the loader fills. Names are NUL-terminated inside the image.

The checksum covers the whole file - header, section table and image bytes -
with the checksum field read as zero, which is why `header_size` has to be
known before it can be verified.

## 2.1 PE32

PE32 is the reason M5 exists: an image built for the parity target's ecosystem
has to run. The subset accepted here is the one that can be loaded correctly
today, and everything outside it is refused by name rather than half loaded.

| Field | Accepted |
|---|---|
| DOS header | `MZ`, with `e_lfanew` naming a `PE\0\0` signature inside the file |
| Machine | `0x014C` (i386); anything else is refused by number |
| Optional header magic | `0x010B` (PE32). `0x020B` is PE32+ and gets the 32-bit refusal |
| ImageBase | exactly the program window's base ([VMM.md](VMM.md#21-user-space)); nothing relocates a PE32 image yet |
| Data directories | the import directory must be empty; there are no libraries to resolve against (§6) |
| Sections | inside the file and inside `SizeOfImage`, in increasing virtual address order without overlapping, and none both writable and executable |
| Entry point | inside an executable section |
| Size | image and file both inside the program window at once |

The loader copies, it does not map: the file is moved to the top of the window,
the image area is cleared and each section is copied down. Copying in place
would work only while every section's raw data sat above the whole image, which
is true of a wrapped flat payload and false of anything a linker emits.

**As built** (`kernel/pe.inc`, `tools/mkpe.py`, `tools/pe_fixtures.py`). The
branch is one comparison in `image_load_program` - `MZ` at the first two bytes
- which is what the format-agnostic door was for; no caller changed. The
refusals are the sentences the rest of this document already promised, said in
the PE32 loader's own words:

```text
pe: bad image in the header
pe: this system is 32-bit
pe: machine 0x014D is not i386
pe: cannot load at 0x00400000: no relocations
pe: section .text is writable and executable
pe: entry 0x00002010 is not executable
pe: imports cannot be resolved yet
pe: image and file do not both fit the window
```

`make check-loader` proves the image that loads (`pe_ok` prints `PE_OK`) and
each refusal, every refusal followed by a program that still runs, so a
half-built process cannot pass for a refusal. `tools/mkpe.py` carries one
keyword per rule, and `tools/pe_fixtures.py` is the refusal table above written
as code.

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

**As built.** There is no section object yet, so steps 2 and 3 are done with
frames: the image is mapped into the task's own directory one page at a time,
the bytes are copied from the file for each page, every page the file does not
reach is zeroed (the BSS tail), and the mapping is recorded as one vm area so
fork copies it copy-on-write and exit releases its frames. A page covered by a
writable section is mapped writable and every other page read-only; on a 32-bit
non-PAE page table there is no execute bit, so read-only is how "code cannot be
rewritten" is enforced. An image built for `PROGRAM_BASE` still takes the older
road - a copy into the window - because that is where the flat and ELF32
loaders put their programs, and the stack and heap still live there.

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

**As built.** A relocatable image is placed by `hbx_choose_base`: a random
64 KB-aligned address in the first 256 MB of the demand zone
(`[user_demand_base, USER_DEMAND_MAX)`), which is the only range this tree can
map a user page in. Every launch draws again, so the same image does not repeat
its base; the delta between that address and the image's preferred base is
applied to every RVA in its relocation table. An image without relocations is
copied into the program window as before, and the loader says so on the console
- `hbx: not relocatable, loaded at 0x00500000` - rather than pretending it was
randomised. `make check-loader` launches the aslr fixture six times in one boot
and requires six different base addresses; the 1 000-launch form of this check
is not a gate yet.

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

**As built** (`kernel/lib.inc`, `tools/mkhbx.py`, `tools/pe_fixtures.py`). A
library is an HBX image with the `dll` flag and a relocation table; `mvlib.hbl`
in the fixture set is built for `0x00700000` and never loaded there, so every
launch exercises the relocation path. The three steps are separate because
they can be:

1. **Resolve**, during loading and before a process exists. The program's
   import descriptors are read from the file still sitting in the window, each
   module is read into a staging slot, verified by the same verifier a program
   goes through, and each symbol is matched against its export table.
2. **Patch** the program's IAT *in the file copy*, so the copy into the window
   and the mapping at a random base both inherit the answer; no page-table code
   is involved. A module the program names twice is loaded once.
3. **Map** each library into the task's own address space, at the base step 1
   chose, through the same `hbx_map_task_image` the main image uses - so a
   library gets the same page protections, relocation fixups and vm-area
   lifetime as a program. A call therefore crosses from the window into the
   demand zone, which `make check-loader`'s `hbx_dll` case proves.

Finding is `<name>.hbl` in `/lib`, then in the root; a program may import at
most four modules, each at most 32 KB. The staging slots live at the top of the
program window, and the window is cleared between loading and launching, so a
module is read *again* at map time rather than trusting bytes that were true
before the clear.

**Thread-local storage** (`kernel/tls.inc`). A module that declares a TLS
directory gets a block of its own in every task that loads it, and the two
halves of the contract are filled where each can be known. The *index* is
assigned while the image is still in the window - the program is module 0 and
each library takes the next - so it travels with the file copy into both roads.
The *block* needs the task, so it is made at launch: one page-rounded
allocation per module, filled from the template and zeroed past it, mapped into
the task's own address space from the top of the demand zone downwards (images
are drawn in its first 256 MB, so the two cannot meet), recorded as a vm area
so fork copies it copy-on-write, and its address written into `base_rva` -
directly for a window image, through the page tables for a mapped one. A module
reads `[base_rva]` once and keeps it; there is no per-access kernel call.

What section 6 still asks for and this does not do: unloading (`FreeLibrary`
and its reference count), delay loading, versioned exports, a library that
imports another library, and the `PROCESS_ATTACH`/`THREAD_ATTACH` notification. A module that cannot be mapped at launch (out of frames) is
reported and skipped: its imports stay unresolved and the first call through
one faults inside that one task, which the fault path contains.

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
"cannot execute". The table is about behaviour, not about a header: the PE32
loader answers it in its own words ([§2.1](#21-pe32)) - `pe: this system is
32-bit`, `pe: cannot load at 0x00400000: no relocations`, `pe: section .text is
writable and executable` - so a caller that reads the status rather than the
sentence sees one loader, and a user reading the console sees which format
disagreed with them.

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
   checksum and a 64-bit image — each with its own status. (**As built:**
   `make check-loader` proves the fixed base, a relocated image, the randomised
   base, a PE32 image, a program calling into a shared library (§6), and every
   refusal in section 8 in both formats.)
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
| HBX images | Read and enforced: `MVBX` header, CRC-32C, sections, relocations and entry point. A relocatable image is mapped at a random base; a fixed-base one is copied into the window. |
| PE32 images | Read and enforced: the §2.1 subset, with one refusal per rule. A PE32 image is copied into the window like a fixed-base HBX image, so it is not randomised and cannot import; the unwrapping and the refusal set are what `make check-loader` proves. |
| Shared libraries | Loaded, bound and mapped: a `dll` image is found in `/lib`, its exports are matched against a program's imports, and it is mapped into the task's own address space. No unloading, no versioning, no delay loading; a library may not import another |
| TLS per module | A block per module per task, from the module's own template; a library's block is separate from its program's. One block per thread is all this tree has, because a process is one thread |
| Threading | No `clone`/`CreateThread`: a task is a process and a thread at once, so a second thread's TLS block has nothing to attach to yet |
| POSIX calls | A shim per call in `c/rt/posix.c` over syscalls this specification does not introduce, with ten headers in `c/include/`. What conforms is listed in [POSIX.md](../POSIX.md), along with the gaps that remain - `struct stat` without a mode, `dup` without a shared offset, no `pipe`/`socket` wrappers, no `pthread` - and the fact that no conformance suite has been run |
| ASLR | The main image base for a relocatable HBX image, and per-launch randomised heap and stack top. Shared libraries, thread stacks and section views have nothing to randomise yet |
| Section-based mapping | Frames are mapped into the task's directory page by page and a page in a writable section is mapped writable; a writable-and-executable section is refused. There is no section object and no execute bit on this page table |
| Imports/exports | Both directions work: an import is resolved against a real export table, a missing module or symbol fails the load naming `module!symbol` before any process exists, and a module is loaded once however many descriptors name it |
| Mitigations | DEP and W^X exist ([VMM.md](VMM.md)); no SEH-analogue |
| env/argv block | A flat command-line buffer and `getargs` |

The loader works and is tested; this document is what it becomes.

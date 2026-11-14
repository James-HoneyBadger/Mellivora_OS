# Storage: HBFS v5 (normative)

**Normative from:** M3. **Governs:** the on-disk format, the volume model, the
cache, transactions and recovery, and the file attributes a 2000-era user is
entitled to expect — permissions, quotas, streams, compression, encryption and
snapshots.

HBFS v3 is what the tree runs on, and v4 is the bridge: a verified, journalled,
writable format with CRC-32C on the superblock and every metadata run, extent
files, and a volume that is refused rather than reformatted when it fails a
check. v5 is the format this document specifies. It keeps v4's guarantees and
removes its ceilings ([S1](README.md#decisions-this-set-records)).

## 1. Ceilings removed

| Quantity | v3/v4 today | v5 |
|---|---|---|
| Volume size | 2 GB (32-bit block numbers, whole-volume bitmap) | 2^62 bytes (64-bit block numbers) |
| File size | 2 GB | 2^63 bytes |
| Block size | 4 KiB | 4 KiB, 16 KiB or 64 KiB per volume |
| Entries per directory | 224 subdirectory / 2 048 root | 65 535, in a B+tree |
| Name | 63 bytes, 8-bit | 255 UTF-16 code units |
| Path | 1 024 bytes | 32 767 code units |
| Attributes | mode bits, one owner | ACL, owner SID, attributes, timestamps, streams |
| Streams | none | Named streams per file, 255-code-unit names |
| Quotas | none | Per SID, hard and soft, tracked in the tree |
| Compression | none | Per file or per stream, 16 KiB chunks |
| Encryption | none | Per file or per volume, AES-256-XTS |
| Snapshots | taken, held, read and dropped; one per volume, plain files only | Copy-on-write, per volume, retained |
| Links | none | Hard links with a link count; symlinks as reparse points |
| Sparse | no | Yes, with a hole map in the run list |

A volume larger than the tree can describe is not a volume the system supports
([S6](README.md#decisions-this-set-records)); 2^62 bytes is chosen because a
`u64` byte offset stays valid for any file inside it.

## 2. Volume model

| Concept | Rule |
|---|---|
| Partitioning | GPT preferred, MBR supported; a partition is a volume, never a filesystem-less blob |
| Volume identity | 128-bit UUID, stable across a re-plug, in the superblock |
| Mount | By **mount point** (a directory) or a drive letter under `\Sessions\<n>\DosDevices`, never by device number |
| Multiple volumes | Any number, each with its own block size; mount points nest |
| Label | 32 UTF-16 code units, changeable without unmounting |
| Removable media | Ejected only when no handle is open and the cache is flushed; a refusal names the holder |
| Health | Bad blocks remapped on write, counted, and reported by `chkdsk`; a volume that needs repair mounts read-only and says so |

A volume that fails validation is **refused**, never reformatted: the only way
to create a filesystem is `format`, which is explicit and confirmed.

## 3. On-disk layout

```text
LBA 0        protective MBR (GPT) or the MBR partition table
LBA 1..      GPT header + entries, or a reserved area
+ 0          superblock           (one 4 KiB block, primary)
+ 4 KiB      superblock copy      (secondary, must verify identically)
+ 8 KiB      journal              (N blocks, size in the superblock)
+            block groups:
              group descriptor      (one block per group)
              allocation bitmap     (one bit per block, checksummed)
              inode / file records  (B+tree leaves, checksummed)
              directory tree        (B+tree, checksummed)
              quota tree            (B+tree, checksummed)
              snapshot table        (B+tree, checksummed)
              extent tree           (B+tree, checksummed)
              data blocks
```

A block group is **32 768 blocks** (128 MiB at 4 KiB), and every group carries
its own bitmap and its own copy of the group descriptor's checksum, so a damaged
group does not take the volume with it.

### 3.1 Superblock

| Field | Size | Notes |
|---|---|---|
| `magic` | 4 | `'HBF5'` |
| `version` | 4 | 5 |
| `feature_word` | 8 | Which of §7–§11 are in use |
| `compat_word` | 8 | Features a reader may ignore |
| `block_size` | 4 | 4 096, 16 384 or 65 536 |
| `volume_blocks` | 8 | 64-bit |
| `free_blocks` | 8 | 64-bit |
| `volume_uuid` | 16 | |
| `root_tree` | 8 | Block of the directory B+tree's root |
| `extent_tree` | 8 | |
| `quota_tree` | 8 | |
| `snapshot_table` | 8 | |
| `journal_start` | 8 | |
| `journal_blocks` | 4 | |
| `generation` | 8 | Monotonic, advanced by every committed transaction |
| `label` | 64 | 32 UTF-16 code units |
| `clean` | 4 | 0 = mounted, 1 = cleanly unmounted |
| `checksum` | 4 | CRC-32C over the block with this field zeroed |

Both copies are verified at mount. If they disagree, the one whose generation is
higher **and** whose checksum verifies wins, and the mount says so; if neither
verifies, the volume is refused.

## 4. Metadata

### 4.1 File record

| Field | Size | Notes |
|---|---|---|
| `file_id` | 16 | Stable for the life of the file; what a handle and a share refer to |
| `parent_id` | 16 | For hard links, the link's own parent |
| `links` | 4 | Hard link count; 0 means deleted, blocks freed at transaction commit |
| `size` | 8 | Bytes, sparse-aware |
| `allocated` | 8 | Blocks actually allocated, for quota and reporting |
| `timestamps` | 32 | Created, modified, accessed, changed — `mv_time`, 100 ns since 1601 |
| `attributes` | 4 | Read-only, hidden, system, archive, compressed, encrypted, sparse, reparse |
| `security` | 4 | Offset into the volume's security table (deduplicated descriptors) |
| `acl_hash` | 4 | So a descriptor change is one comparison |
| `stream_count` | 2 | |
| `streams` | var | `{name, size, flags, run_list}` — the unnamed stream is the file itself |
| `run_list` | var | Extents: `{offset: u64, length: u64, flags}`, inline when it fits in the record |
| `checksum` | 4 | CRC-32C over the record |

A record up to 4 KiB lives inline in its B+tree leaf; a larger record (many
streams, a long run list) is a tree of its own pointed to by the leaf. Small
files (≤ 700 bytes) are stored **inline in the record**, so a directory of small
files costs one block each rather than two.

### 4.2 Directories

A B+tree keyed by the case-insensitive, case-preserving name
([ABI.md](ABI.md#8-text-and-names)) whose leaf entries are
`{name, file_id, attributes, size, timestamps}`. A directory listing does not
open a file record, so `dir` on a large directory is one tree walk.

**Hard links** are directory entries that name an existing `file_id`; the record's
`links` count is the truth. **Reparse points** (symlinks, junctions, mount
points) are stored as a stream whose content is the target, with the
`reparse` attribute; resolution lives in the namespace
([OBJECTS.md](OBJECTS.md#5-the-namespace)).

## 5. Transactions and recovery

Every metadata change is a **transaction**, and a transaction is either entirely
on the volume or not there at all:

1. The journal records the *after* image of each block the transaction will
   change, with a CRC-32C per record and a transaction id.
2. A **commit record** with the id and a CRC is written and flushed.
3. The blocks are written to their home locations, each with its own checksum.
4. The commit record is retired once every home block verifies.

On mount, a journal with an unretired commit is replayed **idempotently** (after
images are safe to apply twice); a journal whose commit record does not verify is
discarded, and the volume's `clean` flag decides whether that is safe. A
transaction that never committed leaves no trace, because nothing was written to
a home location before the commit.

| Rule | Contract |
|---|---|
| Ordering | Data blocks are written and flushed before the metadata that references them, so a crash never exposes uninitialised data through a valid length |
| Checksums | Every metadata block and every run carries one; a mismatch refuses the read and marks the volume for repair |
| No reformat | A failure anywhere in mount is a refusal with a reason, never an implicit format ([S6](README.md#decisions-this-set-records)) |
| Recovery console | A volume the journal cannot replay mounts read-only and can be repaired from the recovery console ([RELIABILITY.md](RELIABILITY.md#5-boot-modes-and-the-recovery-console)) |
| Power-cut gate | 10 000 randomised power cuts during metadata work, every one recoverable, is the exit test |

## 6. The cache

One cache, shared by the file layer, the section layer and the loader
([VMM.md](VMM.md#6-sections)):

| Property | Contract |
|---|---|
| Size | One eighth of physical RAM, clamped to [8 MiB, 2 GiB], resizable at run time |
| Unit | A 4 KiB page, plus a block-unit view for metadata |
| Write policy | Metadata and journal: write-through with flush. Data: write-back, flushed every 5 s and at every transaction boundary that depends on it |
| Coherency | One page object per `(volume, block)`, so `read`, `mmap` and the loader see the same bytes |
| Eviction | Clock with a dirty list; a dirty page is written before it is evicted; eviction never blocks a `DISPATCH`-level caller |
| Failures | A write error marks the volume read-only and reports the block, rather than retrying forever |
| Removable | A volume cannot be ejected with dirty pages; the refusal names the files holding them |

## 7. Compression

Per file or per stream, **16 KiB chunks**, each independently decodable, with a
per-chunk flag byte and a per-chunk CRC. A chunk that does not compress is stored
raw; the file's `allocated` count reflects what is really on disk, and the
`compressed` attribute is a hint rather than a promise. Decompression happens in
the cache, so a section maps decompressed pages and a read returns decompressed
bytes with no separate path.

The algorithm is a fixed, self-contained LZ-family codec implemented in
`kernel/` — no dependency, no version negotiation, and a format that can be
frozen once it is tested against a corpus.

**As built.** The container is `kernel/comp.inc`: a compressed entry sets flag
bit 1 and its run is `[chunk table][payload]`, back to back. Each 16-byte table
entry is `{u32 block, u32 stored bytes, u32 CRC-32C of the plaintext, u32 flags}`
with bit 0 of `flags` selecting LZ-encoded or stored-whole, so a chunk that does
not shrink is kept as itself and still carries its checksum. The table is
addressed arithmetically - chunk *n* is entry *n* at byte `n*16`, so table block
`n/256` - which is why reading one section costs one table block and one chunk
and not the file. `compact` and `expand` move a file between this layout and one
plain run inside a journal transaction, charging or releasing the difference to
the file's owner; the new run is written and the entry committed before the old
run is freed, so a crash leaves a leak `repair` reports and never a file that is
half of each. A chunked file refuses in-place writes (a descriptor read that
writes would scribble into a payload), `truncate` (there is no plaintext tail to
drop) and `defrag` (its table names absolute blocks, so moving it would leave
every entry pointing where the payload used to be); `write`, `copy` and
`SYS_FWRITE` replace the entry whole and are unaffected. A mapping is decoded a
chunk at a time into the faulting frame - the area carries the plaintext size,
the chunk table's block and the first file page it covers, so a mapping that
starts mid-file decodes the chunk that page belongs to - and the bytes between
the end of the file and the end of its last page are zeroed, because the decode
leaves the rest of that page as it found it. Such a mapping is private by
construction: there is no plaintext block to share, so `MAP_SHARED` of a
chunked file is refused while `MAP_PRIVATE` works. The loader decodes
through the same seam, so a compressed executable runs. The offline checker
decodes every chunk with its own decoder, written from this section rather than
from the kernel, and requires each plaintext checksum to match - the kernel's
encoder and decoder agree with each other even when both are wrong, and that is
the check which does not.

`kernel/lz.inc` is the codec, and its bitstream is the frozen part: tokens are byte-aligned, eight to a group; the group opens with a
flag byte whose bit *n* says whether token *n* is a literal (0, one byte) or a
match (1, a little-endian word whose low 12 bits are `distance - 1` and high 4
bits are `length - 3`). A match may not reach outside its chunk and may not read
bytes it has not produced, so a chunk is decodable alone. The encoder never
returns an encoding that is not smaller than its input — it returns 0 and the
caller stores the chunk raw — which is why incompressible data cannot grow a
file. `lz_selftest` runs on every boot and prints `LZ_SELFTEST_OK`: nine cases
that round-trip prose, a single repeated byte, an eight-byte period, a quarter
repeated at exactly the maximum distance and noise that must be refused, plus a
stream that refers before its own start and must be rejected. Each case that
must shrink has a size it must come in under, so a compressor that emits one
literal per byte cannot pass by round-tripping with itself.

Not built: a shared mapping of a chunked file (refused, not served: there is no
plaintext page to share), a `compressed` attribute in `dir`'s attribute word -
the entry flag is the truth and nothing derives a hint from it yet - and a lock
around the codec's scratch: it is one set of buffers, serialised today by the
block cache lock around the reads it does and by nothing else, so two CPUs
faulting on chunked mappings at once is a race this build does not exclude. A
stream takes the container like any other content, because a stream is an
ordinary entry with its own size and blocks.

## 8. Encryption

| Scope | Key | Notes |
|---|---|---|
| Volume | A volume key, wrapped by the machine key in the store | Mount requires the key |
| File or stream | A per-file key, wrapped by the volume key | Randomly generated at creation |

AES-256 in XTS mode over 4 KiB data units, with a per-file tweak derived from the
`file_id`. A volume whose key is unavailable is **refused**, not mounted empty
and not formatted; the prompt for a passphrase is a property of the mount, not of
the format. Keys live in the Sett ([CONFIG.md](CONFIG.md)) wrapped by the
machine key ([SECURITY.md](SECURITY.md#9-secrets)).

Encryption is a feature bit: a volume without it can be mounted by a reader that
does not implement it, and a file marked encrypted inside an unencrypted volume
is refused rather than returned as ciphertext.

## 9. Quotas

| Quantity | Accounting | Enforcement |
|---|---|---|
| Bytes allocated | Per SID, in the quota tree, updated in the same transaction as the allocation | `MV_QUOTA_EXCEEDED` on a soft limit (warning + audit) or a hard limit (refusal) |
| File count | Per SID | Same |
| Directory count | Per SID | Same |

Usage is recomputed by a scrub pass (`chkdsk --quotas`) and the recomputed value
is what the tree is corrected to, so a crash between an allocation and its
accounting cannot leave a permanent discrepancy.

## 9a. Access control lists

A file may carry an ACL. Where one is present it **decides**, and the mode word
is no longer consulted; where none is present the mode word answers exactly as
it always has.

| ACE | Key | Value | Meaning |
|---|---|---|---|
| Named user | `acl:u<uid>` | `0`-`7` | one user's permissions |
| Named group | `acl:g<gid>` | `0`-`7` | one group's permissions |
| Mask | `acl:m` | `0`-`7` | narrows the named-user and group ACEs |
| Other | `acl:o` | `0`-`7` | everyone else |

`acl:o` is what makes an ACL present: writing it is what turns a file from
mode-word access into ACL access. The ACEs live in the file's existing xattr
block, one entry each, so no on-disk structure changed to add them and a file
carrying an ACL is still a valid v3 file to an older reader.

Evaluation is POSIX's, without inheritance:

1. root is granted everything before any ACL is read;
2. the owner, through `acl:u<owner>` when present, else the owner triplet;
3. a named user ACE for the caller, narrowed by `acl:m`;
4. the caller's group ACE, narrowed by `acl:m`;
5. `acl:o`.

The mode word is left alone: `chmod` on an ACL'd file changes what a tool that
only reads the mode word sees, and does not rewrite the ACEs. Replacing a file's
contents through the create path - `write`, `save` - writes a fresh entry, so the
old ACEs go with the old entry; setting an ACL again is the caller's job.

## 10. Snapshots

A snapshot is a **copy-on-write block tree**: a snapshot holds a read-only root
record, and the first write to a block after the snapshot allocates a new block
and leaves the old one to the snapshot. This is what makes restore points cheap
([RELIABILITY.md](RELIABILITY.md#6-restore-points)) and a differencing snapshot a
natural backup primitive.

| Rule | Contract |
|---|---|
| Creation | O(1): record the current root and generation |
| Retention | Per volume, by count and age; the oldest is deleted when the cap is reached, and the deletion frees blocks no other snapshot references |
| Mount | A snapshot is exposed read-only as a directory of the volume, so a user can recover a file without a tool |
| Deletion | Refcounted blocks; the free count is exact after deletion |
| Space | A snapshot consumes only the blocks that changed, and the space is charged to the volume, not to a user |

## 11. Online maintenance

| Operation | Runs | Guarantee |
|---|---|---|
| `chkdsk` (online) | `PASSIVE`, throttled | Reads every metadata block, verifies its checksum, corrects the free counts from the bitmaps |
| `chkdsk --repair` | Recovery console or online with the volume quiesced | Repairs a damaged tree from the journal and the secondary structures |
| `chkdsk --quotas` | Online | Recomputes usage per SID |

As built: the recovery console's command is `repair`. It walks every directory
and compares what the tree claims with the allocation bitmap.

| Command | What it does |
|---|---|
| `repair` | Walks the tree twice against the resident bitmap and reports: blocks claimed but marked free, blocks marked used that nothing claims, blocks two entries claim, and entries that cannot be trusted (empty name, `/` in the name, unknown type, a block or xattr pointer outside the data area). Writes nothing |
| `repair fix` | The same walk, then rewrites the bitmap from what the tree claims and the free count from the bitmap. Refused when an entry is damaged or a block is shared: rewriting the bitmap to agree with damage only makes the next check quieter |
| `repair journal` | Discards a log that cannot be replayed. A pre-image torn by the power cut can never be applied, so the transaction it belongs to can never be undone; zeroing the log says so out loud instead of replaying a guess |

`make check-repair` corrupts a copy of the image in the three ways a volume
really goes wrong - a needed block marked free, an unowned block marked used,
and a free count that no longer describes the bitmap - and asserts that
`repair` finds all three, `repair fix` repairs them, a second boot still calls
the volume clean, and `tools/fsck_hbfs.py` agrees afterwards. It also runs
`repair` twice in one boot and requires the two reports to count the same
volume: the counters belong to a walk, not to the boot.

The walk borrows the bitmap as its own scratch (referenced bits are cleared as
they are met, so what remains set is exactly the leaks), and nothing is written
until the end: a machine that dies mid-check leaves the volume untouched.
| Scrub | Background, lowest priority | Reads every allocated block, verifies the run CRCs, remaps a bad block from spare space |
| Defragment | Background | Moves extents to reduce fragmentation; never runs on a volume with a snapshot that has not been retired |

An offline checker (`tools/fsck_hbfs.py`) remains the reference implementation of
the format, and the online checker is tested against it on every corrupt image
the fuzzer produces.

## 12. Concurrency and sharing

| Scope | Mechanism |
|---|---|
| Within the machine | Per-file read/write locks that respect the region (`byte-range` locks) for compatibility, plus a share mode from `open` (`FILE_SHARE_READ/WRITE/DELETE`) |
| Across machines | The redirector seam ([NETWORK.md](NETWORK.md#4-shares)) uses the same share modes and leases; the local layer does not know the difference |
| Against a snapshot | Writes never block a snapshot read |

## 13. Performance targets

Reported by the benchmark suite, measured under QEMU on the standard image:

| Operation | Target |
|---|---|
| Sequential read | > 100 MB/s |
| Sequential write | > 80 MB/s |
| Metadata create/delete | > 5 000 per second |
| Directory scan of 10 000 entries | < 20 ms |
| Mount a clean 2 GB volume | < 100 ms |
| Snapshot create | < 1 ms |
| Cache hit rate on a build workload | > 90 % |

## 14. Conformance

M3 does not close until:

1. **10 000 power-cut iterations** during metadata work, every one recovered by
   replay, with `fsck` clean afterwards.
2. A quota and an ACL conformance suite: a write at the hard limit is refused,
   an access the ACL denies is refused, and both are audited.
3. A corrupted volume is repaired from the recovery console and the repaired
   volume passes `fsck`.
4. An encrypted volume round-trips with its key in the store; refusing the key
   refuses the mount and leaves the volume byte-identical.
5. Compression round-trips a corpus including incompressible data, and a section
   over a compressed file reads the decompressed bytes.
6. A snapshot taken mid-write is consistent, the files it holds read back, and
   its deletion returns the free count to the pre-snapshot value. All three are
   `make check-snapshot`; section 16 says what the read path covers.
7. The online checker agrees with `tools/fsck_hbfs.py` on every image the fuzzer
   produces.

## 15. As built (the gap)

| Contract | Tree today |
|---|---|
| 64-bit block numbers, 2^62 volume | 32-bit, 2 GB |
| B+tree directories, 65 535 entries | Linear directory blocks, 455 entries per directory |
| Alternate data streams | **Yes**, as directory entries of type 6 named `FILE:STREAM` beside the file: the path resolver finds them, quotas, ACLs, xattrs and repair treat them as entries, and `stream list/read/write/delete` is the shell surface. Listings hide them; delete and rename move them with the file. The target's per-stream run lists inside one record are not implemented |
| ACLs, streams, compression, encryption, snapshots | **ACLs: yes**, as ACEs in the xattr block (`acl:u<id>`, `acl:g<id>`, `acl:m`, `acl:o`), evaluated by `acl_perm` before the mode word and settable from the shell with `acl set`; conformance in `acl_test`. Inheritance is a trailing `i` on an ACE's value: a directory written that way passes the ACE to everything created in it, clearing the flag for files and keeping it for subdirectories so the grant walks a tree; evaluation reads only the permission digit, so an inheritable ACE is effective where it is written too. The group class is a union: the mode word's group triplet and any group ACE naming the caller's group are added, and the mask narrows the total, so a 0640 file with `acl:g<gid>` granting write gives that group write and keeps the read the triplet gave them. **Volume encryption: yes**, as AES-256-XTS over the 4 KiB units above the journal, with the key checked against the superblock before the mount and `crypt format`/`crypt unlock` as the only writers of the stamp; the store is kernel memory (`key add`), so the volume is refused after a reboot until the secret is put back, and there is no per-file layer yet. **Compression: yes**, as 16 KiB chunks named by a 16-byte table entry each (block, stored length, plaintext CRC-32C, LZ/whole flag) in the file's own run, written and removed by `compact`/`expand` in a journal transaction; reads decode through one seam, so `cat`, `diff`, a descriptor read and the program loader all see plaintext, and `make check-compress` runs a 37-chunk program while it is compressed. Gaps: in-place writes, truncate and defrag refuse a chunked file rather than teach each of them about chunks; a mapping is decoded on fault but is private, so `MAP_SHARED` of one is refused; and the codec's scratch is not locked. **Snapshots: taken, held, read and dropped.** The volume's blocks are copied before anything can destroy them - a write, and a free, because a freed block is the next allocation's content - into a {source LBA, saved LBA} map held in a contiguous run the superblock names. `snapshot create|info|drop` takes, reports and releases it, and `snapshot cat FILE` prints a file as it was, walking the snapshot's own copy of each directory; `make check-snapshot` covers a file deleted after the snapshot, one under a subdirectory, and a name it never held. A drop gives every block back, which the gate asserts by comparing the referenced-block count before and after, and `repair`, `tools/fsck_hbfs.py` and `populate.py` all count the record and its copies as referenced rather than as leaks. The gate also runs `rewrite_test` -
twelve writes to one name, each read back byte for byte - with and without a
snapshot, which is what catches a write lost between two of them. Gaps: one
snapshot per volume and no retention policy; the read path handles plain files only, and says so for a compressed or extent-mapped one; and the map refuses the write when its 4096 entries are used |
| Per-user quotas | Yes, and derived rather than stored: the ledger is recomputed from the tree at every mount (`quota_build`), so it cannot drift, and `quota rebuild` does it on demand. Block accounting per uid with a ceiling where 0 is unlimited; a write past it is refused before anything is allocated, and deleting a file returns the blocks to the file's owner rather than to whoever deleted it. `quota set` takes a uid or an account name. Root is deliberately exempt: every shipped file is uid 0, so without the exemption the first write by the shell would cross the default ceiling and refuse, and no system file could grow. The ceiling therefore has to be proved where it applies - from a task that has dropped to an ordinary uid, which is what `quota_limit_test` does. That test uses a uid no other case writes as (2200): the ledger charges a file to its owner and a directory to whoever owns it, so a uid the rest of the regression matrix already owns blocks under does not start at zero, and a one-block ceiling would refuse its control write as well as the one it means to refuse |
| Journal with redo records | Undo journal: before-images replayed backwards |
| One unified cache | A block cache and a page cache that agree by construction |
| Partition table | Read: MBR's four primary entries, or GPT behind a protective MBR, with each volume's start, length, type, GUID and UTF-16 name in `vol_table`. The filesystem is *probed* (the superblock LBA must fall inside a volume) rather than assumed |
| Mount points | One volume, mounted from `vol_base_lba`, and it has a **label**: 32 NUL-padded bytes at offset 80 of the superblock, written by `format` and populate.py, read and set by `label` (root only, journalled through the superblock), shown by `volumes` as `fs label` beside the partition table's own name for the volume. Multiple mounts are the remaining volume-model work |
| Online repair | `repair` (report) and `repair fix` (bitmap + free count) from the shell, at parity with the offline checker's classes: leaks, freed-but-claimed blocks, shared blocks, untrusted entries. `make check-repair` proves it on a corrupted copy. `repair journal` discards a log that cannot be replayed |
| Removable safe ejection | **Partly.** `eject` refuses while any page of the volume is still mapped - naming how many and the block the first holds - because the page cache keeps no dirty list and a mapped page is a write that has not happened yet; with nothing mapped it flushes the journal and the block cache, then releases the storage device without touching the keyboard on the same controller. It does not name the *files* holding the pages: the cache does not record which file a page belongs to, and the block is what it can prove |
| Durability gate | `make check-durability`: the crash harness over rotating workloads kills QEMU mid-write. A failed fsck *before* replay is accepted only when the volume is marked for replay - damage with a clean journal is the failure that no replay can repair - and fsck must pass after the replay, which is the assertion. A harness boot that never reaches the shell is retried once and reported as a boot failure, not as a durability failure: QEMU dies at start-up often enough that conflating the two once made a clean 200-iteration run look like damage. `CRASH_ITERS=10000 make crash-soak` is the exit test; 200 iterations of it have run clean, and the other 9 800 have not. The workloads rotate over directory creation, create/delete/chmod, the extent writer and an extended-attribute churner that allocates, grows and frees the block an ACL lives in - the one structure named by a field of an entry rather than by a run, and the one a checker is therefore most likely to enumerate wrongly |

## 16. Snapshots (as built: taken, held, dropped)

Conformance item 6 asks for a snapshot taken mid-write to be consistent, for its
files to read correctly, and for its deletion to return the free count to the
pre-snapshot value. The first and third are built and gated; the second is not,
and this section says so rather than leaving it implied.

**What a snapshot is.** A set of blocks whose content must stay as of the moment
it was taken. `snapshot create` allocates one contiguous run of nine data blocks
and names its first block in the superblock (`HBFS_SB_SNAP`, offset 112). Block 0
of the run is a header - magic `SNAP`, version, the map's entry count and
capacity, the journal epoch, and the superblock as of the snapshot - and the
other eight are the map: `{source LBA, saved LBA}` pairs, 4096 of them. A v4
volume also sets `HBFS4_FEATURE_SNAPSHOT`; both formats are journalled, so both
can carry one. A mount that finds the record unreadable refuses the volume
instead of forgetting it: those blocks would otherwise be handed out again.

**The hook, and the free.** Before a block's first modification while a snapshot
exists,
its old content is copied to a free block and `source -> copy` is appended to the
map. Three writers carry every write in this kernel - `hbfs_meta_write` for
metadata, `blk_cache_write` for file data, and `hbfs_save_root_dir`, which
journals and calls the device itself because it writes a single changed sector -
and the hook is in all three. The third was missing at first, and its absence
was invisible until a read was attempted: a snapshot kept every file's bytes and
none of the tree that named them. Preserving is itself a write, so a guard
covers the recursion; the
consequence is wanted, because the superblock, the bitmap and the root directory
are preserved the first time they change, which is what gives the snapshot its
own view of the volume.

Three things had to be true for that to work, and each cost a bug:

- *Block alignment.* The data area does not start on an LBA that is a multiple of
  the eight sectors in a block, so its grid is anchored at `HBFS_DATA_START`.
  Rounding a write's LBA down to a multiple of eight instead lands in the middle
  of the neighbouring block: a dropped snapshot then freed a block a file was
  using and left its own copy allocated.
- *Re-entrancy.* Preserving allocates, and allocating writes the bitmap and the
  superblock - through the same hook, inside the caller's own allocation. The
  allocator used to keep its start block and sector in module statics, so the
  inner call overwrote the outer call's answer and one block was handed to a file
  and to a saved copy at once. Those locals now live in the call's own frame.
- *Atomicity.* The copy's allocation, the copy and the map entry that names it
  are one journal transaction. Split across two, a power cut between them leaves
  a block allocated that nothing names - one leaked block per interrupted write.

**Dropping it.** `snapshot drop` frees every saved copy, then the record's run,
then clears the superblock field and the feature bit, in one transaction. That is
what returns the free count: `make check-snapshot` reads the referenced-block
count before the snapshot and after the drop and refuses an increase beyond the
one file the case itself creates.

**Everything that counts blocks has to know.** A saved copy is allocated and
named by nothing in the tree, so `repair`, `tools/fsck_hbfs.py` and
`populate.py`'s bitmap rebuild each claim the record's run and every copy. A
checker that did not would call them leaks and a repair would hand them to the
next write - the snapshot destroyed without being dropped.

**Reading it.** `snapshot cat FILE` prints a file as of the snapshot. It walks
the path through the snapshot's own copy of each directory - so a file deleted
since is still found by name - and reads each block through the map. The unit a
snapshot keeps is a window of eight sectors, and a window's start is not the LBA
a caller asks for: the data area's windows are anchored at `HBFS_DATA_START`, and
the root directory is written a sector at a time, so a request can begin part way
into a window and run into the next. Each sector is therefore resolved through
the window that holds it. Reading a whole window as if it were the block asked
for shifted a directory's entries by however many sectors the window began early,
which is how a file that existed before a snapshot came to be "not there".

**Not built.** There is one snapshot per volume, with no retention policy, no
list of several and no generations to roll forward to. The read path covers plain
files; a compressed or extent-mapped one is refused by name
(`snapshot: that file's layout ... is not readable yet`) rather than printed as
garbage.

**The capacity question, answered.** A full map (4096 preserved blocks = 16 MiB
of changed data) refuses the write, prints `HBFS: snapshot map full, write
refused`, and leaves the snapshot true - it never degrades silently.

The v4 guarantees — verified metadata, a writable journalled volume, extents, a
refusal instead of a format — are real and tested, and this document keeps every
one of them.

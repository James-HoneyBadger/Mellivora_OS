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
| Snapshots | none | Copy-on-write, per volume, retained |
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
the volume clean, and `tools/fsck_hbfs.py` agrees afterwards.

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
6. A snapshot taken mid-write is consistent, its files read correctly, and its
   deletion returns the free count to the pre-snapshot value.
7. The online checker agrees with `tools/fsck_hbfs.py` on every image the fuzzer
   produces.

## 15. As built (the gap)

| Contract | Tree today |
|---|---|
| 64-bit block numbers, 2^62 volume | 32-bit, 2 GB |
| B+tree directories, 65 535 entries | Linear directory blocks, 224 entries per subdirectory |
| ACLs, streams, compression, encryption, snapshots | **ACLs: yes**, as ACEs in the xattr block (`acl:u<id>`, `acl:g<id>`, `acl:m`, `acl:o`), evaluated by `acl_perm` before the mode word and settable from the shell with `acl set`; conformance in `acl_test`. No inheritance and no group-class unions yet. Streams, compression, encryption and snapshots: none |
| Per-user quotas | Yes, and derived rather than stored: the ledger is recomputed from the tree at every mount (`quota_build`), so it cannot drift, and `quota rebuild` does it on demand. Block accounting per uid with a ceiling where 0 is unlimited; a write past it is refused before anything is allocated, and deleting a file returns the blocks to the file's owner rather than to whoever deleted it |
| Journal with redo records | Undo journal: before-images replayed backwards |
| One unified cache | A block cache and a page cache that agree by construction |
| Partition table | Read: MBR's four primary entries, or GPT behind a protective MBR, with each volume's start, length, type, GUID and UTF-16 name in `vol_table`. The filesystem is *probed* (the superblock LBA must fall inside a volume) rather than assumed |
| Mount points | One volume, mounted from `vol_base_lba`; labels and multiple mounts are the remaining volume-model work |
| Online repair | `repair` (report) and `repair fix` (bitmap + free count) from the shell, at parity with the offline checker's classes: leaks, freed-but-claimed blocks, shared blocks, untrusted entries. `make check-repair` proves it on a corrupted copy. `repair journal` discards a log that cannot be replayed |
| Removable safe ejection | None |
| Durability gate | `make check-durability`: the crash harness over rotating workloads kills QEMU mid-write. A failed fsck *before* replay is accepted only when the volume is marked for replay - damage with a clean journal is the failure that no replay can repair - and fsck must pass after the replay, which is the assertion. `CRASH_ITERS=10000 make crash-soak` is the exit test, not yet run |

The v4 guarantees — verified metadata, a writable journalled volume, extents, a
refusal instead of a format — are real and tested, and this document keeps every
one of them.

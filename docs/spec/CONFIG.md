# Configuration: the Sett (normative)

**Normative from:** M4. **Governs:** the one hierarchical, transactional store
for every setting in the machine — kernel, driver, service and user.

Today a setting is an environment variable, a compile-time constant, a text file
that a program parses, or a number in a `.inc` file. There is no way to ask what
a setting is, who may change it, whether the change took effect, or what it was
before somebody changed it. The Sett replaces all four
([S2](README.md#decisions-this-set-records)).

The name is the badger's sett: the place a colony lives, organised in chambers,
each with its own door.

## 1. Model

```text
\Registry
├── Machine                    this machine, every user, every service
│   ├── System
│   │   ├── Boot               read by the boot path before anything else
│   │   ├── Kernel             scheduler, memory, cache policy
│   │   └── Services           service definitions (SERVICES.md)
│   ├── Software               installed packages and their settings
│   └── Security               policies, audit configuration
└── User
    └── <SID>                  one hive per account, loaded at logon
        ├── Environment
        ├── Software
        └── Desktop
```

A **key** is a node: it carries values and child keys. A **value** is a named
datum of a declared type:

| Type | Encoding | Max size |
|---|---|---|
| `STRING` | UTF-16, NUL-terminated | 1 MiB |
| `STRING_LIST` | UTF-16, double-NUL-terminated | 1 MiB |
| `U32`, `U64` | Little-endian | 4 / 8 bytes |
| `BINARY` | Opaque | 1 MiB |
| `LINK` | A path in the namespace, resolved on read | 255 code units |
| `MULTI_KEY` | A reference to a subtree of values, resolved on read | — |
| `SECRET` | A `BINARY` value that may only be read through the credential store, and only audited | 1 MiB |

Values are limited to 1 MiB so a key is a setting, not a database. A program
that needs a database uses a file ([STORAGE.md](STORAGE.md)); the Sett is for
configuration, and a configuration that needs 10 MB is a design smell the limit
makes visible.

## 2. Identity and naming

- Key and value names: 255 UTF-16 code units, case-insensitive, case-preserving
  ([ABI.md](ABI.md#8-text-and-names)).
- A path: 32 767 code units, `\` separated.
- A key is an object ([OBJECTS.md](OBJECTS.md#3-types)) with a security
  descriptor, so a setting can be readable by everyone and writable by an
  administrator — which is most of them.
- A value's effective ACL is its key's ACL unless the value carries its own
  (rare, and only for `SECRET`).

## 3. Transactions

The store is transactional, because a setting is usually more than one value:

| Call | Semantics |
|---|---|
| `SettBegin` | Starts a transaction on the calling thread; nested begins join the outer one |
| `SettCommit` | Writes every change made in the transaction, or none of them |
| `SettAbort` | Discards the transaction |
| `SettSetValues` | Sets several values atomically within a transaction |

| Property | Contract |
|---|---|
| Atomicity | A commit is all-or-nothing; a crash leaves the old values or the new ones |
| Isolation | A reader sees the last committed state; a writer's uncommitted values are private to its transaction |
| Durability | A committed write is on disk before the call returns, journaled with the hive ([STORAGE.md](STORAGE.md#5-transactions-and-recovery)) |
| Ordering | Two writers on the same key serialise; the second sees the first's committed change |
| Observers | A commit notifies watchers *after* the state is durable, never before |

A program that changes one value does not need to know about transactions: a
single `SettSet` is a one-value transaction, and the API says so.

## 4. Change notification

`SettWatch(key, subtree, filter)` returns a handle that becomes signalled when a
matching change commits; the change record carries key, value name, kind
(create/set/delete), and the transaction id.

| Rule | Contract |
|---|---|
| Delivery | Through the port the watch was created on ([IPC.md](IPC.md#2-ports)), so a watcher can be a thread, a service or a GUI queue |
| Coalescing | A burst of changes to one key within 50 ms is one notification, unless the watcher asked for every change |
| Ordering | Notifications for one key arrive in commit order |
| No missed commits | The first read of a watch reports the state at creation, so a watcher cannot miss a change that happened between "start" and "watch" |
| Cost | A watch does not hold a reference to the value's data, only to the key's identity |

This is how a theme change repaints every window without polling, and how a
service learns its configuration changed without restarting.

## 5. Hives

| Hive | File | Loaded |
|---|---|---|
| `Machine` | `\System\Config\Machine.hiv` | At boot, before the first driver |
| `User\<SID>` | `\Users\<SID>\NTUSER.DAT` (analogue) | At logon |
| `Boot` | A small hive inside the boot image | Before the file system is mounted |
| `Volatile` | In RAM, never written | At boot; lost on `S5` |

A hive file is a **cell-based** format, the same idea as HBFS's B+tree but tuned
for read-mostly access:

```text
header        magic 'MHIV', version, size, checksum, generation
bins          4 KiB units, each with its own offset map and checksum
cells         variable-length, referenced by offset; a value is a cell
key nodes     name, parent, child list, value list, security offset
supporting   a security table, a subkey index for large keys (a B+tree leaf)
```

| Rule | Contract |
|---|---|
| Verify | Every bin's checksum is checked at load; a corrupt bin refuses the load and is reported by offset |
| Recovery | A log file (`*.hiv.log`) holds the last transaction's after-images; replay makes the load idempotent |
| Size | A hive is limited by the volume, not by the format; a hive is loaded by mapping, not by reading it all |
| Compaction | Unused cells are reclaimed by a background compaction that is itself a transaction |
| Read-only | A hive on read-only media mounts read-only and writes are refused with the hive named |

## 6. Boot configuration

The kernel, the loader and the earliest drivers need settings before a file
system exists. Two mechanisms, both Sett-shaped:

1. The **boot information block** ([EXEC.md](EXEC.md#9-boot-information)) carries
   the loader's own parameters: memory map, video mode request, boot device,
   debug flags, dump device.
2. The **boot hive** is embedded in the boot image, cell-based like any hive, and
   is loaded into the volatile store before the first driver starts. A setting
   that must be identical on every machine (a service's start type) lives in
   `Machine`; a setting that depends on *this* boot (a debug flag) lives in the
   boot hive.

Once `Machine` is mounted, the boot hive's keys are available under
`\Registry\Machine\System\Boot` and a program cannot tell which hive answered.

## 7. Schema, defaults and migration

| Rule | Contract |
|---|---|
| Schema | Every key a component reads has a declared type, a default and a documented range in its own specification; the store does not guess |
| Defaults | A missing key returns the declared default; it does not create the key |
| Versioning | A key that changes shape carries `Version`; a reader refuses a version it does not know rather than reading the wrong bytes |
| Migration | A migration is a transaction that reads the old version, writes the new one and writes `Version` last, so a crash mid-migration leaves the old version intact and repeatable |
| Unknown keys | Preserved, never pruned, so a downgrade does not delete a newer component's settings |
| Rollback | Each hive keeps a last-known-good generation; a load that fails validation falls back one generation and audits it |

## 8. Security

- Every key has a descriptor; the default for a machine key is
  administrators-writable, users-readable ([SECURITY.md](SECURITY.md#53-default-descriptors)).
- `SECRET` values are readable only through the credential store and every read
  is audited; the store never returns the plaintext to a process that is not
  entitled to it, even as an administrator.
- A service reads its configuration through the same checks as a user: there is
  no "kernel bypass" for a setting.
- A descriptor change is audited with the old and new hash, and a change that
  removes administrator access requires `SE_TAKE_OWNERSHIP`.

## 9. Migration from what exists

| Today | Becomes |
|---|---|
| Environment variables (`PATH`, `HOME`, `TERM`, …) | `\Registry\User\<SID>\Environment` and a compatibility layer that synthesises the environment at exec ([EXEC.md](EXEC.md#52-exec-and-exit)) |
| Compile-time constants for defaults | Values read at start with the same default compiled in, so a missing hive is not a broken machine |
| Text config files a program parses | Keys owned by that program's package ([ECOSYSTEM.md](ECOSYSTEM.md)) |
| Command-line flags | Keys with an override that lasts for one process (`SettSetVolatile`) |

The compatibility layer is permanent: a program that reads `PATH` from its
environment keeps working ([S3](README.md#decisions-this-set-records)).

## 10. Conformance

M4 does not close until:

1. `sett_test` crashes the machine at every phase of a multi-value commit and
   proves the store is either wholly old or wholly new.
2. A watcher observes create, set and delete in commit order, with coalescing
   bounded, and a watch created after a change still reports the current state.
3. A `SECRET` value cannot be read without the credential store, even as an
   administrator without the audit privilege, and every attempt is recorded.
4. `sett_migrate_test` interrupts a migration at every step and re-runs it; the
   final state is correct and idempotent.
5. A hive with a corrupted bin is refused by name and offset, and the
   last-known-good generation is used when present.
6. The environment compatibility layer produces the same values the tree has
   today for every variable the existing programs read.

## 11. As built (the gap)

| Contract | Tree today |
|---|---|
| One hierarchical store | Environment variables, compile-time constants and ad-hoc files |
| Transactions | None |
| Change notification | None |
| Key ACLs | None |
| Hives | None |
| Boot configuration | A boot information block only |

Nothing in this document exists yet; it is the first thing M4 builds.

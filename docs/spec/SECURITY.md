# Security model (normative)

**Normative from:** M1. **Governs:** who a process is, what it may do, how a
refusal is decided, what is written down when a decision is made, and what
protects the kernel from its users.

The model is **capability-based in spirit and ACL-based in mechanism**: every
object carries a descriptor, every handle carries rights, and nothing a process
can reach is reachable without a check that names the right it needed
([S6](README.md#decisions-this-set-records)).

## 1. Threat model

In scope:

- a hostile **application** at user privilege: downloaded, packaged, buggy or
  deliberate;
- **corrupted or crafted on-disk data** — a damaged volume, a doctored image;
- **malformed input** from the network, the GUI event queue, a driver and every
  syscall argument;
- a **buggy application** that crashes: it must not take the machine with it;
- a **hostile local user** on a multi-user machine, including one who can run at
  the same privilege as the victim;
- a **compromised service** running with a service token.

Out of scope, and stated so nobody assumes otherwise: physical attacks, malicious
firmware, microarchitectural side channels beyond the mitigations in §11, and a
hostile kernel (the kernel is the trusted computing base).

## 2. Identities

### 2.1 Security identifiers

A **SID** is a variable-length identity: `u8 revision`, `u8 subauthority count`,
6 bytes of authority, then up to 15 `u32` subauthorities. The well-known set is
fixed in `c/include/mv/sids.h`:

| SID | Meaning |
|---|---|
| `S-1-5-18` | Local System — the kernel's own authority for service work |
| `S-1-5-19`, `S-1-5-20` | Local Service, Network Service |
| `S-1-5-32-544` | Administrators |
| `S-1-5-32-545` | Users |
| `S-1-5-32-546` | Guests |
| `S-1-5-32-555` | Remote Desktop Users |
| `S-1-5-32-568` | IIS_IUSRS-analogue: sandboxed service accounts |
| `S-1-5-4` | Interactive logon |
| `S-1-5-7` | Anonymous |
| `S-1-5-11` | Authenticated users |
| `S-1-5-21-…` | A machine-local account |

Floating SIDs — "logon session", "restricted", "integrity" — are computed at
logon and never stored.

### 2.2 Tokens

A **token** is an object ([OBJECTS.md](OBJECTS.md)) describing a security
context. Every process and thread has one; a thread may temporarily assume
another for impersonation.

| Field | Contents |
|---|---|
| `user` | The account SID |
| `groups[]` | Up to 64 group SIDs, each with a `MANDATORY` or `ENABLED`/`DISABLED` flag |
| `privileges[]` | Up to 32 entries: `{luid, attributes}` (enabled, default, used-for-access) |
| `integrity` | Level 0..5: untrusted, low, medium, high, system, plus a policy mask |
| `session` | The session the token belongs to ([OBJECTS.md](OBJECTS.md#5-the-namespace)) |
| `restricted[]` | Optional list of restricting SIDs (a sandbox) |
| `logon_type` | Interactive, network, batch, service, unlock |
| `impersonation_level` | Anonymous, identify, impersonate, delegate |

Logon produces a **primary** token; impersonation produces an **impersonation**
token that may never be assigned to a process, only to a thread, and only at or
below the primary token's impersonation level.

## 3. Privileges

A privilege is a right to bypass a class of check. It is not an identity and it
is not inherited by every process: a token holds privileges, and most are
disabled until a program asks for them.

| Privilege | Gates |
|---|---|
| `SE_BACKUP` / `SE_RESTORE` | Reading or writing an object regardless of its ACL (for backup tools) |
| `SE_DEBUG` | Opening any process or thread for any right |
| `SE_TAKE_OWNERSHIP` | Changing an object's owner, and `WRITE_OWNER` without the ACL bit |
| `SE_LOAD_DRIVER` / `SE_UNLOAD_DRIVER` | Loading and unloading a module ([DRIVERS.md](DRIVERS.md)) |
| `SE_SHUTDOWN` | Power state changes ([PLATFORM.md](PLATFORM.md)) |
| `SE_AUDIT` | Reading and clearing the security log |
| `SE_SESSION_CROSS` | Addressing another session's namespace |
| `SE_TCB` | Raw access to the kernel's own objects |
| `SE_CHANGE_NOTIFY` | Watching a directory or key for changes |
| `SE_TIME_ZONE`, `SE_SYSTEMTIME` | Setting the clock |
| `SE_NETWORK_BIND_LOW` | Binding a port below 1024 ([NETWORK.md](NETWORK.md)) |
| `SE_CREATE_PAGEFILE`, `SE_INCREASE_QUOTA` | Memory policy |
| `SE_SECURITY` | Reading or writing an object's *audit* ACEs and the security log |

`SE_SESSION_CROSS` and `SE_TCB` replace the old "uid 0 is the kernel" rule. UID
0 survives as a **compatibility identity** for the frozen syscalls
([ABI.md](ABI.md#5-status-codes)): it maps to the Administrators group with
significant privileges, and the legacy checks that read `uid == 0` keep working
on it. Nothing new may use uid alone as an authorisation decision.

## 4. Integrity and mandatory policy

Every object and every token carries an **integrity level**. The level is a
floor, not a ceiling: a **no-write-up** rule applies to writable access.

| Actor \ Target | Untrusted | Low | Medium | High | System |
|---|---|---|---|---|---|
| Untrusted | read/write | read | read | read | read |
| Low | read/write | read/write | read | read | read |
| Medium | read/write | read/write | read/write | read | read |
| High | read/write | read/write | read/write | read/write | read |
| System | read/write | read/write | read/write | read/write | read/write |

A process is born at the level its token carries: an unpackaged program at
**medium**, a sandboxed one at **low**, a service at **high**, the kernel and its
threads at **system**. A downloaded program that the user marks untrusted learns
the model the useful way: it can still run, it cannot modify the machine.

The rule is evaluated **before** the ACL check and can only deny. `SE_DEBUG` does
not cross it; `SE_TCB` does.

## 5. The access check

One function, `mv_access_check`, called from every path that turns a name or a
duplicated handle into a handle ([S8](README.md#decisions-this-set-records)).
[OBJECTS.md](OBJECTS.md#9-security-hook) names the call sites.

```text
Inputs:  token, security descriptor, desired rights, optional object path
Output:  granted rights, or MV_ACCESS_DENIED with the right that failed

1. If the descriptor is NULL, grant MV_RIGHTS_ALL (unprotected object).
2. If the caller demanded MV_RIGHTS_ALL, map it to the type's specific rights.
3. Mandatory policy (§4): if the access is a write and the level is below the
   object's, deny, recording MANDATORY in the audit record.
4. Walk the DACL in canonical order and accumulate:
     - ALLOWED if the SID matches and the ACE is an allow
     - DENIED  if the SID matches and the ACE is a deny
   A deny wins over an allow for the same SID, in ACE order.
5. If any restricting SID is present, the access must also be allowed by the
   restricting ACLs; a denial there is final.
6. If the caller's token holds the privilege that bypasses this check
   (§3), grant and record BYPASS.
7. If the DACL is absent, deny. If the DACL is present and empty, grant nothing.
8. If the desired rights are not all present, deny and name the missing right.
```

`MV_RIGHTS_ALL` (generic all) maps per type to its specific rights, so a caller
that asks for "all" on an event gets `SIGNAL | QUERY | SET`, not `READ_DATA`.
The mapping lives in the type descriptor and is asserted by a test for every
type.

### 5.1 Descriptor and ACE format

```text
security descriptor
  u8  revision
  u8  sbz0
  u16 control          /* DACL_PRESENT, SACL_PRESENT, SELF_RELATIVE, PROTECTED */
  u32 owner_sid_off
  u32 group_sid_off
  u32 sacl_off
  u32 dacl_off
  (SIDs and ACLs follow, offsets from the descriptor's start)

ACE (header)
  u8  type             /* 0 allow, 1 deny, 2 audit, 3 alarm,
                          4 compound allow (object ACE), 5 compound deny,
                          6 mandatory label, 7 resource attribute,
                          8 scoped allow (bounding) */
  u8  flags            /* inherited, inherit-only, no-propagate-inherit,
                          no-propagate-inherit-only, audit-success,
                          audit-failure */
  u16 size             /* bytes, including the header */
  u32 mask             /* the rights the ACE grants, denies or audits */
  SID sid
  [u32 object_type_guid_lo, guid_hi, inherited_type_lo, inherited_type_hi]
```

Canonical order is **deny, allow, audit**, each group ordered so that
object-specific ACEs precede generic ones. A descriptor that is not canonical is
rejected at parse time, because two orderings of the same ACEs are two different
security policies and only one of them is the one the administrator wrote.

### 5.2 Inheritance

- An ACE with `INHERITED` came from a parent and is never edited in place; the
  parent's change propagates.
- `INHERIT_ONLY` applies to children only.
- `NO_PROPAGATE_INHERIT` stops the chain at the next level.
- `PROTECTED` on a descriptor stops inheritance for that object entirely.
- Default propagation is **containers and leaves** (directories and files, keys
  and values), which is what an administrator means nine times out of ten.

### 5.3 Default descriptors

A new object gets the descriptor its type names, not "everything allowed":

| Created in | Default owner | Default DACL |
|---|---|---|
| A user's session directory | The creating user | The creator full, `Users` read, `SYSTEM` full |
| `\Devices`, `\Drivers`, `\Registry` (machine) | `SYSTEM` | Administrators full, `Users` read |
| `\KernelObjects` | The creating process | The creator full, `SYSTEM` full |
| A named port | The creating process | The creator full, `SYSTEM` full, no implicit `Users` |
| A service's own objects | The service's token | The service full, administrators read |

## 6. Application capability manifests

A published application declares what it needs in its manifest
([ECOSYSTEM.md](ECOSYSTEM.md)). **A manifest is a request, not a grant.**

- At install, the user sees the list in plain language and approves it once.
- At launch, the launcher builds a token: the user's identity, restricted
  integrity if the app is sandboxed, and **only** the privileges the manifest was
  granted.
- An undeclared access is refused exactly like a missing ACL bit — same status,
  same audit record, same message.
- A package update that widens the request re-prompts; it never widens silently.

This is how "the app can read my documents" stays a decision the user made
rather than a property of the desktop session.

## 7. Audit

Security-relevant decisions are recorded, because a refusal nobody can explain is
a support call nobody can answer.

| Event | Fields |
|---|---|
| Logon / logoff / failed logon | account, session, type, source |
| Access denied | object name, requested rights, missing right, token SIDs |
| Privilege use | privilege, process, object if any |
| Descriptor change | object, old and new owner/group/DACL hash |
| Driver load/unload | module, signer, image hash |
| Package install / removal | package, publisher, granted capabilities |
| Mandatory policy denial | integrity levels, object |
| Audit log cleared | who, when |

Records go to an `eventlog` object ([OBJECTS.md](OBJECTS.md#3-types)) with a
monotonic sequence number and a per-record checksum chained to the previous one,
so a deletion shows up as a gap and an edit as a broken chain. `SE_SECURITY`
gates reading and clearing. An audit buffer that is full **drops with a counted
gap**, never blocks the machine.

## 8. Kernel self-protection

| Control | Requirement |
|---|---|
| W^X | No page is both writable and executable, in ring 0 or ring 3. `MAP_JIT` is a capability, not a flag. |
| Kernel text | Mapped read-only; `CR0.WP` set so a stray kernel write faults instead of succeeding |
| Kernel base | Randomised within the kernel window at boot (KASLR-lite); the offset is reported only to `SE_TCB` |
| SMEP / SMAP | Enabled where `CPUID` offers them; user pointers are never dereferenced without an explicit copy |
| User pointer validation | Every pointer and length pair goes through `mv_probe_user` (range, overflow, wrap) before use; a bad one returns `MV_INVALID_PARAMETER`, it never faults the kernel |
| Kernel stacks | One per task, guard page below, canary above; overflow panics with the task name |
| Allocator metadata | Magic numbers and canaries on every block; a corrupted header stops the machine naming the block |
| Double-free | The slub and the frame allocator refuse a second free and count it ([RELIABILITY.md](RELIABILITY.md)) |
| Speculation | `IBRS`/`IBPB` or retpoline-equivalent at the syscall and interrupt boundary, chosen by `CPUID`; documented in `PLATFORM.md` |
| Fault containment | A ring-3 fault kills the task, not the system ([RELIABILITY.md](RELIABILITY.md#3-fault-containment)) |

## 9. Secrets

- Passwords are stored as a salted, memory-hard hash (Argon2id parameters fixed
  in `SERVICES.md`); the plaintext exists only for the duration of the logon.
- The credential store is encrypted at rest with a machine key held in the
  platform's secure store where one exists, and derived from a passphrase
  otherwise ([CONFIG.md](CONFIG.md)).
- A process may ask for a secret only through the store, only for a secret its
  token may read, and the read is audited.
- No secret is ever placed in an environment variable, a command line, the
  clipboard history or a crash dump. The dump writer redacts by address range,
  and the range is set by the store.

## 10. Signals, crashes and refusals

- `kill` reaches only a task the sender may touch: itself, a task of the same
  user, or anything with `CAP_PROC_CONTROL`/`SE_DEBUG`. A process-group signal
  that includes a task the sender may not touch is refused **whole**, so a
  partial delivery cannot be mistaken for success. [As built and tested.]
- A user-mode fault kills the faulting task and reports
  `[fault] task killed, eip=… addr=…`. The desktop and the shell survive.
  [As built and tested.]
- A supervisor fault is fatal: panic with a symbolised stack, dump if configured,
  restart ([RELIABILITY.md](RELIABILITY.md)). Silently continuing after a kernel
  fault is never acceptable.
- A refusal always names the check: `MV_ACCESS_DENIED` carries the missing right
  in the audit record, and the user-visible message says which. "Access denied"
  alone is not an acceptable message.

## 11. As built (the gap)

Enforced and tested today:

| Rule | Test |
|---|---|
| Mode bits deny for `open`, `SYS_FWRITE`, `unlink`, `create`, `mkdir`, `rename`, `truncate`, `rmdir` | `perm_test`, `dirperm_test` |
| `chmod` is the owner's, `chown` is root's, `setuid`/`setgid` move the effective ID | `perm_test` |
| A handle carries its rights; a read-only handle cannot write back, even as root | `handle_test` |
| Eleven refusals and two controls from an unprivileged program, machine survives | `hostile_test` |
| `kill` follows the uid policy, whole-or-nothing per group | `hostile_test` |
| A security descriptor with a DACL of ACEs, walked canonically, deny before allow | `object_test` |
| `mv_access_check` runs on every path that turns a name into a handle | `object_test`, `osmoke` |
| Traverse is required on each directory crossed, and a type's `MV_RIGHTS_ALL` is mapped by the type | `object_test` |
| A duplicate may only narrow the rights of the handle it came from | `object_test` |
| Mandatory no-write-up: a medium-integrity process cannot write into a SYSTEM-integrity object whose DACL allows it | `object_test` |
| A crossed-session name is refused without `SE_SESSION_CROSS` | `object_test` |

What M1 added, concretely: `kernel/object.inc` holds one access check
(`obj_access`), one identity source (`obj_identity`, which reads the TCB the
frozen syscalls already check), a fixed-layout descriptor with a fixed-format
ACE array, and the privilege test that bypasses a check. Since the identity is
the TCB's, `setuid`/`setgid` changed what the object manager sees at the same
moment it changed what the frozen syscalls see - there is no second copy to
disagree.

Stated plainly, and to be removed from this section as the waves land:

- SIDs are not variable-length structures yet. Identity is a 32-bit uid read
  from the TCB; the well-known SID set and group lists arrive with logon.
- Privileges are held by "uid 0 and nothing else" rather than by a token's
  privilege list. `SE_SESSION_CROSS`, `SE_DEBUG` and `SE_TCB` therefore behave
  as uid 0 holds them; the other privileges are defined and not yet granted.
- A descriptor is a fixed-size record with up to six fixed-format ACEs. There
  are no object ACEs, no SACL, and no inheritance: a new object gets its type's
  default descriptor and nothing is propagated from its parent.
- Integrity is derived from identity (uid 0 is SYSTEM, everyone else MEDIUM)
  rather than carried in a token, so there is no way for a program to be
  *marked* untrusted yet.
- There is no audit trail beyond the reason word the check leaves in
  `obj_audit_last`; the ring buffer and the event log are M4.
- Directory search permission and the sticky bit are not enforced by the file
  system; the object namespace enforces traverse on its own directories.
- Mount options (`noexec`, `nosuid`) do not exist ([STORAGE.md](STORAGE.md)).

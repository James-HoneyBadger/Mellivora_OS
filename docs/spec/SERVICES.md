# Services, logon and sessions (normative)

**Normative from:** M4. **Governs:** what starts before a user logs on, who a
user is when they log on, what a service may touch, and how two users share one
machine without rebooting.

The tree today boots to a shell. There is one identity (uid 0), no services, no
logon and no session: a program runs, and the console is whoever is typing. This
document is the system that replaces that.

## 1. Services

A **service** is a program the service control manager (SCM) starts, stops and
restarts, with its own account and without a console.

| Property | Values | Notes |
|---|---|---|
| Name | 256 UTF-16 code units, unique under `\Services` | What `sc` takes |
| Display name | Human text | Shown in the management console |
| Image path | A path plus arguments | Resolved with the service's token |
| Start type | `boot`, `system`, `auto`, `manual`, `disabled` | `boot`/`system` start before the file system's cache is warm (`boot` before `system`) |
| Account | `LocalSystem`, `LocalService`, `NetworkService`, or a user SID + password | Determines the token ([SECURITY.md](SECURITY.md#22-tokens)) |
| Dependencies | Service names and **load-order groups** | A cycle is refused at install, not at start |
| Recovery | Up to three actions on failure: restart, run a program, reboot | With a reset period and a backoff |
| Isolation | Own process, or a shared host process with a restricted token | The default is own process; sharing is an explicit opt-in |
| Privileges | The subset of the account's privileges the service may use | Everything else is disabled in its token |
| Session | `0` by default; an interactive service may run in a user session | Session 0 is not the console session |

### 1.1 Start sequence

```text
boot drivers -> system drivers -> SCM -> boot services -> system services
            -> logon screen -> auto services -> desktop for the logged-on user
```

- The SCM starts a service only when every dependency reports `RUNNING`, and it
  starts independent services **in parallel** with a bounded fan-out.
- A dependency that fails to start fails the dependants with a status naming the
  dependency, rather than letting them fail in a confusing way of their own.
- Each start has a **timeout** (30 s by default, per service). A service that
  misses it is marked failed and its recovery actions run.
- `system` services start before the logon screen exists, which is why a
  service's own objects must be creatable in session 0
  ([OBJECTS.md](OBJECTS.md#5-the-namespace)).

### 1.2 Lifecycle

| Call | Contract |
|---|---|
| `Start` | SCM creates the token, starts the image, waits for `RUNNING` or `START_PENDING` then `RUNNING` |
| `Stop` | Sends `STOP`; if the service does not report `STOPPED` within its timeout, the SCM kills the process and records the kill |
| `Pause` / `Continue` | Optional; a service that does not support them says so |
| `Interrogate` | Reports the current state, exit code and checkpoint |
| `Configure` | Changes start type, account or recovery; audited |
| `Delete` | Refused while running; the service's keys are removed in one transaction |

A service may refuse a stop **once** with a reason, which is what makes a
database flush before shutdown possible. Refusing twice is a bug: the SCM kills
it and records the refusal.

### 1.3 Recovery

| Failure | Action |
|---|---|
| Exit non-zero | The configured action, after a backoff of 1 s, 5 s, 30 s |
| Start timeout | The configured action, with the attempt counted |
| Crash of a shared host | Every service in the host restarts; the failure is attributed to the host, not to an innocent service |
| Repeated failure | After the attempt count's worth of consecutive failures the SCM stops retrying and reports `FAILED` with the count. The reset period is what makes them *consecutive*: a service that runs on without failing for that long begins a new streak, so a failure hours later is the first of the new one |

Every start, stop, failure and recovery action is an audit record
([SECURITY.md](SECURITY.md#7-audit)).

## 2. Logon

The logon process is deliberately small: it authenticates, builds a token and
starts a session. Everything else follows from the token.

```text
1. The logon screen runs with a restricted token (no user, `SE_TCB` only).
2. Credentials are checked against the account store; passwords are Argon2id
   ([SECURITY.md](SECURITY.md#9-secrets)).
3. A primary token is built: user SID, groups, privileges, integrity level,
   session id, logon type.
4. The profile is created or loaded (SS3).
5. A session is created: its namespace, its window station, its desktop, its
   objects under \Sessions\<n>.
6. The shell starts with the token, and the profile's environment is
   synthesised ([CONFIG.md](CONFIG.md#9-migration-from-what-exists)).
```

| Failure | Result |
|---|---|
| Bad password | A counted failure, an audit record, a delay that grows to 2 s, and a lockout after the configured count |
| Expired or disabled account | Refused with a reason that does not disclose whether the account exists |
| No profile | A profile is copied from the default profile, and the copy is a transaction |
| Logon type not permitted | Refused by policy (interactive, network, batch, service, unlock are separate) |

`SE_SESSION_CROSS` is what lets an administrator see another session; without
it, session 1 cannot name session 0's objects, and a service cannot draw on the
user's desktop.

## 3. Profiles

| Piece | Contents | Created |
|---|---|---|
| `\Users\<SID>` | Documents, desktop, downloads, application data | First logon, copied from the default profile |
| `NTUSER.DAT` analogue | The user's hive | First logon, then loaded per logon |
| `AppData\Local`, `AppData\Roaming` | Per-machine and per-user application data | First logon |
| Environment | Synthesised from the hive plus machine values | Every logon |

Deleting a profile is a transaction: the hive is unloaded, the directory is
removed, and a failure leaves the profile intact and reported rather than half
deleted.

## 4. Sessions

A **session** is a security and namespace boundary — not a terminal, not a
window station:

| Piece | Per session |
|---|---|
| Namespace | `\Sessions\<n>` with `BaseNamedObjects` and `DosDevices` ([OBJECTS.md](OBJECTS.md#5-the-namespace)) |
| Window station and desktop | At least one each; a service's window station is not the user's |
| Input | The console or the remote session owns it; only one session has the physical console |
| Devices | A session may hold devices exclusively (audio, USB); the holder is recorded |
| Objects | Ports, sections and events are session-scoped unless created machine-wide |

### 4.1 Switch user, lock, disconnect, logoff

| Operation | What happens |
|---|---|
| **Switch user** | The current session stays alive and disconnected; a new session is created for the new token. No reboot, and the outgoing session's programs keep running |
| **Lock** | The session's desktop is replaced by the logon screen; the token is retained but the session cannot draw or receive input |
| **Unlock** | Re-authentication against the same account; a different account does not unlock it |
| **Disconnect** | The session keeps running; its window station is not rendering; a program that requires a desktop is told |
| **Logoff** | Programs are asked to exit, then killed after a timeout; the profile is flushed and the hive unloaded; the session's objects are destroyed |

Switch-user is the reason the session namespace exists. Without it, a second
user's object names would collide with the first's, and "switch" would have to
mean "log the first one out".

## 5. Management

| Surface | Provides |
|---|---|
| `sc` (shell) | Service state, start/stop/configure, dependencies, recovery |
| `whoami` / `sessions` | Identity, groups, privileges, session list, who owns the console |
| A management console application | The same over the service objects, for the desktop ([SHELL.md](SHELL.md)) |
| Remote | The same over RPC with `SE_SESSION_CROSS` and an authenticated transport ([NETWORK.md](NETWORK.md#6-remote-administration)) |

## 6. Conformance

M4 does not close until:

1. A dependency cycle is refused at install time, naming both ends.
2. A service that exits non-zero is restarted according to its recovery actions,
   with backoff, and after the reset period is marked `FAILED` with the count.
3. A service that ignores `STOP` is killed after its timeout, and the kill is
   audited.
4. Boot services and system services start before the logon screen; auto
   services start after it; the logon screen appears with no service running in
   the user's session.
5. Logon, lock, unlock, switch user, logoff and a second logon complete without a
   reboot, and session 1 cannot address session 0's objects without
   `SE_SESSION_CROSS`.
6. A crashed logon screen does not let anybody in: the session remains locked.
7. A profile is created, used, logged off and deleted with no leaked handle and
   no leaked file.

## 7. As built (the gap)

| Contract | Tree today |
|---|---|
| Services, dependencies, recovery | **Yes.** A record is a key under `\Registry\Machine\System\Services\<name>` (`Image`, `Start`, `DependOn`, `Attempts`, `Failure1..3`, `Reset`, `StartTimeout`); `sc create/delete/start/stop/query/list/dep/failure/reset/timeout/attempts`. A dependency is started first and a missing one fails the dependant by name; a cycle is refused when the record is written. Exit zero is a clean stop |
| Start sequence | **Yes for boot and system.** Boot services start at boot, system services after them, auto services at logon; the boot log names each start, and `sessions` shows session 0 with nobody logged on. `StartTimeout` (30 s by default, `0` = no deadline) bounds the launch: a start that misses it, or that cannot produce a task at all, is a failure with the attempt counted and the policy's action run, not a service that silently stops |
| Recovery actions | **Yes.** Three stored actions, taken in order as the failures accumulate: `restart` (after 1 s, 5 s, 30 s), `run:IMAGE` (the named program runs in place of a restart), `reboot`, and `none`. `sc failure NAME [A1 [A2 [A3]]]` sets them and prints them when given no action, `sc reset NAME SECS` sets the period (60 s) after which a service that has run clean begins a new streak, and `sc attempts NAME N` the cap that ends one. A `run:` or `reboot` action is recorded by the failure and carried out by the SCM tick: a failure is noticed inside the dying task's own address space, and loading a program there would build the image under that task's page directory. Neither action restarts the service, so both leave it `FAILED` |
| Shared host processes, service privileges | **None.** Every service is its own task, and the token is the launcher's: session 0, SYSTEM |
| Logon, tokens, sessions | **Yes.** `account add/del/list`, `logon`, `logoff`, `lock`, `unlock`, `switch`, `sessions`. Accounts live in the Sett with a uid, a salt and a hash; a logon verifies the password, makes a session, creates `/Users/<name>`, loads `<name>.hiv` into `\Registry\User\<name>` and starts the auto services. `TCB_SESS` carries the session, and `\Sessions\<n>\` is refused to another session without `SE_SESSION_CROSS` |
| Profiles | **Partly.** A directory and a hive file per account, created at logon and saved at logoff. The default-profile copy, `AppData\Local` and `AppData\Roaming` are not created |
| Switch user, lock, disconnect | **Partly.** `switch` moves the console between live sessions and `lock`/`unlock` re-authenticate the current one. A session has a namespace but no window station of its own, so "separate desktops" means separate session namespaces, not two composited desktops; `disconnect` is not a command |
| Service management surface | `sc` and `sessions`/`whoami` in the shell. There is no management console application (SHELL.md) and no remote surface (NETWORK.md) |
| Password hashing | A salted, iterated AES-256 construction, eight applications per half. It is not Argon2id, it is not memory-hard, and section 2 says Argon2id: a real KDF re-hashes these stores when it lands |

`make check-runtime` carries the gates: a service is created and started, a
cycle is refused with both ends named, a crash-loop is counted out to `FAILED`,
the policy is stored and shown by `query`, a failed service runs its recovery
program, a start that never appears runs its program too, a failure streak is
kept until the reset period passes and forgotten after it, a boot service is
seen in the boot log before any session exists, one account logs on to session
1, and two accounts hold sessions 1 and 2 at once.

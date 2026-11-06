# Networking (normative)

**Normative from:** M7. **Governs:** the socket API as objects, the stack's
contracts, name resolution, file sharing, RPC and remote administration.

The tree today has a real TCP/IP stack — ARP, ICMP, DHCP, DNS, a full TCP state
machine, sockets, `poll`, non-blocking connect — and no way for one machine to
share a folder with another. This document adds what the parity target calls
"a shared folder another machine mounts", and it does it with **our own
protocol** ([D5](../ROADMAP.md#architectural-decisions)): SMB is a multi-year
compatibility project and a half-finished one satisfies nobody.

## 1. Positions

| Position | Rule |
|---|---|
| Protocols | `TCP`, `UDP`, `ICMP`, `ARP`, `DHCP`, `DNS` as the transport set; `TCP` and `UDP` are what applications see |
| Addressing | IPv4 first-class; IPv6 out of scope for parity, with the socket layer written so it is an address family rather than a rewrite |
| The socket is an object | A handle, rights, waits and completions ([OBJECTS.md](OBJECTS.md#3-types)) — not a number in a table |
| Compatibility | The existing socket syscalls keep working forever ([S3](README.md#decisions-this-set-records)) and are implemented over the socket object |
| Security | A bind below 1024 needs `SE_NETWORK_BIND_LOW`; every listening endpoint has an ACL; the firewall is default-deny inbound ([§7](#7-firewall)) |

## 2. The socket object

| Right | Means |
|---|---|
| `READ_DATA` | `recv`, `accept` |
| `WRITE_DATA` | `send`, `connect`, `shutdown` |
| `SET_STATE` | Options, `bind`, `listen`, `ioctl` |
| `SYNCHRONIZE` | Wait for readable, writable or hangup |

| Property | Contract |
|---|---|
| Blocking | A socket may be blocking or non-blocking; the mode is a property of the object, not of the call |
| Async | Every blocking operation can be issued as an overlapped request that completes on a port ([IPC.md](IPC.md#21-completion-ports)); `select`/`poll` are compatibility wrappers over the same wait |
| Timeouts | Per socket and per call; a timeout is a status, never a zero-length success |
| Options | `SO_REUSEADDR`, `SO_KEEPALIVE`, `SO_RCVBUF`, `SO_SNDBUF`, `TCP_NODELAY`, `SO_LINGER`, `IP_TTL`, `SO_ERROR` — the list a program actually uses, each with a documented default |
| Buffer accounting | Receive and send buffers are charged to the owning process's job ([SCHED.md](SCHED.md#7-jobs)); a sender that floods itself is throttled |
| Shutdown | `SHUT_RD`, `SHUT_WR`, `SHUT_RDWR`; data already queued for sending is still sent on `SHUT_WR` |
| Close | A close with unread data sends a reset, as TCP requires, and says so in the audit record if the socket was listening |

## 3. The stack's contracts

| Layer | Contract | Test |
|---|---|---|
| Ethernet | Frames under 1 518 bytes, no jumbo | Existing driver tests |
| ARP | Cache with ageing, gratuitous ARP on address change, entry states | `net` suite |
| IP | Fragmentation on send and reassembly, with a bounded reassembly table; TTL decrement; checksum verified and counted | `packet_fuzz`, `net` suite |
| ICMP | Echo, destination unreachable, time exceeded; **rate-limited** | `ping` case |
| TCP | Full state machine, retransmission with backoff and RTT estimation, window scaling, delayed ACK, keepalive, TIME_WAIT limit | Existing TCP cases, plus a loss/reorder harness |
| UDP | Datagram semantics, checksum verified, no silent truncation | Suite |
| DHCP | Lease acquisition, renewal, release, and the option set the stack needs | `dhcp` case |
| DNS | A and PTR, with a negative cache and a bounded retry policy | `dig example.com` case |
| Loopback | A real interface, so two processes can use the stack without a NIC | Suite |

Every length parsed out of a frame is bounded before it is used; a malformed
frame is counted and dropped, never trusted. `tools/packet_fuzz.py` is the gate.

Quality of service beyond this is out of scope. Congestion control is TCP's
standard algorithm, named in the source, not tuned.

## 4. Shares

A **share** is a directory exported under a name, reachable by another machine
as a path.

| Piece | Server side | Client side |
|---|---|---|
| Name | `\\<machine>\<share>` in the namespace, and a path under `\Sessions\<n>` | A mount point or a drive letter ([STORAGE.md](STORAGE.md#2-volume-model)) |
| Authentication | Challenge-response against the credential store; the client's identity is a token on the server ([SECURITY.md](SECURITY.md#22-tokens)) | Credentials from the logon session, or an explicit prompt |
| Authorisation | The share's ACL **and** the file's own ACL, both checked; a share grants nothing the file does not | Nothing local |
| Protocol | Length-prefixed request/response over TCP, one connection per session, with a sequence number and a session key | Same |
| Integrity | Every message carries a MAC keyed by the session key; a mismatch drops the connection | Same |
| Files | Open, read, write, close, stat, rename, delete, list — the operations the VFS already has, mapped one to one | Same |
| Leases | A read or write lease (an oplock) so a client may cache; a break is sent before another client writes | Honours and breaks |
| Reconnect | A session that loses its connection is re-established with the same identity and re-opened handles | Retries with backoff |
| Path semantics | Case-insensitivity, name length and attribute set are the server volume's, and the client must not assume otherwise | Reports the server's semantics |

The redirector seam in the VFS ([STORAGE.md](STORAGE.md#12-concurrency-and-sharing))
is what makes a share look like a volume: the file system layer does not know
whether a handle is local or remote, and the cache treats both the same way.

### 4.1 The wire format as built

One message is a 16-byte header, a payload, and a 16-byte MAC:

| Offset | Field | Meaning |
|---|---|---|
| 0 | magic | `MVSH` |
| 4 | version | 1 |
| 5 | type | `HELLO`, `CHALLENGE`, `AUTH`, `OK`, `DENY`, `VIEW`, `LIST`, `STAT`, `READ`, `WRITE`, `SVC`, `LOGON`, `LOGONPROOF`, `PRINT`, `RPC` (§5.1), `CONSOLE` (§6.1) |
| 6 | flags | reserved, 0 |
| 8 | seq | request id, echoed by the reply |
| 12 | length | payload bytes that follow |

The MAC is `BLAKE2b(key || BLAKE2b(header || payload))` truncated to 16 bytes:
the outer hash binds the key and the inner one fixes the message length, so it
is not a plain secret-prefix MAC.

| Step | Message | Key |
|---|---|---|
| 1 | client `HELLO{share}` | none - there is no key yet |
| 2 | server `CHALLENGE{nonce}` | none |
| 3 | client `AUTH{proof}` | `H` |
| 4 | server `OK` / `DENY{reason}` | `OK` uses `H`; `DENY` is bare |
| 5 | requests and replies | the session key |

Two request families run outside that handshake. `LOGON` and `LOGONPROOF` are
their own conversation on a fresh connection (the client sends the account
name, the server answers `CHALLENGE{salt, nonce}`, the client answers with a
proof over the password verifier), so neither carries a MAC; the `OK` that
ends it is keyed with the verifier both sides now hold, which proves the server
knows it too. `VIEW` stays unkeyed (§ below).

`H = BLAKE2b(secret)` is what the server stores; the secret never crosses the
wire. `proof = BLAKE2b(H || nonce)`, and the session key is
`BLAKE2b(H || nonce || "MVSH-K")`, fresh per connection, so a recorded
exchange cannot be replayed against a later one. `DENY` carries no MAC because
the peer may not have chosen a share yet: it is a refusal, not state a client
can act on. A MAC mismatch drops the connection.

`VIEW` is the one request answered without a session, because a browse list of
names is disclosure and not access. Everything that touches a file needs one.

A job larger than one message is streamed: `PRINT{offset, total, name, bytes}`
carries a chunk, the server answers one byte `0` while more is wanted, and the
reply to the last chunk is the four-byte job id. `WRITE{offset, length, name,
bytes}` streams the same way into an exported folder.


**What a share is not:** it is not SMB, it is not NFS, and it will not talk to a
machine that speaks either. It is one protocol, documented, with one
implementation, that two Mellivora machines use to share a folder
([D5](../ROADMAP.md#architectural-decisions)).

## 5. RPC

Remote procedure call is how the management surface and third-party services
talk across a machine boundary.

| Piece | Contract |
|---|---|
| Interface | A UUID and a major.minor version; a call is dispatched by `(uuid, version, opnum)` |
| Transport | A port ([IPC.md](IPC.md#2-ports)) locally, the share protocol's connection remotely; the stub does not know which |
| Binding | A registration under `\RpcControl`, so a client looks up the interface rather than a port number |
| Marshalling | Fixed layout, little-endian, 4-byte aligned ([ABI.md](ABI.md#3-types)); an interface's stub is generated from its declaration, and the generator's output is checked in |
| Versioning | Adding an opnum is additive; changing one requires a new major version, and both may be served at once |
| Limits | 64 KiB inline, a section above that ([IPC.md](IPC.md#2-ports)), 32 handles per call |
| Security | The server sees the client's token through the transport's authentication; impersonation levels (`identify`, `impersonate`, `delegate`) are explicit per call |
| Failure | A transport failure is a distinct status from a server refusal, so a retry decision is possible |

### 5.1 The call as built

A call is a request frame and a reply frame, carried inside a share message
(`type = RPC`) after the session handshake. The stub never sees a socket: the
same two frames go over the share connection, and the dispatch does not know
which transport delivered them.

Request, 28 bytes then the body:

| Offset | Field | Meaning |
|---|---|---|
| 0 | uuid | 16 bytes, NUL-padded |
| 16 | major | 1 |
| 17 | minor | 1 |
| 18 | opnum | 2, little-endian |
| 20 | impersonation | 0 `identify`, 1 `impersonate`, 2 `delegate` (refused with `NO_DELEGATE`); anything above 1 that is not `delegate` is `BAD_ARG` |
| 21 | pad | 3, zero |
| 24 | bodylen | 4 |
| 28 | body | `bodylen` bytes |

Reply, 8 bytes then the body: `status` (4) and `bodylen` (4). The status field
is what separates "the wire was down" (no reply at all) from "the server said
no" (a reply with a non-zero status), which is the decision a caller needs to
retry or not.

| Status | Name | Meaning |
|---|---|---|
| 0 | `OK` | The body is the answer |
| 1 | `UNKNOWN_IF` | No interface with that uuid |
| 2 | `BAD_VERSION` | Major differs, or the minor asked for is newer than served |
| 3 | `UNKNOWN_OPNUM` | The interface has no such operation |
| 4 | `BAD_ARG` | The body does not match the operation |
| 5 | `ACCESS_DENIED` | The session may not call this |
| 6 | `NOT_AUTHENTICATED` | A call arrived before the handshake finished |
| 7 | `NO_DELEGATE` | `delegate` was asked for and cannot be honoured |
| 8 | `NOT_FOUND` | The named value or record does not exist |
| 9 | `TOO_BIG` | The body exceeds one message |

Versioning is enforced as the contract asks: the major must match exactly and
a minor newer than the one served is refused, because a caller that expects an
opnum this build does not have must not be dispatched against the older table.
Several versions of one interface are not served at once yet - the table holds
one entry per uuid.

The binding is written at boot: for each registered interface,
`\RpcControl` holds `<uuid> = "<major>.<minor>"`, so a client looks the
version up in Sett instead of assuming it. `net iface` prints the same table
from memory.

| Interface | Version | Operations |
|---|---|---|
| `mv.rpc/config` | 1.0 | 0 `GET key` -> value, 1 `SET key value` -> empty |
| `mv.rpc/eventlog` | 1.0 | 0 `COUNT` -> count, 1 `READ index` -> `level, tick, source[16], text[80]` |

`net cfg HOST SHARE get\|set KEY [VALUE] [SECRET]` is the client for the first
and `net log HOST SHARE [SECRET]` for the second; both read the far machine's
records as `LEVEL SOURCE: text`.

**What this is not yet.** The body limit is 1 200 bytes rather than the 64 KiB
inline the contract asks for, there is no stub generator (the two interfaces
are hand-written and their layouts are in this table), and no section is used
above the limit. Impersonation is parsed and bounded - `delegate` is refused
with its own status rather than being quietly treated as `impersonate` - but
it carries no authority: the transport authenticates a session against a share
secret, not a user, so `identify` and `impersonate` change nothing about what
the caller may do. Everything that needs a user's rights is refused, which is
the honest state rather than a level that pretends to be one.

## 6. Remote administration

The parity target says "administer a workstation remotely is in scope":

| Capability | Mechanism |
|---|---|
| Service control | The service objects over RPC ([SERVICES.md](SERVICES.md#5-management)) |
| Configuration | Sett keys over RPC ([CONFIG.md](CONFIG.md)) |
| Event log | Read and filter records ([RELIABILITY.md](RELIABILITY.md#8-tracing)) |
| Process and performance | Enumerate, sample counters, stop a process with `CAP_PROC_CONTROL` |
| Remote console | A terminal session over the share protocol's connection, authenticated, with `SE_SESSION_CROSS` |
| Power | `S5`, restart, with `SE_SHUTDOWN` ([PLATFORM.md](PLATFORM.md#6-power)) |

Every one of these is refused without an authenticated identity that holds the
privilege, and every refusal is an audit record. There is no unauthenticated
administrative listener, and no default password.

As built, the mechanism column is a mix of done and not:

| Capability | State |
|---|---|
| Service control | **Done**, over the share's `SVC` request rather than over RPC: the SCM's records are reached by name, so the call is a command and not an interface yet |
| Configuration | **Done** over RPC (`mv.rpc/config`): read and write a Sett value on the other machine, with `NOT_FOUND` for a value that is not there |
| Event log | **Done** over RPC (`mv.rpc/eventlog`): the record count and then one record at a time, so the client asks how many the far machine keeps instead of assuming |
| Process and performance | Not built. There is no process-enumerating interface and no counter interface |
| Remote console | **Built, with the limits §6.1 names.** `net console HOST SHARE SECRET [COMMAND]` sends a command line over the authenticated session; this machine's shell executes it in its own context with the output captured rather than painted, and the answer comes back. With no command it is an interactive session until an empty line or `exit` |
| Power | Not built over the network. `S5` and restart exist locally in `sc`/`shutdown` |
| Privileges | Not enforced per capability. The only check on a management call today is that the session is authenticated at all (§5.1), which is why the table above says *done* and not *authorised* |
| Audit | Every management call that changes something writes a record to the boot log, and every refusal does too |

### 6.1 The remote console as built

The transport was never the hard part. The hard part is *which context runs the
command*: the share server lives in the network receive path, which is no place
to execute a shell command, and the shell is the only thing on this machine that
can execute one. So the server runs nothing. It queues the line and answers
later:

1. The server receives `CONSOLE{command}` on an authenticated session and
   records the command, the session and the sequence number.
2. The shell, idle at an empty prompt, takes the command, starts capturing
   console output, and runs the line through its own dispatch.
3. The shell reaches its prompt again, which is the one place both a builtin and
   a program arrive when a command is done: capture stops and the output becomes
   the reply.
4. The idle loop sends the reply on the session that asked.

| Property | Behaviour |
|---|---|
| Authentication | The session's, like every other request. The secret is not optional in `net console`: a console reachable without one is what section 6 says must not exist |
| Concurrency | One command at a time, and only while the local console is idle at an empty line. A second request is answered at once with `busy` rather than queued behind a command the client cannot see |
| Output | Captured, not painted: the local console is somebody else's. What a program the command launches prints is caught too, because the capture sits in `vga_putchar` |
| Size | 1 200 bytes back in one message; a longer answer is truncated and the client says so |
| Failure | A transport failure is reported as `remote: the connection failed`, a refusal by name says so, and nothing is retried silently |

**What it is not.** Not an independent session: the command runs in the
console's own context, so it sees the console's current directory and a `cd`
from a remote client moves the local shell too. There is no pty, no echo of what
the client types, and no window size. And there is no `SE_SESSION_CROSS`: the
shell runs as SYSTEM, so a client holding the share secret can do what SYSTEM
can. That is the gap between this and section 6's "authenticated identity that
holds the privilege", and it is the next thing to close.

One address is refused by name rather than attempted: `net console` against an
address on this machine. The console is what would execute the command and what
would be waiting for the answer, so a loopback console would deadlock until the
client's deadline.

## 7. Firewall

| Rule | Contract |
|---|---|
| Default | Inbound deny, outbound allow |
| Match | Application image, remote address range, protocol, port |
| Application rule | A program's package declares the ports it wants ([ECOSYSTEM.md](ECOSYSTEM.md)); the user approves once, the same way capabilities are approved |
| State | Established connections are allowed back by state, not by a rule |
| Loopback | Always allowed, and never reported as an inbound connection |
| Enforcement point | The stack's accept path for `TCP` and the receive path for `UDP`; a driver cannot bypass it because there is no second receive path |
| Audit | A blocked inbound connection is counted; a block caused by a missing rule is reported once per remote address |

### 7.1 As built

A rule is one Sett value under `\Registry\Machine\System\Firewall`, named
`Rule0`, `Rule1`, ... and holding three words:

| Form | Meaning |
|---|---|
| `tcp 4450 any` | the share server, from anywhere |
| `tcp 8000 10.0.0.0/24` | a local web server, from one subnet |
| `udp 53 any` | a name server |
| `any 0 any` | everything, which is what turning it off expresses too |

`Enabled` holds `0` when the firewall is off. The live table is built from
those values at boot and rewritten after every change, so the store and the
table cannot disagree, and a rule is written back as normalized text - the
same line `net fw list` prints - so what reloads is what was accepted.

| Rule | Tree today |
|---|---|
| Default | **Yes.** On at boot, inbound denied, outbound untouched and uncounted |
| Match | **Partly.** Protocol, local port and remote address with a prefix length. There is no application-image match, and no per-package port declaration with an approval prompt: a rule is written with `net fw add` or into the Sett, by whoever may write that key |
| State | **Yes, for UDP.** A reply is admitted when it matches a flow this machine sent - remote address and port and our own port, the last 16 kept - and a flow sent to a broadcast accepts a reply from any address, which is how DHCP is answered. TCP needs no table: a segment for an established connection is matched to its socket before the listen path |
| Loopback | **Yes.** Admitted before the rules are consulted, and not counted |
| Enforcement point | **Yes.** `tcp_handle` decides in the listen state, before a child socket is allocated; `udp_handle` decides before any handler - DHCP, DNS or a socket - sees the datagram |
| Audit | **Yes.** `fw_blocked` counts, `net fw` prints it, and the first block for a remote address writes `fw: blocked inbound from A.B.C.D` to the boot log. Up to 8 addresses are remembered; after that the count continues without a new record |
| Reply to a block | **Nothing.** The segment is dropped, not answered with a reset, so a scan learns nothing about which ports exist. A client therefore waits out its connect timeout rather than failing at once |

`net fw` reports the state and the counters, `net fw list` prints the table,
`net fw add <tcp\|udp\|any> <port> [remote[/bits]]` appends a rule (16 is the
limit), `net fw del <n>` removes one and compacts both the table and the store,
`net fw on` / `net fw off` write `Enabled`, and `net fw reload` re-reads the
store so a rule written with `sett set` takes effect without a restart. A
stored value that does not parse is refused rather than honoured: the reload
counts it, and a boot that finds one writes `fw: a stored rule was refused:
Rule2` to the log, because a rule that was typed wrong must not look like one
that is being enforced.

**What to expect the first time.** The firewall is on and there is no rule for
4450, so `net view` from another machine is refused until `net fw add tcp 4450`
runs on the machine that serves the share. That is the contract working, and
`tests/test_two_vms.sh` asserts exactly that sequence - refused, counted,
admitted, refused again once the rule is deleted.

## 8. Performance targets

| Operation | Target |
|---|---|
| TCP throughput on e1000 under QEMU | > 100 Mbit/s |
| Round-trip latency on loopback | < 100 µs |
| Concurrent sockets | 4 096 per process, 65 536 system-wide |
| Share: sequential read | > 40 MB/s on a gigabit link |
| DHCP + DNS to first successful `connect` | < 1 s |

## 9. Conformance

M7 does not close until:

1. `net_test` runs the stack's suites: ARP, ICMP, TCP state transitions, UDP,
   loss and reordering at 5 %, and reassembly of a fragmented datagram.
2. `packet_fuzz` feeds 1 000 000 malformed frames and the stack neither panics
   nor loses a valid connection.
3. `share_test` mounts a share from a second QEMU instance, copies 1 000 files
   both ways, and the bytes and attributes match; a lease is granted and broken.
4. `rpc_test` calls every management interface locally and over the share
   transport, refuses an unauthenticated client, and honours impersonation
   levels.
5. The firewall blocks an inbound connection without a rule, allows it with one,
   and audits the difference. The seven firewall steps in
   `tests/test_two_vms.sh` - five `net fw` commands and the two `net view`
   attempts they decide - are this item: machine B is refused, A's counter
   records it, `net fw add tcp 4450` admits the next attempt, and deleting the
   rule refuses it again.
6. A share mount survives the server's connection dropping and recovers its
   open handles.

## 10. As built (the gap)

| Contract | Tree today |
|---|---|
| Loopback | A real interface: a datagram addressed to 127.0.0.0/8 is queued and handed back to the receive path, and it carries the loopback address as its source, so the reply stays local. It works with no NIC present. |
| Name resolution | A literal dotted quad is answered by the resolver itself, so an address needs no server to resolve it |
| Socket as an object | Sockets are numbers in a table, with `poll` and non-blocking connect |
| Blocking calls | `connect` and `accept` wait without freezing the console: a task that is waiting parks itself, the shell keeps reading commands, and the scheduler hands the task back its frame when it is dispatched again. Two programs talk over the loopback interface at the same time (`httpd 8000 &` then `curl http://127.0.0.1:8000/...`) |
| Overlapped I/O via completion ports | None |
| Shares | A directory exported under a name, served from the kernel on TCP 4450: `net share [/rw] NAME PATH [SECRET]` exports (read-only unless `/rw`), `net view HOST` browses, `net use HOST SHARE [SECRET]` lists it, `net get HOST SHARE FILE [SECRET]` reads one file, `net put HOST SHARE FILE [SECRET]` streams one back, and `net use HOST SHARE /MOUNT [SECRET]` registers a `VFS_FS_NET` mount so `dir`, `cat`, `cd` and `>` work on a share like any other volume (the shell's redirect routes through the VFS for any mount that is not the volume itself). Framing, the handshake and the MAC are in [§4.1](#41-the-wire-format-as-built). Rename, delete, leases and reconnect of a broken session are not built |
| Network logon | `net logon HOST USER PASSWORD` proves the password to the other machine's account store without sending it: the server answers with the account's salt and a fresh nonce, the client answers with a proof over the Argon2 verifier, and the `OK` is keyed with that verifier. The console's logon is the same verifier, so an account is one account on both sides |
| Two machines | `tests/test_two_vms.sh` (`make check-twovm`) joins two QEMU guests with a TCP socket netdev pair (listen then connect), each with its own `-snapshot` overlay and MAC, and drives both over their serial consoles: one exports a read-write and a read-only folder, the other mounts it, reads through it, writes through it, is refused by the read-only export and by a wrong secret, logs on, starts a service on the first and queues a print job there, and changes a Sett value on it through RPC and then reads that value out of the first machine's own registry. It proves the wire; the loopback cases in `tests/test_runtime.sh` keep the protocol's corners cheap. The same plan is the firewall's gate (§7.1), because a blocked connection needs two addresses that are not each other's. Item 3 above is still open: 1 000 files both ways, leases and reconnect are not exercised |
| Remote administration | `net svc HOST SHARE SERVICE start\|stop\|restart [SECRET]` controls a service on the other machine through its SCM and reports the resulting state, and `net print HOST SHARE FILE [SECRET]` sends a document to the other machine's spooler (jobs up to 4 KiB, streamed in messages, stored in the root as `Spool-<name>` because HBFS has no directory for them) |
| Remote console | `net console HOST SHARE SECRET [COMMAND]` sends a command line over the authenticated session and answers with its output; with no command it is an interactive session until an empty line or `exit` (§6.1). The shell executes it in its own context with output captured, so the local console is untouched and `cd` is shared with it. No pty, and no per-user privilege |
| RPC | Two interfaces are served, `mv.rpc/config` and `mv.rpc/eventlog`, and both are reachable as `net cfg` and `net log` (§5.1). A call is dispatched by `(uuid, major.minor, opnum)` with the nine statuses above, and the bindings are written into `\RpcControl` at boot so a client reads the version instead of assuming it. There is no stub generator, the body limit is 1 200 bytes, and impersonation carries no authority - the session authenticates a secret, not a user. A call on a connection that has not finished the handshake is dropped, not answered |
| Firewall | On at boot: inbound denied unless a rule admits it or the packet answers a flow this machine sent. Rules are Sett values under `\Registry\Machine\System\Firewall` and are edited with `net fw` (§7.1). Enforced in the TCP listen path and the UDP receive path. No application-image match, no per-package port declaration, and a blocked packet is dropped rather than reset. `make check-twovm` proves refusal, count, admission and refusal again |
| Diagnostics | `net sess` dumps the server's session and socket tables, because both ends of the protocol run in this kernel and "the server refused the session" is answered with the tables rather than a guess |
| IPv6, MSI-X-driven NICs | Not in scope / not used |

The TCP/IP stack, DHCP, DNS and the socket API are real, tested and stay; the
sharing, RPC and firewall layers are new.

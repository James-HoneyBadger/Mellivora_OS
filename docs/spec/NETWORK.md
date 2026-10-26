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
   and audits the difference.
6. A share mount survives the server's connection dropping and recovers its
   open handles.

## 10. As built (the gap)

| Contract | Tree today |
|---|---|
| Socket as an object | Sockets are numbers in a table, with `poll` and non-blocking connect |
| Overlapped I/O via completion ports | None |
| Shares | None — the reason this document exists |
| RPC | None |
| Remote administration | None |
| Firewall | None |
| IPv6, MSI-X-driven NICs | Not in scope / not used |

The TCP/IP stack, DHCP, DNS and the socket API are real, tested and stay; the
sharing, RPC and firewall layers are new.

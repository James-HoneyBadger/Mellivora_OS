# Mellivora OS — Networking Guide

Mellivora OS includes a complete TCP/IP networking stack with a RTL8139 NIC driver,
protocol support for Ethernet, ARP, IP, ICMP, UDP, and TCP, plus user-space programs
for web browsing, file transfer, email, and more.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Network Architecture](#network-architecture)
3. [Shell Networking Commands](#shell-networking-commands)
4. [Networking Programs](#networking-programs)
5. [Programming with Sockets](#programming-with-sockets)
6. [QEMU Networking Setup](#qemu-networking-setup)
7. [Troubleshooting](#troubleshooting)

---

## Getting Started

### Quick Start (QEMU)

Boot Mellivora in QEMU with networking enabled (the default Makefile already sets this
up). Then run:

```text
Lair:/> dhcp
Requesting IP via DHCP...
DHCP complete: 10.0.2.15

Lair:/> ping 10.0.2.2
PING 10.0.2.2:
  Reply: time=1ms
  Reply: time=1ms
  Reply: time=1ms
  Reply: time=1ms
--- Ping statistics: 4/4 packets received
```

You now have a working network connection. Try fetching a web page:

```text
Lair:/> forager example.com
Connecting to example.com...
<!doctype html>
<html>
...
```

### Network Defaults (QEMU User-Mode)

| Setting | Value |
| --- | --- |
| Guest IP (after DHCP) | `10.0.2.15` |
| Subnet mask | `255.255.255.0` |
| Default gateway | `10.0.2.2` |
| DNS server | `10.0.2.3` |
| NIC emulated | RTL8139 |

---

## Network Architecture

The networking stack is implemented entirely in the kernel (`kernel/net.inc`) and
exposes functionality through syscalls and shell commands.

### Protocol Layers

```text
┌──────────────────────────────────────────┐
│  User Programs (ping, http, ftp, ...)    │  User space
├──────────────────────────────────────────┤
│  net.inc Library (programs/lib/net.inc)  │  User library
├──────────────────────────────────────────┤
│  Syscall Interface (INT 0x80)            │  Kernel boundary
├──────────────────────────────────────────┤
│  Socket API (8 sockets × 128 bytes)     │  Kernel
│  TCP (full state machine, 11 states)     │
│  UDP                                     │
│  ICMP (echo request/reply)               │
│  IP (send/receive, checksum)             │
│  ARP (16-entry cache, request/reply)     │
│  Ethernet (frame TX/RX)                  │
│  RTL8139 NIC Driver (PCI, IRQ, DMA)     │  Hardware
└──────────────────────────────────────────┘
```

### Key Limits

| Resource | Limit |
| --- | --- |
| Simultaneous sockets | 32 |
| Socket structure size | 128 bytes per socket |
| Socket recv buffer | 8,192 bytes (2 pages) per socket |
| Socket send buffer | 8,192 bytes (2 pages) per TCP socket |
| ARP cache entries | 16 |
| DNS cache entries | 32 |
| DHCP buffer | 600 bytes |
| TX buffer | 1,536 bytes |
| RX buffer | 1,536 bytes |

---

## Shell Networking Commands

These commands are built into the HB Lair shell and available immediately.

### net — Network Status

Displays full network status including NIC state, addresses, and configuration.

```text
Lair:/> net
=== Network Status ===
NIC: RTL8139 (Up)
PCI: 00:03.0  IRQ: 11
I/O: 0xC000
MAC: 52:54:00:12:34:56
IP:  10.0.2.15
Mask: 255.255.255.0
GW:  10.0.2.2
DNS: 10.0.2.3
```

### dhcp — Request IP Address

Sends a DHCP discover/request sequence to obtain an IP address, subnet mask, gateway,
and DNS server automatically.

```text
Lair:/> dhcp
Requesting IP via DHCP...
DHCP complete: 10.0.2.15
```

If no NIC is detected or the request times out (10 seconds), an error message is shown.

### ping — Test Connectivity

Sends 4 ICMP echo requests to a host. Accepts either a dotted IP address or a hostname
(resolved via DNS).

```text
Lair:/> ping 10.0.2.2
PING 10.0.2.2:
  Reply: time=1ms
  Reply: time=1ms
  Reply: time=1ms
  Reply: time=1ms
--- Ping statistics: 4/4 packets received
```

### ifconfig — View/Set IP Address

With no arguments, displays the same output as `net`. With an IP argument, sets the
system IP address manually (useful if DHCP is unavailable).

```text
Lair:/> ifconfig 192.168.1.100
IP set to 192.168.1.100
```

### arp — View ARP Cache

Displays the current ARP table showing resolved IP-to-MAC address mappings.

```text
Lair:/> arp
ARP Cache:
  10.0.2.2    52:55:0A:00:02:02
  10.0.2.3    52:55:0A:00:02:03
```

### Sharing a folder — `net share`, `net view`, `net use`, `net put`, `net get`

A share is a directory exported to another Mellivora machine over the platform's own
protocol on TCP port 4450. It is not SMB and not NFS: two Mellivora machines speak it,
and nothing else does. An export is read-only unless `/rw` is given, because a share
that grants writes by accident is the failure that matters.

```text
Lair:/> net share /rw docs /docs s3cret
Shared: docs = /docs
Lair:/> net share
docs = /docs
Lair:/> net share -d docs
Share removed
```

The secret is optional. When one is set, the client must present it; the secret never
crosses the wire, and a wrong one drops the session rather than denying a single
request:

```text
Lair:/> net view 10.0.0.1
Shares on 10.0.0.1:4450
docs
Lair:/> net use 10.0.0.1 docs s3cret
Session on docs
  readme.txt
  todo.txt
```

`net use HOST SHARE /MOUNT` registers the share as a volume, so the ordinary file
commands work on it:

```text
Lair:/> net use 10.0.0.1 docs /mnt s3cret
Mounted /mnt from docs
Lair:/> dir /mnt
Lair:/> cat /mnt/readme.txt
```

Two commands move one file without mounting anything. `net get` reads a file from the
export and prints it; `net put` streams a local file into an export that was made
`/rw`, and prints how many bytes arrived:

```text
Lair:/> net put 10.0.0.1 docs /notes.txt s3cret
Wrote 42 bytes to the share
```

A read-only export answers the same command with
`net: the share refused the write (read-only?)`. Rename, delete, leases and reconnect
of a broken session are not implemented.

### Remote administration — `net logon`, `net svc`, `net print`, `net sess`

These use the same authenticated session as a share. `net logon` proves a password to
another machine's account store without sending it: the server returns the account's
salt and a fresh nonce, and the client answers with a proof over the Argon2 verifier.

```text
Lair:/> net logon 10.0.0.1 alice secret1
Logged on as alice on 10.0.0.1
Lair:/> net logon 10.0.0.1 alice wrongpw
net: the server refused the logon
```

`net svc` drives the other machine's service manager and reports the state reached;
`net print` streams a document into the other machine's spooler, where `printq` shows
it; `net sess` dumps the server's own session and socket tables, which is how a
refused session is diagnosed from the serving side.

```text
Lair:/> net svc 10.0.0.1 docs pt start s3cret
Service pt is running
Lair:/> net print 10.0.0.1 docs /notes.txt s3cret
Queued job 1
```

### Configuration and the event log — `net cfg`, `net log`, `net iface`

These three are the RPC layer rather than a request of their own: a call carries
an interface id, a version and an operation, and the answer comes back inside
the same authenticated session. `net cfg` reads or writes a Sett value on the
other machine, `net log` reads its boot log, and `net iface` lists the
interfaces this machine serves, which is the thing to run when an answer is
`unknown interface`.

```text
Lair:/> net cfg 10.0.0.1 docs set \Registry\Machine\Demo\Host far-side s3cret
net: set \Registry\Machine\Demo\Host
Lair:/> net cfg 10.0.0.1 docs get \Registry\Machine\Demo\Host s3cret
\Registry\Machine\Demo\Host = far-side
Lair:/> net cfg 10.0.0.1 docs get \Registry\Machine\Demo\Absent s3cret
net: the server refused the call, status 8 (unknown interface, version, opnum or argument)
Lair:/> net log 10.0.0.1 docs s3cret
Event log: the last 20 records
I scm: boot and system services started
Lair:/> net iface
Interfaces served:
  mv.rpc/config  v1.0 ops 2
  mv.rpc/eventlog  v1.0 ops 2
```

A value is one word, because the shell has no quoting yet. Status `8` is
`NOT_FOUND`, and it is the difference between a setting that is blank and one
that does not exist. The management interfaces are not public: a call on a
connection that has not finished the handshake is dropped, and a wrong secret
reports `net: the server dropped the session (wrong secret?)`.

### The firewall — `net fw`

Inbound connections are denied until a rule admits them, and outbound is
allowed without a rule. Loopback is always exempt and is not counted, so
everything on this machine talks to itself as before; a reply to something this
machine sent is admitted by state, which is why DHCP and DNS keep working with
no rule at all.

That means a share is not reachable from another machine until the machine
serving it says so:

```text
Lair:/> net fw
Firewall: on
Rules: 0
Admitted: 0   Blocked: 1 (udp 0)
Lair:/> net fw add tcp 4450
fw: rule 0  tcp 4450 any
Lair:/> net view 10.0.0.1
Shares on 10.0.0.1:4450
docs
```

A rule is three words — protocol, port, and an optional remote address with a
prefix length:

```text
Lair:/> net fw add tcp 8000 10.0.0.0/24
fw: rule 2  tcp 8000 10.0.0.0/24
Lair:/> net fw list
Rules (index, then the line the Sett holds):
  0  tcp 4450 any
  1  udp 53 any
  2  tcp 8000 10.0.0.0/24
Lair:/> net fw del 1
fw: rule 1 removed
```

`net fw off` stops the check (the store remembers it); `net fw on` starts it
again. The rules live in `\Registry\Machine\System\Firewall` and are read at
boot, so editing them with `sett set` works too — `net fw reload` applies the
edit without a restart, and the line `net fw list` prints is exactly the line
that was accepted. A value that does not parse is refused and counted rather
than quietly ignored.

A blocked connection is counted and, the first time a given address is seen,
written to the boot log as `fw: blocked inbound from 10.0.0.2`. The packet is
dropped rather than answered, so the other machine waits out its connect
timeout and reports `net: cannot reach the share service`.

---

## Networking Programs

All networking programs live in `/bin` and are accessible from any directory via PATH.

### ping — ICMP Ping Utility

```text
Usage: ping <host>
```

Sends 4 ICMP echo requests to the specified host. Supports both IP addresses and
hostnames (automatically resolved via DNS). Displays round-trip time for each reply.

```text
Lair:/> ping example.com
PING 93.184.216.34:
  Reply: time=15ms
  Reply: time=14ms
  Reply: time=15ms
  Reply: time=14ms
--- Ping statistics: 4/4 packets received
```

### forager — Web Browser

```text
Usage: forager <url>
```

Fetches a web page via HTTP/1.0 GET and prints the response body. The URL can include
an optional `http://` prefix, port number, and path.

```text
Lair:/> forager example.com
Connecting to example.com...
<!doctype html>
<html>
<head>
    <title>Example Domain</title>
...
```

URL formats accepted:

- `forager example.com` — GET `/` on port 80
- `forager example.com/page.html` — GET `/page.html` on port 80
- `forager example.com:8080/api` — GET `/api` on port 8080
- `forager http://example.com/path` — `http://` prefix is stripped

### telnet — Interactive Telnet Client

```text
Usage: telnet <host> [port]
```

Opens an interactive TCP connection to a remote host. Default port is 23. Characters
you type are sent to the server; responses are displayed in real time. Basic IAC
(telnet control sequence) filtering is applied. Press **Ctrl+C** to disconnect.

```text
Lair:/> telnet towel.blinkenlights.nl
Connecting to towel.blinkenlights.nl:23...
Connected!
```

### gopher — Gopher Protocol Client

```text
Usage: gopher <host> [path] [port]
```

Browses Gopher servers (RFC 1436). Connects to the specified host on port 70 (default),
sends the selector path, and renders the response with type prefixes:

| Prefix | Meaning |
| --- | --- |
| `[TXT]` | Text file (type 0) |
| `[DIR]` | Directory/menu (type 1) |
| `[ERR]` | Error (type 3) |
| *(none)* | Info line (type i) |

```text
Lair:/> gopher gopher.floodgap.com
[DIR] Welcome to Floodgap Gopher
[DIR] Gopher Project
[TXT] About this server
      This is an info line
```

### ftp — FTP Client

```text
Usage: ftp <host> [port]
```

Interactive FTP client using passive mode (PASV). Automatically connects and logs in
as `anonymous`. Supported commands:

| Command | Description |
| --- | --- |
| `user <name>` | Set username |
| `pass <password>` | Set password |
| `ls` | List directory contents (PASV + LIST) |
| `cd <dir>` | Change remote directory (CWD) |
| `get <file>` | Download and display file (PASV + RETR) |
| `pwd` | Print remote working directory |
| `quit` | Disconnect and exit |

```text
Lair:/> ftp ftp.example.com
Connecting to ftp.example.com:21...
220 Welcome to FTP server
230 Login successful
ftp> ls
drwxr-xr-x  2  pub
-rw-r--r--  1  readme.txt
ftp> get readme.txt
Hello from the FTP server!
ftp> quit
```

### mail — Email Client (SMTP/POP3)

```text
Usage: mail <server>
```

Combined email client supporting both sending (SMTP, port 25) and receiving (POP3,
port 110). Interactive command interface:

| Command | Description |
| --- | --- |
| `compose` | Compose and send an email via SMTP |
| `inbox` | List messages via POP3 (prompts for user/pass) |
| `read N` | Read message number N via POP3 |
| `delete N` | Delete message number N via POP3 |
| `quit` | Exit |

**Sending Mail (compose):**

```text
Lair:/> mail mail.example.com
mail> compose
From: user@example.com
To: friend@example.com
Subject: Hello from Mellivora!
Body (blank line to end):
This is a test email sent from Mellivora OS.

Sending via SMTP...
Message sent!
```

**Reading Mail (inbox/read):**

```text
mail> inbox
User: myuser
Pass: mypass
1  1234 bytes
2  567 bytes
mail> read 1
From: sender@example.com
Subject: Welcome
...
```

### news — Usenet/NNTP Client

```text
Usage: news <server>
```

Usenet newsgroup reader using the NNTP protocol (port 119). Interactive command
interface:

| Command | Description |
| --- | --- |
| `list` | List available newsgroups |
| `group <name>` | Select a newsgroup |
| `headers` | Show article headers in current group (XOVER) |
| `read N` | Read article number N |
| `post` | Post a new article (prompts for details) |
| `quit` | Disconnect and exit |

```text
Lair:/> news news.example.com
Connecting to news.example.com:119...
200 Welcome to NNTP
news> list
comp.os.mellivora  1-42
alt.test           1-100
news> group comp.os.mellivora
211 42 articles
news> read 1
From: user@example.com
Subject: First post!
...
news> quit
```

---

## Programming with Sockets

### Networking Syscalls

Ten syscalls provide the kernel networking interface:

| # | Name | EBX | ECX | EDX | Returns (EAX) |
| --- | --- | --- | --- | --- | --- |
| 39 | SOCKET | type (1=TCP, 2=UDP) | — | — | socket fd or -1 |
| 40 | CONNECT | socket fd | IP address | port | 0 or -1 |
| 41 | SEND | socket fd | buffer ptr | length | bytes sent or -1 |
| 42 | RECV | socket fd | buffer ptr | max length | bytes received, 0, or -1 |
| 43 | BIND | socket fd | port | — | 0 or -1 |
| 44 | LISTEN | socket fd | — | — | 0 or -1 |
| 45 | ACCEPT | socket fd | — | — | new fd or -1 |
| 46 | DNS | hostname ptr | — | — | IP address or 0 |
| 47 | SOCKCLOSE | socket fd | — | — | 0 |
| 48 | PING | IP address | — | — | RTT ticks or -1 |

### The net.inc User Library

Include `programs/lib/net.inc` for convenient wrappers around the raw syscalls:

```nasm
%include "syscalls.inc"
%include "lib/net.inc"
```

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `net_socket` | EAX=type (1=TCP, 2=UDP) | EAX=fd (-1 error) | Create a socket |
| `net_connect` | EAX=fd, EBX=IP, ECX=port | EAX=0/-1 | Connect to remote host |
| `net_send` | EAX=fd, EBX=buffer, ECX=length | EAX=bytes sent | Send raw data |
| `net_recv` | EAX=fd, EBX=buffer, ECX=max | EAX=bytes (0=none, -1=closed) | Receive data |
| `net_close` | EAX=fd | — | Close socket |
| `net_dns` | ESI=hostname | EAX=IP (0=fail) | Resolve hostname |
| `net_ping` | EAX=IP | EAX=RTT ticks (-1=timeout) | ICMP echo request |
| `net_bind` | EAX=fd, EBX=port | EAX=0/-1 | Bind to local port |
| `net_listen` | EAX=fd | EAX=0/-1 | Start listening |
| `net_accept` | EAX=fd | EAX=new fd (-1=timeout) | Accept connection |
| `net_send_line` | EAX=fd, ESI=string | — | Send string + CRLF |
| `net_recv_line` | EAX=fd, EDI=buffer, ECX=max | EAX=bytes, EDI filled | Receive line |
| `net_parse_ip` | ESI=dotted IP string | EAX=IP (0=error) | Parse "1.2.3.4" to binary |

### Example: Simple HTTP Fetch

```nasm
%include "syscalls.inc"
%include "lib/io.inc"
%include "lib/net.inc"

start:
        ; Resolve hostname
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        ; Create TCP socket
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .sock_fail
        mov [sockfd], eax

        ; Connect to port 80
        mov eax, [sockfd]
        mov ebx, [server_ip]
        mov ecx, 80
        call net_connect
        cmp eax, -1
        je .conn_fail

        ; Send HTTP request
        mov eax, [sockfd]
        mov esi, http_request
        call net_send_line

        ; Send blank line (end of headers)
        mov eax, [sockfd]
        mov esi, empty_line
        call net_send_line

        ; Receive and print response
.recv_loop:
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, 1024
        call net_recv
        cmp eax, 0
        jle .done

        ; Null-terminate and print
        mov byte [recv_buf + eax], 0
        mov esi, recv_buf
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        jmp .recv_loop

.done:
        mov eax, [sockfd]
        call net_close
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.dns_fail:
        mov esi, msg_dns_err
        jmp .print_exit
.sock_fail:
        mov esi, msg_sock_err
        jmp .print_exit
.conn_fail:
        mov esi, msg_conn_err
.print_exit:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

hostname:       db "example.com", 0
http_request:   db "GET / HTTP/1.0", 0
empty_line:     db "", 0
msg_dns_err:    db "DNS resolution failed", 10, 0
msg_sock_err:   db "Failed to create socket", 10, 0
msg_conn_err:   db "Connection failed", 10, 0

section .bss
server_ip:      resd 1
sockfd:         resd 1
recv_buf:       resb 1025
```

### Example: TCP Echo Server

```nasm
%include "syscalls.inc"
%include "lib/io.inc"
%include "lib/net.inc"

start:
        ; Create listening socket on port 7
        mov eax, NET_TCP
        call net_socket
        mov [listen_fd], eax

        mov eax, [listen_fd]
        mov ebx, 7             ; Echo port
        call net_bind

        mov eax, [listen_fd]
        call net_listen

.accept_loop:
        mov eax, [listen_fd]
        call net_accept
        cmp eax, -1
        je .accept_loop
        mov [client_fd], eax

        ; Echo received data back
.echo_loop:
        mov eax, [client_fd]
        mov ebx, echo_buf
        mov ecx, 128
        call net_recv
        cmp eax, 0
        jle .close_client

        ; Send it back
        mov ecx, eax
        mov eax, [client_fd]
        mov ebx, echo_buf
        call net_send
        jmp .echo_loop

.close_client:
        mov eax, [client_fd]
        call net_close
        jmp .accept_loop

section .bss
listen_fd:      resd 1
client_fd:      resd 1
echo_buf:       resb 128
```

---

## QEMU Networking Setup

### Default Configuration

The Makefile runs QEMU with user-mode networking and an emulated RTL8139 NIC:

```bash
qemu-system-i386 ... -netdev user,id=net0 -device rtl8139,netdev=net0
```

This provides NAT networking where the guest can access the host network and internet
through QEMU's built-in router. No host configuration is required.

### Port Forwarding

To make a server running inside Mellivora accessible from the host, add port forwarding:

```bash
qemu-system-i386 ... \
    -netdev user,id=net0,hostfwd=tcp::8080-:80 \
    -device rtl8139,netdev=net0
```

This forwards host port 8080 to guest port 80.

### Two Machines on One Wire

`make check-twovm` is the milestone gate that needs two computers: one guest exports a
folder, and the other mounts it, writes through it, logs on, prints and starts a
service. It joins two QEMU processes with a TCP socket netdev pair, which needs no
bridge, no TAP device and no root. Start the listening end first:

```bash
# machine A (the server): listens, and must be up before B starts
qemu-system-i386 ... -netdev socket,id=net0,listen=127.0.0.1:40000 \
    -device rtl8139,netdev=net0,mac=52:54:00:12:34:01 -snapshot ...

# machine B (the client)
qemu-system-i386 ... -netdev socket,id=net0,connect=127.0.0.1:40000 \
    -device rtl8139,netdev=net0,mac=52:54:00:12:34:02 -snapshot ...
```

A multicast group would be the obvious choice and is not: whether two guests on
one host hear each other depends on the host's multicast setup, and a run that
silently delivers nothing looks exactly like a stack bug.

Both machines boot the same disk image because each is given a `-snapshot` overlay;
without it the second guest's writes would appear in the first guest's filesystem.
They need distinct MACs — the profile's NIC otherwise defaults both to
52:54:00:12:34:56. Give each a static address with `ifconfig` (there is no DHCP server
on a private segment), then use the share commands from the section above:

```text
# machine A
Lair:/> ifconfig 10.0.0.1
IP set to 10.0.0.1
Lair:/> net share /rw docs /docs s3cret
Shared: docs = /docs

# machine B
Lair:/> ifconfig 10.0.0.2
IP set to 10.0.0.2
Lair:/> net use 10.0.0.1 docs /mnt s3cret
Mounted /mnt from docs
```

The driver behind the gate is `tools/qemu_twonode.py`; it takes a JSON plan of steps
and reports each one, so a hand-written two-machine experiment can be scripted the
same way.

### TAP Networking (Advanced)

For bridged networking where the guest appears as a real device on the LAN:

```bash
# Create TAP interface (requires root)
sudo ip tuntap add dev tap0 mode tap
sudo ip link set tap0 up
sudo brctl addif br0 tap0

# Run QEMU with TAP
qemu-system-i386 ... -netdev tap,id=net0,ifname=tap0 -device rtl8139,netdev=net0
```

---

## Troubleshooting

### "No NIC detected"

The RTL8139 NIC was not found during PCI bus scan. Ensure QEMU is started with
`-device rtl8139,netdev=net0`. Check with:

```text
Lair:/> net
=== Network Status ===
NIC: None
```

### "DHCP request timed out"

- Verify the NIC is detected (`net` command should show "Found" or "Up")
- QEMU user-mode networking includes a built-in DHCP server — if it fails, try
  setting the IP manually: `ifconfig 10.0.2.15`

### "DNS resolution failed"

- Ensure you have an IP address (`dhcp` or `ifconfig`)
- QEMU's DNS server is at 10.0.2.3 — this is set automatically by DHCP
- Try pinging the DNS server: `ping 10.0.2.3`

### "Connection refused" or "Connection timed out"

- QEMU user-mode networking allows outbound connections but blocks unsolicited inbound
- For servers, use port forwarding (see QEMU setup above)
- Some internet hosts may block or rate-limit connections

### Programs hang or no response

- The network stack is single-threaded — only one network operation runs at a time
- TCP has a built-in timeout; if a server is unresponsive, the program will eventually
  return an error
- Press **Ctrl+C** to abort any stuck program

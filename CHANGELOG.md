# Mellivora OS - Changelog

## v5.0.0 - The Hornet Release

### Kernel — Semaphores (`kernel/ipc.inc`)

- **`SYS_SEM_CREATE` (#88)**: Create a counting semaphore with an initial value. Returns a semaphore ID (0–7) or -1 on failure.
- **`SYS_SEM_WAIT` (#89)**: Decrement (P/wait) a semaphore. If the value is 0, yields up to 500 times before returning -1 (non-blocking style consistent with pipe I/O).
- **`SYS_SEM_POST` (#90)**: Increment (V/post) a semaphore.
- **`SYS_SEM_CLOSE` (#91)**: Release a semaphore slot.
- Up to 8 semaphores simultaneously. Semaphore state initialised in `ipc_init`.

### Kernel — Synchronous Wait (`kernel/sched.inc`)

- **`TASK_ZOMBIE` state (5)**: Tasks that call `sys_exit` now enter a zombie state instead of being freed immediately, preserving their slot until reaped by a parent.
- **`SYS_WAITPID` (#92)**: Wait for a task by PID. Yields up to 2000 times polling for task completion, then reaps the zombie and returns its exit code. Returns -1 if the PID is not found.

### Kernel — File Timestamps (`kernel/hbfs.inc`)

- **`SYS_GETMTIME` (#93)**: Query a file's timestamps. Returns packed RTC `DIRENT_MODIFIED` timestamp in EAX and `DIRENT_CREATED` timestamp in ECX.
- **`SYS_SETMTIME` (#94)**: Update a file's `DIRENT_MODIFIED` timestamp. Pass ECX=0 to use the current RTC time.

### Shell — Ctrl+R Reverse History Search (`kernel/shell.inc`)

- **Incremental reverse-i-search**: Press Ctrl+R at the shell prompt to start an interactive history search.
- Shows `(reverse-i-search)\`<query>': <match>` while typing.
- **Backspace** removes the last search character and re-searches.
- **Enter** copies the matched command to the input line and executes it.
- **Escape / Ctrl+C** cancels and returns to an empty prompt.

### Batch Scripts — Structured Control Flow (`kernel/util.inc`)

- **`if exist FILENAME cmd`**: Test for file existence. Works with the `not` modifier (`if not exist ...`).
- **`if "STR1"=="STR2" cmd`**: String comparison. Supports `not` modifier.
- **Block `if` / `else` / `endif`**: Multi-line conditional blocks. When an `if` line has no inline command the following lines form the block body; an optional `else` block is executed when the condition is false; `endif` closes the block. Nested `if`/`endif` pairs are handled correctly.
- **`for %%x in (a b c) do cmd`**: Single-line for loop. Iterates over a space-separated list; `%%x` in the command template is substituted with each value in turn.
- `else` and `endif` are now recognised as batch directives in `batch_run_loop`.

### New Programs

- **`strace`**: Syscall trace wrapper. Usage: `strace PROGRAM [args]`. Records the dmesg ring-buffer depth before running the target program and dumps all new log entries added during the run, providing a lightweight activity trace.
- **`patch`**: Apply unified-style diff output to a file. Usage: `patch FILE PATCHFILE`. The patch file is the output of the `diff` utility (`< ` = remove, `> ` = insert). Applies all hunks and reports how many could not be matched.

### Syscall Constants (`programs/syscalls.inc`)

- Added constants for all v5.0 syscalls: `SYS_SEM_CREATE` through `SYS_SETMTIME` (88–94).

---

## v4.0.0 - The Titan Release

### Kernel — Priority Scheduler (`kernel/sched.inc`)

- **Priority-based scheduling**: Replaced round-robin with 4-level priority scheduling (HIGH, NORMAL, LOW, IDLE). Higher priority tasks are selected first; equal priority tasks use round-robin for fairness.
- **64 tasks max**: Doubled from 32 to 64 concurrent tasks (`MAX_TASKS`).
- **Expanded TCB**: Task Control Block expanded from 32 to 64 bytes with new fields:
  - `TCB_NAME` (16 bytes): Human-readable task name
  - `TCB_SIG_PEND` / `TCB_SIG_MASK`: Signal pending/mask bitmasks
  - `TCB_PGID`: Process group ID
  - `TCB_EXIT_CODE`: Task exit code
- **TASK_STOPPED state**: New state (4) for signal-stopped tasks (SIGTSTP/Ctrl+Z).
- **`sys_proclist` expanded**: Output buffer expanded from 16 to 48 bytes, now includes priority, PGID, pending signals, exit code, and task name.

### Kernel — Signals (`kernel/sched.inc`)

- **POSIX-style signals**: 9 signal types implemented: SIGINT (2), SIGKILL (9), SIGUSR1 (10), SIGUSR2 (12), SIGALRM (14), SIGTERM (15), SIGCHLD (17), SIGTSTP (20), SIGCONT (25).
- **`SYS_SIGNAL` (#74)**: Send any signal to a task by PID. SIGKILL forcibly terminates, SIGTSTP stops, SIGCONT resumes, SIGINT/SIGTERM terminate.
- **`SYS_SIGMASK` (#77)**: Get/set/block/unblock signal mask. 4 operations: get, set, block (OR), unblock (AND NOT).
- **Signal masking**: Tasks can mask signals via bitmask; masked signals are queued as pending.

### Kernel — New Syscalls (`kernel/syscall.inc`)

- 8 new syscalls (72–79), bringing total to 80:
  - `SYS_SETPRIORITY` (#72): Set task priority by PID
  - `SYS_GETPRIORITY` (#73): Get task priority by PID
  - `SYS_SIGNAL` (#74): Send signal to task
  - `SYS_SETPGID` (#75): Set process group ID
  - `SYS_GETPGID` (#76): Get process group ID
  - `SYS_SIGMASK` (#77): Signal mask operations
  - `SYS_TASKNAME` (#78): Set task name string
  - `SYS_REALLOC` (#79): Reallocate memory block

### Kernel — Memory (`kernel/syscall.inc`)

- **`SYS_REALLOC` (#79)**: New syscall for reallocating memory — allocates new block, copies data, returns new pointer. Supports NULL pointer (fresh allocation).

### Shell — Enhanced Line Editing (`kernel/shell.inc`)

- **Ctrl+A**: Move cursor to beginning of line
- **Ctrl+E**: Move cursor to end of line
- **Ctrl+U**: Kill entire input line
- **Ctrl+W**: Delete previous word (skip spaces, then delete word)
- **Ctrl+L**: Clear screen and redraw prompt with current input
- **128 history entries**: Doubled from 64

### Shell — New Commands (`kernel/shell.inc`)

- **`ps`**: List all running tasks with PID, state, priority, and name
- **`jobs`**: List background jobs (alias for ps)
- **`kill <pid> [signal]`**: Send signal to task (default: SIGTERM)
- **`bg <pid>`**: Resume stopped task in background (sends SIGCONT)
- **`fg <pid>`**: Resume stopped task in foreground (sends SIGCONT)
- **`nice <priority> <command>`**: Run command at specified priority level (0=HIGH, 1=NORMAL, 2=LOW, 3=IDLE)
- **`export NAME=VALUE`**: Export environment variable (synonym for set)
- **`source <file>`** / **`. <file>`**: Execute batch script in current shell context

### Shell — Capacity Increases

- **32 environment variables**: Doubled from 16
- **128 history entries**: Doubled from 64

### Programs — Userland Updates (`programs/syscalls.inc`)

- All 8 new v4.0 syscall numbers added to shared header
- Priority level constants (PRIO_HIGH through PRIO_IDLE)
- Signal number constants (SIGINT through SIGCONT)

### Version & Branding

- Version bumped to v4.0.0 "The Titan Release"
- Shell version bumped to HB Lair v3.0
- Updated `ver` output: 80 syscalls, priority scheduler, signal info, enhanced shell shortcuts
- Updated `help` text with all new commands and shortcuts

---

## v3.0.1 - Bug Fixes: Sleep Regression, Neofetch, Pipe Race, Permissions UI

### Kernel — Scheduler (`kernel/sched.inc`)

- **`sys_sleep` HLT fallback**: In v3.0.0 the `SYS_SLEEP` rewrite made sleep a no-op when `task_count == 0` (normal shell / single-program context). This broke 29 programs that use `SYS_SLEEP` for timing, animation frame-rates, and CPU-yield poll loops (clock, galaga, doomfire, matrix, rain, ntpd, serial, etc.). Fixed: when no scheduler tasks are running, `sys_sleep` falls back to the original HLT-based busy-wait loop so timing is preserved.

### Kernel — IPC (`kernel/ipc.inc`)

- **`pipe_retry_count` race condition**: The retry counter in `sys_pipe_read` was a global static variable (`dd 0`). Under preemptive multitasking two tasks blocking on separate pipes could clobber each other's count, causing infinite waits. Fixed by moving the counter to a register (EAX) local to each call — the global is removed.

### Kernel — Filesystem (`kernel/hbfs.inc`)

- **`sys_symlink` dead code**: Two dead `mov edi, ...` instructions preceded the correct `mov edi, [esp+24]` line, producing confusing assembly. Removed the two stale lines; behavior is unchanged.

### Shell — New Commands (`kernel/shell.inc`, `kernel/data.inc`)

- **`chmod <octal> <filename>`**: New shell command wrapping `SYS_CHMOD` (#68). Parses an octal permission value (e.g. `chmod 755 myprog`) and updates `DIRENT_PERMS`. Returns success/failure message.
- **`chown <uid> <filename>`**: New shell command wrapping `SYS_CHOWN` (#69). Parses a decimal UID and updates `DIRENT_OWNER`.
- **`stat` permission display**: The `stat` command now shows two additional lines: `Perms: rwxrwxrwx (octal)` and `Owner: <uid>`, reading from `DIRENT_PERMS` and `DIRENT_OWNER` respectively. Previously these fields were stored on disk but never displayed.

### Programs — Display Fix (`programs/neofetch.asm`)

- **Logo null terminators**: Each `logo_art` line was 32 bytes with no null terminator. `SYS_PRINT` (`vga_print`) scans until null, so every logo line printed the entire remaining logo blob as one continuous string. Fixed by adding a null byte to each line (33 bytes/line) and updating the stride calculation from `shl eax, 5` to `imul eax, 33`. Logo now renders correctly.

---

## v3.0.0 - Kernel Overhaul, Filesystem Permissions, TCP Backlog & 4 New Syscalls

### Kernel — Syscall Dispatch (`kernel/syscall.inc`)

- **O(1) jump table**: Replaced 68-entry linear `cmp`/`je` chain with a 128-entry indexed jump table (`jmp [syscall_table + eax * 4]`). Average dispatch time reduced from ~34 comparisons to 1 lookup.
- **Extensible**: Reserved slots 72–127 for future syscalls; unused entries point to a safe error handler.

### Kernel — Scheduler Enhancements (`kernel/sched.inc`)

- **TASK_BLOCKED state** (state 3): New task state for sleeping/waiting tasks, automatically skipped by round-robin scanner.
- **sys_sleep** (SYS_SLEEP #16): Rewritten from busy-wait `HLT` loop to proper blocking — sets TASK_BLOCKED, stores wakeup tick in `TCB_WAKEUP`, yields CPU to other tasks.
- **sched_wake_sleepers**: Called every PIT tick from `irq_timer`; scans all blocked tasks and wakes those whose wakeup tick has elapsed.
- **TCB_PRIORITY** (offset 24) and **TCB_WAKEUP** (offset 28): New fields replacing reserved padding.
- **MAX_TASKS** doubled from 16 to 32.

### Kernel — IPC Pipe Fix (`kernel/ipc.inc`)

- **Non-spinning pipe reads**: `sys_pipe_read` now yields CPU up to 200 times when the pipe buffer is empty, instead of immediately returning 0 bytes. Eliminates 100% CPU polling loops in callers.
- **pipe_wake_waiter** stub: Preparation for future proper wait-queue integration on pipe write.

### Kernel — ISR / IRQ Hardening (`kernel/isr.inc`)

- **Timer context switch CLI**: Added defensive `CLI` before the critical ESP-swap section in `irq_timer`. Prevents potential nested-interrupt corruption if gate type ever changes.
- **Wake sleepers on tick**: `irq_timer` now calls `sched_wake_sleepers` every tick to unblock sleeping tasks.

### Kernel — Page Fault Handler (`kernel/paging.inc`)

- **Human-readable error codes**: Page fault handler now parses error code bits and prints cause: `[not present]`/`[protection]`, `[read]`/`[write]`, `[supervisor]`/`[user]`.

### Filesystem — Permissions & Symlinks (`kernel/hbfs.inc`)

- **File permissions** (DIRENT_PERMS, offset 276): 9-bit Unix-style `rwxrwxrwx` stored in previously-reserved directory entry bytes. New files default to `0777`.
- **File ownership** (DIRENT_OWNER, offset 278): 16-bit owner UID per file.
- **SYS_CHMOD** (#68): Change file permission bits.
- **SYS_CHOWN** (#69): Change file owner UID.
- **SYS_SYMLINK** (#70): Create symbolic link (file type `FTYPE_LINK=5`, target path stored as file data).
- **SYS_READLINK** (#71): Read symbolic link target path.

### Networking — TCP Accept Backlog (`kernel/net.inc`)

- **Child socket allocation**: When a SYN arrives on a LISTEN socket, a NEW child socket is allocated. The parent stays in `TCP_LISTEN` for more connections — enables multiple simultaneous accepts.
- **TCP SYN_RCVD state handler**: Added missing state machine handler for `TCP_SYN_RCVD`. When the final ACK of the 3-way handshake arrives, properly transitions child socket to `TCP_ESTABLISHED`. This fixes a bug where server-side TCP connections could never complete.
- **Child socket resolution**: `tcp_handle` now scans for child sockets matching remote IP/port before dispatching, ensuring handshake packets reach the correct socket.
- **SOCK_PARENT field** (offset 68): Tracks which listening socket spawned each child.

### Networking — UDP Checksum (`kernel/net.inc`)

- **RFC 768 checksum**: `udp_send` now computes a proper UDP checksum over the pseudo-header (source IP, dest IP, protocol, length) plus UDP header and payload, replacing the previous hardcoded zero.

### Programs & User-Space

- **syscalls.inc**: Added `SYS_CHMOD` (#68), `SYS_CHOWN` (#69), `SYS_SYMLINK` (#70), `SYS_READLINK` (#71) for user programs.
- Version strings updated across: `neofetch`, `uname`, `bterm`, screensaver, MOTD, Burrows About dialog.

### Build & Tests

- All 45 tests passing (syscall consistency, HBFS layout, listing validation).
- 169 files populated in HBFS image.

## v2.2.0 - IPC, Audio, Screensavers, 52 New Programs & Bug Fixes

### Kernel — Inter-Process Communication (`kernel/ipc.inc`)

- **Pipes**: 8 concurrent pipes, 4 KB circular buffer each, with `SYS_PIPE_CREATE`, `SYS_PIPE_WRITE`, `SYS_PIPE_READ`, `SYS_PIPE_CLOSE` (syscalls 60–63).
- **Shared memory**: 4 regions, 4 KB each, keyed access via `SYS_SHMGET` and `SYS_SHMADDR` (syscalls 64–65).

### Kernel — Sound Blaster 16 Audio Driver (`kernel/sb16.inc`)

- ISA DMA playback via SB16 DSP (base port 0x220, IRQ 5, DMA channels 1/5).
- Supports 8-bit and 16-bit PCM, mono/stereo, configurable sample rate.
- Three syscalls: `SYS_AUDIO_PLAY` (50), `SYS_AUDIO_STOP` (51), `SYS_AUDIO_STATUS` (52).

### Kernel — Process Management

- `SYS_KILL` (53): Terminate a task by PID.
- `SYS_GETPID` (54): Get current task PID.
- `SYS_PROCLIST` (66): Query task table slots (0–15).
- `SYS_MEMINFO` (67): Report free/total physical pages.

### Kernel — GUI Enhancements

- **Clipboard**: `SYS_CLIP_COPY` (55) and `SYS_CLIP_PASTE` (56) for inter-app text sharing.
- **Notifications**: `SYS_NOTIFY` (57) — toast-style notifications with color accent bars.
- **File dialogs**: `SYS_FILE_OPEN_DLG` (58) and `SYS_FILE_SAVE_DLG` (59).
- **Date setting**: `SYS_SETDATE` (49) — write to RTC from user programs.
- **Widget toolkit**: 7 sub-functions (button, checkbox, progress bar, textbox, listbox, label, rectangle outline) for GUI_DRAW_BUTTON through GUI_DRAW_RECT (sub-functions 20–26).

### Kernel — Screensaver System (`kernel/screensaver.inc`)

- 5 screensaver modes: Starfield (64 parallax stars), Matrix (cascading green columns), Pipes (6 colored growing pipes), Bouncing Logo, Plasma (color-cycling plasma effect).
- Activates after 5 minutes idle. `scrsaver` shell command to cycle/set mode.

### Kernel — Bug Fixes

- **VBE font restore**: Save/restore 8 KB VGA plane 2 font data before/after BGA mode switches. Fixes corrupted text mode characters after exiting Burrows desktop.
- **3D button rendering**: `gui_bb_button_3d` left-highlight vline clobbered EDX (button height) with the white color value. This caused 3 spurious dark vertical lines from each window's title bar buttons to the bottom of the screen. Fixed by preserving EDX across the vline call.
- **HBFS directory caching**: Added cache-tag check in `hbfs_load_root_dir` to avoid redundant disk reads during PATH searches.
- **HBFS timestamps**: Files now store RTC-based create/modify timestamps in DOS-compatible packed format.
- **HBFS symbolic links**: File type 5 (`FTYPE_LINK`) with `ln -s` shell command and `stat` resolution.

### Shell — New Commands

- **`stat`**: Display file metadata (type, size, blocks, timestamps, link target).
- **`fsck`**: Filesystem consistency check (bitmap vs. directory cross-validation).
- **`whoami`**: Print current user name.
- **`ln`**: Create symbolic links (`ln -s target linkname`).
- **`scrsaver`**: Cycle or set screensaver mode.
- **`mouse`**: Show mouse position and button state.
- Total: 58 unique commands + 6 aliases = 64 dispatched names.

### Programs — 61 New Programs (79 → 140)

New games: blackjack, breakout, chess, connect4, freecell, kingdom, mastermind, neurovault, outbreak, pong, puzzle15, raycaster, rogue, simon, solitaire, starfield.

New utilities: asm (in-OS assembler), banner, base64, basename, bcalc, bedit, bforager, bhive, bnotes, bpaint, bplayer, bsettings, bsheet, bsysmon, bterm, bview, cmp, csv, cut, debug, df, diff, dirname, du, expr, factor, find, forth, free, grep, hexdump, id, lolcat, mandel, nl, od, paste, periodic, perl (interpreter), pipes, ps, sed, seq, sort, strings, sysinfo, tac, tcc (C compiler).

### Programs — Perl Interpreter & Samples

- **perl**: In-OS Perl interpreter supporting variables, arrays, hashes, control flow, string operations, and built-in functions.
- 6 Perl sample scripts: `hello.pl`, `factorial.pl`, `fizzbuzz.pl`, `guess.pl`, `arrays.pl`, `strings.pl`.

### Syscall Summary

20 new syscalls added (48 → 68 total, numbered 0–67):

| Range | Syscalls |
| --- | --- |
| 49 | `SYS_SETDATE` |
| 50–52 | `SYS_AUDIO_PLAY`, `SYS_AUDIO_STOP`, `SYS_AUDIO_STATUS` |
| 53–54 | `SYS_KILL`, `SYS_GETPID` |
| 55–56 | `SYS_CLIP_COPY`, `SYS_CLIP_PASTE` |
| 57 | `SYS_NOTIFY` |
| 58–59 | `SYS_FILE_OPEN_DLG`, `SYS_FILE_SAVE_DLG` |
| 60–63 | `SYS_PIPE_CREATE`, `SYS_PIPE_WRITE`, `SYS_PIPE_READ`, `SYS_PIPE_CLOSE` |
| 64–65 | `SYS_SHMGET`, `SYS_SHMADDR` |
| 66–67 | `SYS_PROCLIST`, `SYS_MEMINFO` |

### Documentation

- All 7 documentation files verified and updated against actual kernel code.
- Fixed outdated statistics across README, USER_GUIDE, INSTALL, TUTORIAL, TECHNICAL_REFERENCE.

### ISO Distribution

- **Bootable ISO**: `make iso` produces an El Torito no-emulation ISO with full disk image, all 7 docs, README, LICENSE, and CHANGELOG.
- **Lite ISO**: `make iso-lite` truncates the 2 GB disk image to 64 MB (~65 MiB ISO vs ~2.1 GiB full) — all HBFS data preserved.
- **ISO verification**: `make iso-verify` validates the El Torito boot record and boot-load-size.
- **ISO launcher**: `run_iso.sh` extracts and boots the ISO with colored output and ASCII banner.
- **Build script**: `build_iso.sh` rewritten with ANSI-colored status output, pre/post build stats, and SHA256 checksums.
- Supports 4 ISO-creation backends: xorriso, genisoimage, mkisofs, hdiutil.

### Build Stats

- Disk image: 169 files, 710 blocks across 4 subdirectories
- Kernel: ~28,800 lines of x86 assembly (22 include files)
- Kernel binary: ~550 KB
- Tests: 1,160 (45 build + 1,115 HBFS integrity)
- Syscalls: 68 (0–67)
- Programs: 140 assembly + 11 C samples + 6 Perl samples

---

## v2.1.0 - Full TCP/IP Networking Stack

### Kernel — TCP/IP Networking (`kernel/net.inc`, ~3,800 lines)

The former RTL8139 networking stub has been replaced with a complete, from-scratch TCP/IP stack:

- **RTL8139 NIC driver**: PCI auto-detect (bus/device/function scan), software reset with timeout, interrupt-driven RX/TX (ISR handles ROK, TOK, RER, TER, LinkChg), 8 KB RX ring buffer with wrap-around, 4 TX descriptors with round-robin rotation.
- **Ethernet II**: Frame construction and parsing with proper EtherType dispatch (0x0800 IPv4, 0x0806 ARP).
- **ARP**: Request/reply handling, 16-entry ARP cache with lookup and expiry, gratuitous ARP on interface configuration.
- **IPv4**: Header construction with checksum, packet reception and protocol dispatch (ICMP=1, UDP=17, TCP=6).
- **ICMP**: Echo request/reply (ping) with sequence numbering and round-trip time calculation.
- **UDP**: Connectionless send/receive with port matching, used for DHCP and DNS.
- **TCP**: Full state machine — SYN → SYN-ACK → ESTABLISHED → data transfer → FIN/FIN-ACK → CLOSED. Sequence/acknowledgment number tracking, 1460-byte MSS, retransmission, connection timeout.
- **DHCP client**: Complete 4-phase negotiation (DISCOVER → OFFER → REQUEST → ACK) with option parsing for subnet mask, gateway, DNS server, and lease time.
- **DNS resolver**: UDP-based query construction and response parsing with answer section extraction.

### Kernel — Socket API (10 new syscalls)

| Syscall | Number | Description |
| --------- | -------- | ------------- |
| `SYS_SOCKET` | 39 | Create a TCP or UDP socket |
| `SYS_CONNECT` | 40 | Connect a TCP socket to a remote host:port |
| `SYS_SEND` | 41 | Send data on a connected socket |
| `SYS_RECV` | 42 | Receive data from a connected socket |
| `SYS_BIND` | 43 | Bind a socket to a local port |
| `SYS_LISTEN` | 44 | Mark a socket as listening for connections |
| `SYS_ACCEPT` | 45 | Accept an incoming TCP connection |
| `SYS_DNS` | 46 | Resolve a hostname to an IPv4 address |
| `SYS_SOCKCLOSE` | 47 | Close a socket and free resources |
| `SYS_PING` | 48 | Send an ICMP echo request and wait for reply |

### Kernel — ISR TX Re-entrance Fix

- **Problem**: TCP acknowledgments and DHCP responses were sent from within the RTL8139 interrupt handler (ISR), which could corrupt in-flight TX descriptors and cause packet loss or hangs.
- **Solution**: Added `SOCK_PENDING` field (offset 48) to the socket structure. The ISR stores pending flags (ACK, SYN-ACK, FIN-ACK) instead of calling `tcp_send_flags` directly. Polling loops in `sys_connect`, `sys_recv`, `sys_send`, `sys_accept`, and `sys_sockclose` call `tcp_flush_pending` to drain deferred transmissions outside interrupt context.
- **Impact**: Fixed DHCP timeout failures and TCP connection stalls.

### Kernel — Bug Fixes

- **`sys_recv` return path**: Changed `ret` to `iretd` — the syscall returns via interrupt frame, not a near return. Missing `iretd` caused stack corruption and triple faults after receiving data.
- **DHCP buffer overflow**: Reduced DHCP option copy length from 300 to 75 bytes (maximum valid DHCP option payload), preventing overwrite of adjacent kernel data.
- **DHCP race condition**: Added `dhcp_state` flag to prevent processing duplicate OFFER/ACK packets from retriggered responses during the 4-phase handshake.

### Shell — 5 New Networking Commands

- **`dhcp`**: Run DHCP client to obtain IP, subnet mask, gateway, and DNS server.
- **`ping <host>`**: Send ICMP echo request and display RTT.
- **`arp`**: Display the ARP cache (IP → MAC mappings).
- **`ifconfig`**: Show network interface configuration (IP, MAC, gateway, DNS).
- **`net`**: Display NIC status, PCI location, I/O base, and MAC address.

### Programs — 7 New Internet Clients + Network Library

- **forager** — HTTP/1.0 web browser. Connects to port 80, sends GET request, displays response body. Tested end-to-end with `forager example.com` in QEMU.
- **ping** — Standalone ICMP ping utility with configurable count, TTL display, and RTT statistics.
- **telnet** — Interactive Telnet client with raw TCP socket communication and escape sequences.
- **ftp** — FTP client with passive mode, directory listing, file get/put, and cd/ls commands.
- **gopher** — Gopher protocol browser (port 70) with selector navigation.
- **mail** — SMTP mail client for composing and sending email via port 25.
- **news** — NNTP news reader for browsing Usenet newsgroups.
- **`programs/lib/net.inc`**: User-space networking library with socket wrappers, DNS helper, HTTP request builder, and line-buffered receive.

### Build Stats

- Disk image: 96 files across 4 subdirectories
- Kernel: ~20,000 lines of x86 assembly (19 include files)
- Kernel binary: ~399 KB
- Tests: 709 (175 build + 534 HBFS integrity)
- Syscalls: 48 (0–48)
- Programs: 79 assembly + 11 C samples

---

## v2.0.0 - Paging, Preemptive Multitasking, Mouse, Burrows Desktop & 10 New Programs

### Kernel — Virtual Memory

- **Paging** (`kernel/paging.inc`): Identity-maps the first 128 MB via 32 page tables (page directory at 0x380000, page tables at 0x381000). All pages marked present, writable, and user-accessible. `paging_map_page` utility for dynamic page mapping. Page-fault handler (INT 14) prints faulting address, EIP, and error code, then recovers to the shell.

### Kernel — Preemptive Multitasking

- **Preemptive scheduler**: The PIT timer handler (IRQ0) now preempts ring-3 tasks every 100 ms (10-tick quantum at 100 Hz). Checks interrupted code's CPL via the CS RPL bits on the interrupt stack frame — kernel code is never preempted. Round-robin scan for next READY task with TCB ESP save/restore and TSS ESP0 update. Cooperative `SYS_YIELD` still works alongside preemptive switching.

### Kernel — PS/2 Mouse Driver

- **Mouse driver** (`kernel/mouse.inc`): Initializes the PS/2 auxiliary port via the 8042 controller (enable aux device, read/write command byte, set defaults, enable data reporting). IRQ12 handler collects 3-byte packets (flags, delta-X, delta-Y) with packet sync validation (bit 3 check). Tracks `mouse_x` (0–79), `mouse_y` (0–24), and `mouse_buttons` (left/right/middle). PS/2 Y-axis inversion for screen coordinates.
- **`SYS_MOUSE` (syscall 36)**: Returns mouse position and button state (EAX=x, EBX=y, ECX=buttons).
- **`mouse` shell command**: Displays current mouse position and button state.

### Kernel — Burrows Desktop Environment

- **VBE/BGA driver** (`kernel/vbe.inc`): Detects Bochs VBE adapter via I/O ports (IDs 0xB0C0–0xB0C5). Sets linear framebuffer modes from protected mode (no real-mode INT 10h). Default 640×480×32 mode. LFB identity-mapped (8 MB) via `paging_map_page`. Full VGA mode 3 restore with CRTC/GC/AC register reprogramming.
- **Drawing primitives**: `vbe_putpixel`, `vbe_fill_rect`, `vbe_clear`.
- **`SYS_FRAMEBUF` (syscall 37)**: Sub-functions for get info (0), set mode (1), and restore text mode (2).
- **`gui` shell command**: Enters the Burrows desktop at 640×480 with colour bars and mouse cursor tracking. Press any key to return to text mode.

### Shell — Wildcard Expansion

- **Global glob preprocessing** (`shell_expand_globs`): Before command dispatch, all arguments containing `*` or `?` are expanded against the current directory listing. Matching filenames replace the glob pattern in the command line. Unmatched globs are passed through literally (POSIX behavior). This makes wildcards work for **all** commands (e.g., `cat *.txt`, `wc *.c`), not just `del` and `copy`.

### Programs — 10 New Showcase Programs

- **top** — Live process/task monitor showing scheduler state, memory usage, and uptime
- **rogue** — ASCII roguelike dungeon crawler with FOV, inventory, combat, and multiple dungeon levels
- **starfield** — Animated 3D starfield simulation with parallax depth effect
- **matrix** — Matrix-style falling green character rain animation
- **weather** — Simulated weather station with multi-day forecast display
- **periodic** — Interactive periodic table browser with element details
- **forth** — FORTH language interpreter with stack operations, arithmetic, and word definitions
- **chess** — Two-player chess with move validation, check detection, and Unicode-style pieces
- **clock** — Analog ASCII clock with sin/cos lookup tables, hour/minute/second hands, and digital display
- **asm** — Interactive x86 assembler REPL that shows machine code bytes for ~25 instruction types

### Build Stats

- Disk image: 83 files, 248 blocks used
- Kernel: ~13,800 lines of x86 assembly (19 include files)
- Tests: 618 (84 build + 534 HBFS integrity)
- Syscalls: 38 (0–37)
- Programs: 66 assembly + 11 C samples

---

## v1.16 - Batch Scripting, Cooperative Multitasking & Networking

### Shell Enhancements

- **Batch scripting directives**: `:LABEL`, `goto LABEL`, `if [not] errorlevel N cmd`, `rem` comments, and `@cmd` (silent execution). Batch scripts now support conditional branching and flow control.
- **Background job detection**: Trailing `&` on a command line is detected and stripped with a "not yet supported" message; the command runs in the foreground. Prepares the shell syntax for future background job support.
- **`net` command**: Displays NIC status, PCI location, I/O base address, and MAC address.
- **`ls -l` timestamps**: Long directory listing now shows a `Modified` column with time since boot in `HHH:MM:SS` format, read from the HBFS `DIRENT_MODIFIED` field.

### Kernel / Subsystems

- **`SYS_STDIN_READ` (syscall 34)**: Programs can read piped stdin data from the shell's redirection buffer. Returns byte count in EAX, or -1 if no stdin is available.
- **`SYS_YIELD` (syscall 35)**: Cooperative yield syscall for the new scheduler. Saves the current task's ring-3 context and switches to the next ready task via round-robin.
- **Cooperative scheduler** (`kernel/sched.inc`): Task Control Block (TCB) array supporting up to 4 concurrent tasks. Per-task kernel stacks allocated from PMM. Round-robin `sys_yield` with proper TSS ESP0 updates for ring transitions.
- **RTL8139 networking stub** (`kernel/net.inc`): PCI bus 0 scan for RTL8139 NIC, software reset, RX/TX buffer allocation via PMM, MAC address read, and frame transmit function. Foundation for future TCP/IP support.

### Programs — Stdin Pipe Support

- **sort, tr, grep, sed, cut**: Now accept piped stdin when no filename argument is given (e.g., `cat file | sort`).
- **tee**: Redesigned to support `tee OUTFILE` (reads stdin) in addition to the legacy `tee INFILE OUTFILE` mode.

### Tests & CI

- **Portable test script**: `tests/test_build.sh` now works on both macOS and Linux. Replaced GNU-specific `stat -c` with a `wc -c` based `file_sz()` helper, and `grep -oP` with `grep -E -o` pipelines.
- **Syscall consistency**: Test now verifies all 36 syscalls (was 34).

### Build Stats

- Disk image: 73 files, 233 blocks used
- Kernel: ~12,300 lines of x86 assembly (16 include files)
- Tests: 45 build + 534 HBFS integrity
- Syscalls: 36 (0–35)

---

## v1.15 - Kernel Hardening & Interrupt Safety

### Kernel Bug Fixes

- **`build_cwd_path` buffer overflow**: Path construction wrote to a 256-byte buffer but max path depth (16 levels × 253 chars) could reach ~4 KB. Enlarged `path_search_buf` to 4096 bytes and added bounds checking with guaranteed null termination on truncation.
- **`copy_word` unbounded write**: Shell word extraction had no destination size limit. Added bounded `copy_word_n` variant (ECX = max bytes, always null-terminates). Migrated `cmd_find_file`, `cmd_append_file`, and `cmd_mkdir` to use the bounded version.
- **Ctrl+C redirection state leak**: Pressing Ctrl+C during a redirected command (e.g., `prog > file`) left `stdout_redir_active` and related flags set, corrupting subsequent output. The Ctrl+C handler now calls `shell_redir_reset` to clear all redirection state before returning to the shell prompt.
- **Interrupt-unsafe stdout redirection**: A keyboard interrupt during `vga_putchar`'s redirection check could corrupt the redirect buffer or length counter. Wrapped the redirection capture section with `cli`/`sti` to make the test-and-write atomic.
- **Interrupt-unsafe directory state**: `cd` path traversal modified `dir_depth`, `current_dir_lba`, `current_dir_sects`, and `current_dir_name` without interrupt protection. A Ctrl+C mid-update could leave the directory stack inconsistent. Added `cli`/`sti` around state mutations in both `cd_enter_subdir` and `cd_pop_stack`.

### Library Bug Fixes

- **`io_file_write` register mapping**: Register remapping clobbered ECX (size) before copying it to EDX. The syscall received the buffer address as the size argument, causing writes to produce corrupted or zero-length files. Fixed register assignment order to preserve all parameters correctly.

### Build System

- **Makefile lib dependency tracking**: Program build rule now depends on `$(wildcard programs/lib/*.inc)` in addition to `syscalls.inc`, so changes to any library file trigger program rebuilds.

### Tests

- **NOBITS warning regression test**: New test in `test_build.sh` scans all `.lst` files for "nobits" warnings, catching `section .bss` ordering bugs (like the v1.14 math.inc issue).
- **File content integrity test**: New test in `test_hbfs.py` reads known program binaries from the disk image and compares them byte-for-byte with the local `.bin` files, catching populate.py data corruption.

### Documentation

- **API_REFERENCE.md**: Added error handling patterns table documenting which functions use EAX=-1, EAX=0/null, or carry flag for error signaling.

## v1.14 - Test Suite Expansion & Shell Hardening

### Bug Fixes

- **Alias expansion infinite loop**: If an alias expanded to a command starting with its own name (or circular aliases like `A→B`, `B→A`), the shell would loop forever. Added `alias_expanding` guard flag that limits alias expansion to one level per command line, matching standard shell behavior:contentReference[oaicite:0]{index=0}. The flag is reset at each new prompt.
- **`sys_readdir` unbounded filename copy**: The `SYS_READDIR` syscall copied filenames to the user buffer without length checking, risking buffer overflow if the caller provided a small buffer. Now capped at `HBFS_MAX_FILENAME` (252) bytes with forced null termination.

### Test Suite

- **Build tests expanded (31 → 44)**: New checks include:
  - All 55 program binaries built successfully (was only 12 spot-checked)
  - No program exceeds 1MB (`PROGRAM_MAX_SIZE`)
  - 9 HBFS constant consistency checks between `kernel.asm` and `populate.py`
  - All 34 syscall numbers verified consistent between `kernel.asm` and `programs/syscalls.inc`
  - Kernel binary entry point validation
- **HBFS integrity tests expanded (40 → 534)**: New checks include:
  - Full subdirectory traversal — all 4 subdirectories validated with child file entry checks
  - Program binary header validation — all 55 executables checked for valid x86 opcode at entry
  - Global block allocation overlap check across all directories (root + subdirectories)
  - Bitmap-vs-file block count cross-verification
  - Stray bitmap bit detection beyond allocated range
  - File census — total files across all directories verified (72 files)

### Build Stats

- Disk image: 72 files, 229 blocks used
- Kernel: ~10,600 lines of x86 assembly
- Tests: 578 (44 build + 534 HBFS integrity)

---

## v1.13 - Standard PATH-Based Program Search

### Breaking Change

- **Removed global directory search**: File operations (`cat`, `size`, `rm`, `rename`, `stat`, `fd_open`, etc.) now only search the **current working directory**. Previously, any file could be accessed from any directory without a path — the kernel would silently scan root and every subdirectory. This was non-standard.. Users must now either `cd` into the correct directory or use an explicit path (e.g., `cat /docs/readme.txt`).

### Shell / Exec

- **PATH-based program execution**: `cmd_exec_program` still uses the `PATH` environment variable (default `PATH=/bin:/games`) to search for executables not found in the current directory. This is the only remaining multi-directory search and works like Unix `$PATH`.
- **`which` command**: Continues to check builtins first, then CWD, then `PATH` directories — unchanged.

### Bugfix

- **`env_get_var` fix**: `env_get` returns the value pointer in EAX (pushad frame offset 28), but `env_get_var` was checking EDI (unchanged after `popad`) instead of EAX — so it always reported "not found". Fixed to use `test eax, eax`. This was a latent bug masked by the old global directory search; with global search removed, PATH-based exec depended on `env_get_var` working correctly.

### Serial I/O Hardening

- **Hardware probe**: `serial_init` now tests the UART scratch register before configuring COM1. If no serial hardware is detected, `serial_present` is set to 0 and all serial I/O becomes a safe no-op.
- **Non-blocking `serial_getchar`**: Changed from an infinite busy-wait to a non-blocking poll. Returns `0xFF` immediately when no data is available. `SYS_SERIAL_IN` (syscall 33) now correctly returns `-1` when the receive buffer is empty, matching the documented ABI.
- **Guard on `serial_putchar`**: Skips output when `serial_present` is 0, preventing hangs on systems without a UART.
- **`serial` test utility** (`/bin/serial`): New program for interactive bidirectional serial testing. `serial send <text>` sends a line; bare `serial` enters an interactive terminal (green = outgoing, cyan = incoming, Escape to quit).
- **`make run-serial`**: New Makefile target that launches QEMU with serial on TCP port 4555 (`nc localhost 4555` to connect).
- **Documentation**: `readme.txt` and `notes.txt` updated with serial usage instructions, QEMU connection examples, and use cases (debug logging, remote shell, file transfer, automated testing, data export).

### Internal

- **`hbfs_find_file_global`**: Simplified from a full recursive directory scan to a single CWD lookup (`hbfs_load_root_dir` + `hbfs_find_file`). The `.gff_moved` flag is always 0 now (kept for ABI compatibility with callers that check it).
- **`hbfs_read_file`**: Removed the `.not_found` fallback that scanned all directories. Path-qualified filenames (`/dir/file`) still work via the path resolution code path.
- **Kernel binary**: ~470 bytes smaller from removed global search code.

---

## v1.12 - Compiler Fixes, Kernel Hardening & Modular Split

### TCC Compiler Fixes

- **Expression precedence**: Replaced flat single-level expression parser with a 7-level precedence-climbing parser (`||` → `&&` → `==`/`!=` → `<`/`>`/`<=`/`>=` → `+`/`-` → `*`/`/`/`%` → unary). Operators now bind correctly: `2 + 3 * 4` evaluates to 14, not 20.
- **String literal addressing**: Rewrote string handling to use a fixup table. `store_string` returns a string index; `emit_string_data` emits string bytes at the end of the output and patches all fixup locations with correct runtime addresses. Fixes printf/string-literal crashes.

### Build System (v1.12)

- **Auto kernel size**: `stage2.asm` no longer has a hardcoded `KERNEL_SECTORS equ 384`. The Makefile generates `kernel_sectors.inc` from the actual `kernel.bin` size (`ceil(size / 512)`), so the stage 2 loader always loads exactly the right amount.
- **Kernel include tracking**: `$(KERNEL_BIN)` now depends on `$(wildcard kernel/*.inc)`, so touching any include file triggers a rebuild.
- **Regression test suite**: New `make check` target runs 71 automated tests:
  - `tests/test_build.sh` — binary size guards (boot ≤ 512, stage2 ≤ 16 KB, kernel < 512 KB), MBR signature, superblock magic, bitmap and root directory sanity, program binary existence, TCC binary checks.
  - `tests/test_hbfs.py` — deep HBFS integrity: superblock field validation, bitmap-vs-directory consistency, per-file block range and allocation overlap checks.

### Kernel Hardening

- **ATA retry wrappers**: `ata_read_sectors` and `ata_write_sectors` now retry up to 3 times with an ATA soft reset (SRST via control register 0x3F6) between attempts. All existing callers (HBFS, shell commands, syscalls) automatically benefit. The raw single-attempt functions are still available as `ata_read_sectors_raw` / `ata_write_sectors_raw`.
- **HBFS error propagation**: `hbfs_load_root_dir`, `hbfs_load_bitmap`, and `hbfs_save_root_dir` now return CF (carry flag) on I/O failure with descriptive error messages.

### Kernel Modular Split

- **13 include files**: `kernel.asm` is now a 300-line master file (constants, entry point, `%include` directives). The ~10,300 lines of subsystem code are split into:
  - `kernel/vga.inc` — VGA text mode driver
  - `kernel/pic.inc` — PIC initialization
  - `kernel/idt.inc` — IDT setup
  - `kernel/isr.inc` — ISR/IRQ handlers
  - `kernel/pit.inc` — PIT timer + keyboard driver
  - `kernel/pmm.inc` — physical memory manager
  - `kernel/ata.inc` — ATA PIO driver + retry wrappers
  - `kernel/hbfs.inc` — HBFS filesystem
  - `kernel/filesearch.inc` — global file search
  - `kernel/syscall.inc` — syscall handler
  - `kernel/shell.inc` — command shell (~4,200 lines)
  - `kernel/util.inc` — utilities, serial, RTC, speaker, TSS, ELF loader, FD table, env vars, subdir support, new syscalls/commands, tab completion
  - `kernel/data.inc` — string data, scancode tables, IDT descriptor, BSS
- Binary output is **byte-identical** to the monolithic version.

### Build Stats (v1.12)

- Disk image: 48 files, 188 blocks used
- Kernel: ~10,600 lines of x86 assembly (split across 14 files)
- Tests: 71 (31 build + 40 HBFS integrity)

---

## v1.10 - Robustness & Filesystem Integrity Enhancements

### Enhancements

- **`df` total file count**: The `df` command now counts files across **all** directories (root + subdirectories), not just the current directory. Reports "N files in M directories" instead of showing a count for just the CWD.
- **Superblock `free_blocks` tracking**: `hbfs_alloc_blocks` and `hbfs_free_blocks` now update the superblock's `free_blocks` counter (offset 12) after every allocation/deallocation, keeping the on-disk superblock consistent with the bitmap.
- **Nested batch execution guard**: `cmd_exec_batch` now detects re-entrant calls (a `batch` command inside a `.bat` script) and rejects them with an error message instead of silently corrupting the shared `batch_script_buf` / `batch_line_buf` buffers.

### Build Stats (v1.10)

- Disk image: 48 files, 188 blocks used
- Kernel: ~10,000 lines of x86 assembly

---

## v1.9 - Code Review Bug Fixes & Enhancements

### Critical Bug Fixes

- **`.save_type` overflow** (hbfs_create_file): The file type parameter was stored via `mov [.save_type], edx` (32-bit write) into a 1-byte `db 0` variable, corrupting the first 3 bytes of `hbfs_delete_file_entry` (overwriting the `pushad` opcode). Fixed by changing to `dd 0`.
- **`cmd_cd` silent failure**: The `cd` command checked `[esp + 28]` (stale pushad-saved EAX) instead of the actual `EAX` register returned by `cmd_cd_internal`. This meant `cd` to a nonexistent directory never showed an error message. Fixed to `cmp eax, -1`.
- **`fd_close` cross-directory bug**: When a file opened via `hbfs_find_file_global` from another directory was closed after writes, `fd_close` only searched the current directory for the entry to persist the updated file size — silently dropping the update. Fixed by recording the directory LBA/sects in the fd table entry at open time (offsets 20-27), then switching to that directory during close.
- **`sys_exec_call` always returned 0**: The SYS_EXEC syscall returned `xor eax, eax` even when `cmd_exec_program` failed (CF set). Programs calling SYS_EXEC couldn't detect failure. Now returns -1 on failure.

### Enhancements (v1.9)

- **`cat -n` line numbering**: Replaced manual 4-digit space padding with `vga_print_dec_width` for cleaner, more maintainable code.
- **`str_has_wildcards` / `str_has_asterisk`**: Now preserve ESI (push/pop) to prevent subtle caller bugs.
- **ls -l alignment**: Right-aligned file sizes in 9-character field using `vga_print_dec_width`.
- **SYS_FWRITE file type**: ESI parameter now specifies file type (FTYPE_TEXT..FTYPE_BATCH); TCC passes FTYPE_EXEC so compiled programs show as executables.
- **Shutdown message**: Styled with COLOR_HEADER separator bar; message printed before ACPI shutdown to prevent cutoff.

### Build Stats (v1.9)

- Disk image: 48 files, 188 blocks used
- Kernel: ~9,900 lines of x86 assembly

---

## v1.8 - Global File Search (Directory-Transparent Operations)

### hbfs_find_file_global

- **New core function**: `hbfs_find_file_global` searches for a file across all directories — current dir first, then root, then every subdirectory. Returns with CWD pointing to the directory containing the file, so save/delete/rename operations target the correct location.
- **GFF-private CWD save/restore**: Dedicated `gff_save_cwd`/`gff_restore_cwd` with separate BSS slots (`gff_cwd_lba`, `gff_cwd_sects`, `gff_cwd_depth`, `gff_cwd_name`, `gff_cwd_stack`) — avoids conflicts with `file_save_cwd` and `path_save_cwd` used by other subsystems.
- **`.gff_moved` flag**: Callers check this to know whether CWD was changed, and restore it after the operation completes.

### Commands Updated

- **rm / del**: Fixed CPU exception bug — now uses `hbfs_find_file_global` + restores CWD after delete. Files can be deleted from any directory regardless of where the user is.
- **ren / rename**: Uses global search for exact renames; saves directory after rename, then restores CWD.
- **size**: Uses global search to display file info from any directory.
- **SYS_DELETE (syscall 9)**: Programs can now delete files in any directory.
- **SYS_STAT (syscall 11)**: Programs can now stat files in any directory.
- **fd_open**: File descriptors can now open files in any directory.

### Build Stats (v1.8)

- Disk image: 48 files, 188 blocks used
- Kernel: ~9830 lines of x86 assembly (~166KB)

---

## v1.7 - Path-Based File Access

### Path Resolution in hbfs_read_file

- **Full path support**: All file operations (cat, batch, run, diff, head, tail, etc.) now accept absolute and relative paths — e.g., `cat /docs/readme`, `run /bin/hello`, `diff /docs/readme /docs/notes`
- **Automatic path splitting**: `hbfs_read_file` scans filenames for `/`; if found, splits into directory part and basename, cd's into the directory, reads the file, then restores the user's original working directory
- **file_save_cwd / file_restore_cwd**: Separate CWD save/restore functions using dedicated BSS variables (`file_save_lba`, `file_save_sects`, `file_save_depth`, `file_save_name`, `file_save_stack`), avoiding conflicts with `path_save_cwd` used by the PATH search
- **Relative paths**: Supports `../bin/hello`, `games/snake`, `./readme` — resolves via `cmd_cd_internal` which handles `.`, `..`, absolute, and multi-component paths
- **Zero call-site changes**: All 19 callers of `hbfs_read_file` gain path support automatically

### Build Stats (v1.7)

- Disk image: 48 files, 188 blocks used
- Kernel: ~9510 lines of x86 assembly (165KB)

---

## v1.6 - Directory Organization & PATH Search

### Filesystem Restructuring

- **Subdirectory support in populate.py**: Rewrote image builder with `FSImage` class supporting `create_subdir()` and `add_file(directory=...)` methods
- **Organized virtual drive into 4 subdirectories**:
  - `/bin` — 22 utility programs (hello, edit, mandel, tcc, sort, grep, wc, etc.)
  - `/games` — 10 game programs (2048, galaga, guess, life, maze, mine, piano, snake, sokoban, tetris)
  - `/samples` — 10 C source files (hello.c, fib.c, calc.c, matrix.c, wumpus.c, etc.)
  - `/docs` — 5 text files (readme, license, notes, todo, poem)

### PATH-Based Program Search

- **Working PATH mechanism**: Kernel searches colon-separated PATH directories when a program isn't found in the current directory
- **Default PATH**: Set to `/bin:/games` — programs in these directories run from anywhere
- **`set PATH` command**: Users can customize PATH (e.g., `set PATH /bin:/games:/samples`)
- **path_save_cwd / path_restore_cwd**: Utility functions to save and restore full directory state (LBA, sectors, depth, name, dir_stack) during PATH traversal
- **cd-based search**: PATH search cd's into each directory, searches there, reads file data directly from directory entry, then restores the user's original working directory

### Updated Commands

- **which**: Now searches PATH directories; shows full path (e.g., `hello is /bin/hello (external)`)
- **help**: Updated to mention PATH search and configuration instructions

### Bug Fixes (v1.6)

- **Critical PATH fallthrough fix**: `.path_not_found` was falling through into `.found_program` — now correctly jumps to `.not_found`
- **NASM optimization oscillation**: Added `-O0` flag to kernel build to prevent label oscillation errors during assembly

### Build Stats (v1.6)

- Disk image: 48 files, 188 blocks used (4 subdirectories + files)
- Kernel: ~9440 lines of x86 assembly

---

## v1.5 - Command & Program Expansion

### New Internal Commands (11)

- **diff**: Side-by-side file comparison with colored output (< red, > green)
- **uniq**: Remove adjacent duplicate lines; flags: `-c` (count prefix), `-d` (duplicates only)
- **rev**: Reverse each line of a file character-by-character
- **tac**: Print file lines in reverse order (last line first)
- **alias**: Define, list, and show shell command aliases (16-slot table)
- **history**: Display numbered shell command history from history buffer
- **which**: Locate a command — shows if built-in or finds external program on disk
- **sleep**: Pause for N seconds (100 ticks/sec timer), supports Ctrl+C abort
- **color**: Set foreground/background VGA color (hex values 0-F)
- **size**: Show file size in bytes/blocks plus type (text/dir/exec/batch/unknown)
- **strings**: Extract printable strings from a file (default ≥4 chars, configurable via flag)

### Shell Enhancements

- **Alias expansion**: Shell parser checks alias table before command dispatch; recursive expansion into `alias_expand_buf`

### New Helper Functions

- **vga_newline**: Convenience function wrapping `mov al, 0x0A / call vga_putchar`
- **str_compare**: Compare two null-terminated strings at ESI/EDI, sets ZF on match

### New External Programs (9 assembly)

- **life**: Conway's Game of Life — 78×23 grid, glider/blinker/R-pentomino seeds
- **maze**: Random maze generator + BFS solver — 39×21 DFS-carved maze with colored path
- **2048**: The 2048 sliding tile game — 4×4 board, arrow keys/WASD, scoring
- **piano**: PC speaker piano — 15 notes (C4-D5), scale and Mary Had a Little Lamb demos
- **mandel**: Mandelbrot set renderer — fixed-point 16.16 arithmetic, 78×23, color gradient
- **pager**: File pager (like `more`) — 23-line pages, space/enter/q controls
- **sed**: Stream editor — search and replace first occurrence per line
- **tr**: Character translator — SET1→SET2 mapping via 256-byte translation table
- **csv**: CSV file viewer — formatted columns, colored headers, pipe separators

### New C Sample Programs (5)

- **hanoi.c**: Tower of Hanoi solver (4 disks, iterative binary counter method)
- **bf.c**: Brainfuck interpreter with hardcoded Hello World program
- **wumpus.c**: Hunt the Wumpus — 8-room cave, move/shoot, hazards
- **matrix.c**: Matrix rain effect — falling characters animation (40 columns × 20 rows)
- **calc.c**: Integer calculator — multi-digit numbers with +, -, *, / operators

### TCC Compiler Bug Fixes (3)

- **line_num reset**: Line counter not reset between compilations — second compile reported wrong line numbers
- **add_global_var extra next_token**: Global variable declarations consumed one too many tokens — broke subsequent parsing
- **Assignment expr_name clobbering**: Assignment expression overwrote expr_name register — corrupted variable name lookup

### Build Stats (v1.5)

- Disk image: 48 files, 172 blocks used
- Kernel: ~9250+ lines of x86 assembly

---

## v1.4 - HBFS Filesystem Expansion

### Bug Fixes (v1.4)

- **sys_free double-shift**: `pmm_free_page` expects physical address but `sys_free` was converting to page number first, causing double `shr 12` and freeing wrong pages — corrupted memory bitmap
- **cmd_copy_file stack corruption**: Wildcard paths jumped to `.src_not_found` which did `pop esi`, but wildcard paths never pushed ESI — stack corruption on "no matches" case
- **env_get_var wrong register**: Checked `EDI == 0` instead of comparing EDI to saved copy; EDI was always non-zero (dest buffer pointer), so variable-not-found was never detected — broke PATH-based program search
- **hbfs_create_file overflow**: `.copy_name` loop had no bounds check against `HBFS_MAX_FILENAME` (252); long filenames could overflow into metadata fields
- **df bitmap scan**: Only scanned 512 of potentially 2000+ bitmap bytes — reported ~1/4 of actual disk usage on 64MB disks
- **hbfs_read_file stale buffer**: Did not call `hbfs_load_root_dir` before `hbfs_find_file`, could search stale directory data
- **fd_close size persistence**: File size updated via `SYS_WRITE` was only stored in the FD table — never written back to the directory entry on close; file appeared truncated after reopen
- **Batch script overwrite**: `cmd_exec_batch` loaded scripts to `PROGRAM_BASE` where shell commands (cat, head, copy) also load data — commands would overwrite the batch script mid-execution
- **ATA LBA48 bits 24-31**: Both `ata_read_sectors` and `ata_write_sectors` zeroed LBA byte 3 instead of sending bits 24-31 from EAX — limited disk access to 8GB (16M sectors) instead of the full 32-bit LBA range

### New Syscalls

- **SYS_MKDIR (12)**: Create a subdirectory; EBX = name pointer, returns EAX = 0 success / -1 error
- **SYS_READDIR (13)**: Read directory entry by index; EBX = filename buffer, ECX = entry index, returns EAX = file type (-1 = end), ECX = file size

### Documentation Fixes

- Version text updated: v1.3 → v1.4, 28 → 227/56 files, 33 → 34 syscalls
- Banner string updated: HB Lair v1.3 → v1.4
- `hbfs_find_file` comment: "root directory" → "current directory"
- `HBFS_DIR_ENTRY_SIZE` comment: corrected field sizes and order to match actual offsets
- `populate.py`: Fixed root dir comment (2 → 16 blocks), readme.txt (34 syscalls), notes.txt (added SYS_SERIAL_IN), todo.txt (34 syscalls)
- `syscalls.inc`: Documented SYS_MKDIR/SYS_READDIR as now implemented

### Internal Changes (v1.4.1)

- Extracted `hbfs_mkdir` shared function from `cmd_mkdir` — used by both shell command and `SYS_MKDIR` syscall
- `cmd_exec_batch` uses 32KB `batch_script_buf` in BSS instead of `PROGRAM_BASE`
- `fd_close` scans directory by `start_block` to persist file size for writable FDs
- `ata_read_sectors` / `ata_write_sectors` send `EAX[24:31]` as LBA byte 3 in high phase

### Major Changes

- **Root directory expanded**: 2 blocks → 16 blocks (28 → 227 file entries per directory)
- **Subdirectories expanded**: 1 block → 4 blocks per subdirectory (14 → 56 entries each)
- **Multi-level subdirectories**: Full support for nested directories to 16 levels deep
- **Directory stack**: Parent directory tracking via push/pop stack enables proper `cd ..` from any depth
- **Multi-component paths**: `cd a/b/c`, `cd ../sibling`, `cd /abs/path` all work correctly
- **Full path display**: Shell prompt, `pwd`, and `SYS_GETCWD` show complete path (e.g., `/projects/src`)

### Disk Layout Changes

- Kernel area increased from 192 to 384 sectors (96KB → 192KB) to accommodate larger BSS
- Superblock moved from LBA 225 to LBA 417
- Bitmap at LBA 418, Root directory at LBA 426-553, Data starts at LBA 554
- **Note**: Existing disk images must be reformatted (incompatible layout change)

### Internal Changes

- New `HBFS_SUBDIR_BLOCKS` constant (4) controls subdirectory allocation size
- New `build_cwd_path` utility builds full path string from directory stack
- `hbfs_format` uses loop to zero all 16 root directory blocks
- `cmd_mkdir` zeros all allocated blocks and stores correct block count
- All 12 directory iteration loops auto-adapt via `hbfs_get_max_entries`
- Added BSS: `dir_depth` (dword), `dir_stack` (16 × 264 bytes = 4,224 bytes)
- `hbfs_dir_buf` expanded from 8KB to 64KB

## v1.3 - Usability & Security Update

### New Features

- **Command-line arguments**: Programs receive arguments via `SYS_GETARGS` (syscall 32); shell parses `program arg1 arg2` syntax
- **Ctrl+C hard-abort**: Keyboard IRQ detects Ctrl+C while a program is running and immediately returns to shell (no program cooperation needed)
- **FD write implementation**: `SYS_WRITE` via file descriptors now performs real block read-modify-write to disk instead of being a stub
- **Raw disk access restriction**: `SYS_DISK_READ` (22) and `SYS_DISK_WRITE` (23) are denied to ring 3 user programs for security

### New Programs

- **cal.asm**: Calendar display showing current month with day-of-week calculation (Sakamoto's algorithm), highlights today
- **calc.asm**: Interactive integer calculator with +, -, *, /, % operators, hex output, signed arithmetic

### Enhancements (v1.3)

- **edit.asm**: Now accepts filename from command line (`edit myfile.txt`) via SYS_GETARGS instead of always editing scratch.txt
- **Syscall count**: 33 syscalls (added SYS_GETARGS = 32)
- **Version text**: Updated to v1.3 with new feature descriptions

### Documentation (v1.3)

- **INSTALL.md**: Complete build and installation guide
- **USER_GUIDE.md**: Comprehensive user manual with all commands
- **TECHNICAL_REFERENCE.md**: Architecture, memory map, HBFS spec, driver details
- **PROGRAMMING_GUIDE.md**: Tutorial on writing Mellivora OS programs
- **API_REFERENCE.md**: Complete syscall API reference with examples

## v1.2 - Major Feature Release

### Bug Fixes (v1.2)

- **IRQ PIC2 EOI**: Split irq_stub into PIC1-only and PIC2 variants; PIC2 IRQs now send EOI to both controllers
- **ATA sector overflow**: LBA48 sector count now sends high byte (CH) instead of always 0
- **cmd_copy redundant find**: Removed unnecessary second `hbfs_find_file` call; uses ECX from `hbfs_read_file` directly
- **guess.asm backspace**: Backspace now does BS+space+BS for proper visual erase
- **CHANGELOG programs**: Fixed v1.0 program list to match actual programs (banner, colors, guess, primes)
- **populate.py docs**: Fixed stale notes.txt (root dir LBA 234-249, data LBA 250+), updated todo.txt/readme.txt

### Architecture Enhancements

- **Ring 3 user mode**: Programs now run in ring 3 with TSS (selector 0x28), user code/data segments (0x18/0x20)
- **ELF loader**: Minimal ELF32 binary loader - parses ELF magic and loads PT_LOAD segments
- **Boot splash**: Stage 2 displays blue title bar ("Mellivora OS - Booting...") during boot
- **Program return code**: SYS_EXIT saves EBX as program exit code; shell reports non-zero codes

### New Drivers

- **Serial console**: COM1 at 115200 baud for debug output; serial_init/serial_putchar/serial_print
- **RTC clock**: Read date/time from CMOS (ports 0x70/0x71) with BCD-to-binary conversion
- **PC speaker**: PIT channel 2 beep via port 0x61; configurable frequency and duration

### New Syscalls (6 new, total 30)

- **SYS_BEEP (24)**: Play tone on PC speaker (EBX=frequency, ECX=duration_ms)
- **SYS_DATE (25)**: Read RTC date/time into buffer
- **SYS_CHDIR (26)**: Change current directory
- **SYS_GETCWD (27)**: Get current working directory
- **SYS_SERIAL (28)**: Write string to serial port
- **SYS_GETENV (29)**: Get environment variable value
- **SYS_OPEN/READ/WRITE/CLOSE/SEEK (5-8,10)**: File descriptor operations implemented

### New Shell Commands (13 new, total 34)

- **echo**: Print text with $VAR environment variable expansion
- **wc FILE**: Line, word, and byte count
- **find FILE PATTERN**: Substring search with line numbers
- **append FILE TEXT**: Append text to existing file
- **date**: Display current date/time (YYYY-MM-DD HH:MM:SS)
- **beep**: Play 1000Hz tone for 200ms
- **batch FILE**: Execute shell commands from a script file
- **mkdir NAME**: Create subdirectory entry
- **cd DIR**: Change current directory
- **pwd**: Print working directory
- **set NAME=VALUE**: Set environment variable
- **unset NAME**: Remove environment variable
- **Tab completion**: Filename auto-completion in shell
- **Ctrl+C**: Interrupt running program

### New Subsystems

- **File descriptors**: 8-slot FD table with open/read/write/close/seek operations
- **Environment variables**: 16 variables, 128 bytes each, $VAR expansion in echo/batch
- **Subdirectories**: Basic directory support with current_dir_lba tracking

### New Programs (v1.2)

- **edit.asm**: Full-screen text editor with cursor movement, insert/delete, Ctrl+S save, Ctrl+Q/ESC quit
- **tetris.asm**: Classic Tetris with 7 tetrominoes, rotation, scoring, levels, next-piece preview

### Code Quality

- **syscalls.inc**: Shared include file with all 30 SYS_* constants and common print_dec routine
- **All 10 programs**: Refactored to use `%include "syscalls.inc"`, eliminated duplicated constants and print_dec
- **Makefile**: Added .lst listing files, populate.py as dependency, syscalls.inc as program dependency
- **Named constants**: DIRENT_* offsets for directory entry fields replace magic numbers

## v1.1 - Comprehensive Review & Hardening

### Bug Fixes (v1.1)

- **Multi-block filesystem**: Files can now span multiple 4KB blocks. `hbfs_alloc_blocks` allocates N contiguous blocks, `hbfs_create_file` writes all sectors, `hbfs_delete_file_entry` frees all blocks.
- **parse_hex_byte**: Fixed inverted carry flag semantics in hex byte parser (enter command).
- **Shift key bounds check**: Added guard for scancodes < 0x20 before shift_table lookup to prevent out-of-bounds read.
- **Keyboard buffer overflow**: Added buffer-full check before writing to ring buffer.
- **cmd_cat overflow**: Clamp file read size to PROGRAM_MAX_SIZE - 1 to prevent null-terminator overflow.
- **ATA flush**: Moved FLUSH CACHE command outside the write loop (was flushing after every sector).
- **Rename length check**: Filename copy now checks against HBFS_MAX_FILENAME (252 chars).
- **Snake tail rendering**: Save old tail position before shift_body loop, use saved coordinates for erase.
- **Sysinfo wasted division**: Removed useless first div in uptime calculation (result was immediately overwritten).
- **Minesweeper stack overflow**: Converted recursive 8-way flood_reveal (up to 800-deep recursion, ~80KB stack) to iterative algorithm with explicit stack array.

### Robustness Improvements

- **IDT fully populated**: All 256 IDT entries now filled with isr_default, preventing #GP on unexpected interrupts.
- **Exception handlers**: Separate handlers for exceptions with/without error codes. Prints faulting EIP and error code, then recovers to shell (no more cli/hlt freeze).
- **Syscall register preservation**: Syscall handlers now save/restore EBX, ECX, EDX, ESI, EDI. Only EAX is modified for return value.

### New Syscalls (v1.1)

- **SYS_DELETE (9)**: Delete a file by name
- **SYS_STAT (11)**: Get file size and block count
- **SYS_MALLOC (19)**: Allocate 4KB-aligned physical memory pages
- **SYS_FREE (20)**: Free allocated memory pages
- **SYS_DISK_READ (22)**: Raw disk sector read
- **SYS_DISK_WRITE (23)**: Raw disk sector write

### New Commands (v1.1)

- **df**: Show HBFS filesystem usage (total/used/free blocks, file count)
- **more FILE**: Page-by-page file viewer (23 lines per page, Space/Enter for next, q/ESC to quit)

### New Features (v1.1)

- **Shell command history**: Up/Down arrow keys recall previous commands (stores last 8 commands)
- **PMM multi-page allocation**: `pmm_alloc_pages` allocates N contiguous physical pages
- **Bitmap load helper**: `hbfs_load_bitmap` shared function for bitmap I/O

### Documentation (v1.1)

- Updated version text to v1.1 with new features
- Fixed stale comments in populate.py (directory = 2 blocks/16 sectors, data starts at LBA 250)
- Updated help text with df and more commands

## v1.0 - Initial Release

- 32-bit protected mode kernel with flat 4GB address space
- HBFS filesystem with 4KB blocks and 28-entry root directory
- ATA PIO disk driver with LBA48 support
- VGA 80x25 text mode with 16 colors
- PS/2 keyboard driver with shift key support
- Physical memory manager with bitmap allocator
- Heap allocator (simple bump allocator)
- PIT timer at 100 Hz
- 11 syscalls via INT 0x80
- Shell with 14 built-in commands
- 10 user programs (hello, banner, colors, fibonacci, guess, primes, sysinfo, snake, mine, sokoban)

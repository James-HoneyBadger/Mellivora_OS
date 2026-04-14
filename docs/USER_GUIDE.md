# Mellivora OS — User Guide

Welcome to Mellivora OS! This guide covers everything you need to know to use the
HB Lair shell, manage files, run programs, network, and get the most out of the system.

> **Version 3.0.0** — 140 programs, 72 syscalls, full TCP/IP networking, Burrows desktop

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Keyboard Controls](#keyboard-controls)
3. [Directory Structure](#directory-structure)
4. [Navigation & PATH](#navigation--path)
5. [Shell Commands Reference](#shell-commands-reference)
6. [File Operations](#file-operations)
7. [Text Processing](#text-processing-hbu--honey-badger-utilities)
8. [Environment Variables & Aliases](#environment-variables--aliases)
9. [Batch Scripting](#batch-scripting)
10. [Programs](#programs)
11. [Networking](#networking)
12. [Burrows Desktop Environment](#burrows-desktop-environment)
13. [The C Compiler (TCC)](#the-c-compiler-tcc)
14. [Tips & Tricks](#tips--tricks)
15. [Limitations](#limitations)

---

## Getting Started

When Mellivora boots, you see a blue banner and the HB Lair shell prompt:

```text
Lair:/>
```

The part after the colon shows your current directory (`/` is root). Type commands and
press **Enter** to execute them. Type `help` to see all available commands.

---

## Keyboard Controls

### Shell Input

| Key | Action |
| --- | --- |
| **Enter** | Execute the current command |
| **Backspace** | Delete character before cursor |
| **Tab** | Auto-complete filename (cycles through matches) |
| **Up Arrow** | Previous command from history |
| **Down Arrow** | Next command in history |
| **Ctrl+C** | Cancel current input / abort running program |
| **Home** | Move cursor to beginning of line |
| **End** | Move cursor to end of line |

### In Programs

| Key | Action |
| --- | --- |
| **Ctrl+C** | Hard-abort — immediately terminates and returns to shell |
| **ESC** | Most programs use ESC to quit (games, editor, calculator) |

### Command History

The shell remembers the last 64 commands. Use **Up** and **Down** arrows to browse.
Press **Enter** to re-execute a recalled command.

---

## Directory Structure

Mellivora organizes 169 files into subdirectories:

```text
/
├── bin/          119 utility and tool programs (edit, grep, tcc, httpd, ...)
├── games/         21 games (snake, tetris, chess, rogue, galaga, ...)
├── samples/       17 sample scripts (11 C, 6 Perl)
├── docs/          10 text files (readme, license, notes, todo, poem, man pages)
├── script.bat    Example batch script
└── welcome.bat   System highlights and quick-start tips
```

Directories support up to 16 levels of nesting. The root directory holds up to 455
entries; subdirectories hold up to 224 entries each.

---

## Navigation & PATH

### Navigating Directories

```text
Lair:/> cd bin               # Enter a subdirectory
Lair:/bin> cd ..             # Go up one level
Lair:/> cd games             # Enter another directory
Lair:/games> cd /            # Return to root
Lair:/> cd docs/subdir       # Multi-component paths work
Lair:/docs/subdir> pwd       # Print current directory
/docs/subdir
```

### The PATH Variable

Programs in `/bin` and `/games` run from anywhere — you don't need to `cd` into their
directories first. This is because the default **PATH** is set to `/bin:/games`.

```text
Lair:/> snake                # Found via PATH in /games
Lair:/> hello                # Found via PATH in /bin
```

When you type a program name, the shell searches:

1. Built-in commands (help, dir, cat, etc.)
2. Current directory
3. Each directory in PATH (left to right)

### Customizing PATH

```text
Lair:/> set PATH /bin:/games:/samples    # Add /samples to PATH
Lair:/> set                              # View all variables (including PATH)
```

### Using Full Paths

All file commands accept absolute and relative paths:

```text
Lair:/> cat /docs/readme           # Absolute path
Lair:/> cat ../docs/readme         # Relative path
Lair:/> diff /docs/readme /docs/notes
Lair:/> run /bin/hello
Lair:/games> cat /samples/hello.c  # Access files across directories
```

### Pipes & Redirection

The shell supports Unix-style redirection, pipes, and command chaining:

```text
Lair:/> echo hello > greet.txt     # Write output to a file
Lair:/> echo world >> greet.txt    # Append output to a file
Lair:/> cat < greet.txt            # Read from redirected stdin
Lair:/> cat greet.txt | wc         # Pipe output into another command
Lair:/> cat /docs/readme | head -n 5
Lair:/> cat /docs/readme | rev
Lair:/> cat /docs/readme && echo ok
Lair:/> cat missing.txt || echo fallback
```

Commands such as `cat`, `head`, `tail`, `wc`, `uniq`, `rev`, `sort`, `grep`, `sed`,
`tr`, `cut`, and `tac` accept piped or redirected input when no filename is supplied.

Use `cmd1 && cmd2` to run `cmd2` only when `cmd1` succeeds, and `cmd1 || cmd2` to run
`cmd2` only when `cmd1` fails.

---

## Shell Commands Reference

### Help & System Information

| Command | Description |
| --- | --- |
| `help` | Display all available commands |
| `ver` | Show OS version, hardware info, feature list |
| `time` | Display uptime in seconds since boot |
| `date` | Show current date and time (YYYY-MM-DD HH:MM:SS) |
| `mem` | Display memory info (free pages, MB, timer ticks) |
| `disk` | Show disk info (total sectors, size in MB) |
| `df` | Filesystem usage (total/used/free blocks, file count) |
| `sysinfo` | Run the sysinfo program for detailed system information |
| `mouse` | Show current mouse position and button state |

### Screen & Display

| Command | Description |
| --- | --- |
| `clear` / `cls` | Clear the screen |
| `color FG BG` | Set text color (hex 0–F, e.g., `color A 0` for green on black) |
| `beep` | Play a beep through the PC speaker |

### Directory Navigation

| Command | Description |
| --- | --- |
| `dir` / `ls` | List files in current directory |
| `dir -l` | Long format with types, sizes, and timestamps |
| `cd DIR` | Change directory (`cd /`, `cd ..`, `cd bin`, `cd /docs/sub`) |
| `pwd` | Print current working directory path |
| `mkdir NAME` | Create a new subdirectory |

### File Viewing

| Command | Description |
| --- | --- |
| `cat FILE` | Display entire file contents |
| `cat -n FILE` | Display with line numbers |
| `head FILE` | Show first 10 lines |
| `head -n 20 FILE` | Show first 20 lines |
| `tail FILE` | Show last 10 lines |
| `tail -n 5 FILE` | Show last 5 lines |
| `more FILE` | Page through file (Space = next page, Q = quit) |
| `hex FILE` | Hexadecimal dump of file |
| `size FILE` | Show file size in bytes/blocks and type |
| `strings FILE` | Extract printable strings (default ≥4 chars) |
| `stat FILE` | Show file metadata (size, type, block location, timestamps) |

### File Creation & Editing

| Command | Description |
| --- | --- |
| `write FILE` | Create/overwrite file (type text, blank line to end) |
| `append FILE TEXT` | Append text to existing file |
| `touch FILE` | Create an empty file |
| `edit FILE` | Launch the full-screen text editor |

### File Management

| Command | Description |
| --- | --- |
| `copy SRC DEST` | Copy a file (wildcards supported: `copy *.txt backup/`) |
| `ren OLD NEW` | Rename a file |
| `del FILE` / `rm FILE` | Delete a file (wildcards: `del *.tmp`) |
| `ln -s TARGET LINK` | Create a symbolic link |

### Filesystem Maintenance

| Command | Description |
| --- | --- |
| `fsck` | Check filesystem integrity (superblock, bitmap, directories) |

### Text Processing (HBU — Honey Badger Utilities)

| Command | Description |
| --- | --- |
| `diff FILE1 FILE2` | Line-by-line file comparison (`<` and `>` diffs) |
| `paste FILE1 FILE2` | Merge lines from two files side-by-side with tab separator |
| `uniq FILE` | Remove adjacent duplicate lines |
| `uniq -c FILE` | Show count prefix for each line |
| `uniq -d FILE` | Show only duplicate lines |
| `rev FILE` | Reverse each line character-by-character |
| `find [-name PATTERN]` | Search for files by name pattern |
| `wc FILE` | Count lines, words, and bytes |
| `cut -f LIST FILE` | Extract specific fields (columns) from text |
| `tee FILE` | Read input and duplicate to stdout and file |
| `head [-n NUM] FILE` | Print first N lines (default: 10) |
| `tail [-n NUM] FILE` | Print last N lines (default: 10) |
| `od [FILE]` | Print octal/hex dump of file contents |

### Networking Commands

| Command | Description |
| --- | --- |
| `net` | Display full network status (NIC, MAC, IP, gateway, DNS) |
| `dhcp` | Request an IP address via DHCP |
| `ping HOST` | Send ICMP pings to a host (IP or hostname) |
| `ifconfig` | Show network info (or `ifconfig IP` to set IP manually) |
| `arp` | Display the ARP cache (IP → MAC mappings) |

### Program Execution

| Command | Description |
| --- | --- |
| `PROGRAM` | Just type the name — found via current dir then PATH |
| `PROGRAM args` | Pass arguments (e.g., `edit myfile.txt`) |
| `run FILE` | Explicitly execute a program file |
| `which NAME` | Show if built-in or locate external program in PATH |
| `enter` | Enter raw hex bytes to create a program |
| `batch FILE` | Execute a batch script |

### Desktop

| Command | Description |
| --- | --- |
| `burrows` | Launch the Burrows desktop environment |
| `scrsaver` | Cycle screensaver mode or set a specific mode |

### Environment Variables

| Command | Description |
| --- | --- |
| `set` | Display all environment variables |
| `set NAME VALUE` | Set a variable (e.g., `set PATH /bin:/games`) |
| `unset NAME` | Remove a variable |
| `echo TEXT` | Print text with `$VAR` expansion |
| `echo -n TEXT` | Print without trailing newline |

### Aliases

| Command | Description |
| --- | --- |
| `alias` | List all defined aliases |
| `alias NAME COMMAND` | Define an alias (e.g., `alias ll dir -l`) |
| `alias NAME` | Show what an alias expands to |

### History

| Command | Description |
| --- | --- |
| `history` | Display numbered command history |
| `!!` | Re-execute the previous command |
| `!N` | Re-execute command number N from history |

### System Operations

| Command | Description |
| --- | --- |
| `shutdown` | Power off (ACPI S5 shutdown, works in QEMU) |
| `reboot` | Restart the system |
| `format` | Format HBFS filesystem (**erases all files!** — requires `y` confirm) |
| `sleep N` | Pause for N seconds (Ctrl+C to abort) |
| `whoami` | Show current user |

---

## File Operations

### Creating Files

```text
Lair:/> write myfile.txt
Hello, this is my file.
Second line here.
                              ← (blank line ends input)
Lair:/>
```

### Viewing Files

```text
Lair:/> cat myfile.txt       # Full contents
Lair:/> cat -n myfile.txt    # With line numbers
Lair:/> head -n 5 myfile.txt # First 5 lines
Lair:/> more /docs/readme    # Page-by-page (paths work!)
```

### Copying, Renaming, Deleting

```text
Lair:/> copy myfile.txt backup.txt
Lair:/> ren backup.txt archive.txt
Lair:/> del archive.txt
```

### Wildcards

All commands that take filenames support `*` and `?` wildcards, expanded globally
before dispatch:

```text
Lair:/> del *.tmp            # Delete all .tmp files
Lair:/> cat *.txt            # Concatenate all .txt files
Lair:/> wc *.c               # Word count for all .c files
```

### Working Across Directories

All file commands accept paths:

```text
Lair:/> cat /docs/readme
Lair:/> head /samples/hello.c
Lair:/> diff /docs/readme /docs/notes
Lair:/> wc /samples/fib.c
Lair:/games> cat /docs/license
```

---

## Text Processing Examples

### Searching in Files

```text
Lair:/> grep pattern file.txt       # Search for lines matching pattern
Lair:/> find -name *.txt            # Find files by name
```

### Comparing Files

```text
Lair:/> diff file1.txt file2.txt
< Line only in file1           (shown in red)
> Line only in file2           (shown in green)
  Common line                  (shown in default color)
```

### Removing Duplicates

```text
Lair:/> uniq data.txt         # Remove adjacent duplicates
Lair:/> uniq -c data.txt      # Show counts
Lair:/> uniq -d data.txt      # Show only duplicated lines
```

### Sorting

```text
Lair:/> sort names.txt            # Alphabetical sort
Lair:/> sort -r names.txt         # Reverse alphabetical
Lair:/> sort -n numbers.txt       # Numeric sort
```

### Stream Editing

```text
Lair:/> sed old new file.txt    # Replace 'old' with 'new'
Lair:/> tr abc ABC file.txt     # Translate characters
Lair:/> cut -f 1,3 data.csv    # Extract fields 1 and 3
```

### Pipelines

```text
Lair:/> cat /docs/readme | grep system | wc
Lair:/> cat data.txt | sort | uniq -c
Lair:/> cat /samples/hello.c | head -n 5
```

### Reversing

```text
Lair:/> rev myfile.txt        # Reverse characters in each line
Lair:/> tac myfile.txt        # Print lines in reverse order (last first)
```

---

## Environment Variables & Aliases

### Setting Environment Variables

```text
Lair:/> set name James        # Set a variable
Lair:/> echo Hello, $name!    # Use in echo ($VAR expansion)
Hello, James!
Lair:/> set                   # List all variables
  PATH=/bin:/games
  name=James
Lair:/> unset name            # Remove a variable
```

**Limits:** 16 variables, 128 bytes each (name + value combined).

The `PATH` variable is special — it controls where the shell searches for programs.

### Defining Aliases

```text
Lair:/> alias ll dir -l       # Create an alias
Lair:/> ll                    # Runs "dir -l"
Lair:/> alias                 # List all aliases
  ll = dir -l
Lair:/> alias ll              # Show specific alias
  ll = dir -l
```

**Limits:** 16 aliases, 32-byte name, 224-byte command.

---

## Batch Scripting

### Creating a Script

```text
Lair:/> write startup.bat
echo === System Starting ===
date
echo Files in root:
dir
echo === Ready ===

Lair:/>
```

### Running a Script

```text
Lair:/> batch startup.bat
> echo === System Starting ===
=== System Starting ===
> date
2026-04-06 14:30:00
> echo Files in root:
Files in root:
> dir
bin             games           samples         docs            script.bat
> echo === Ready ===
=== Ready ===
```

Each line is shown with a `>` prefix before execution.

### Script Directives

| Directive | Description |
| --- | --- |
| `rem COMMENT` | Comment — ignored during execution |
| `:LABEL` | Define a label for goto |
| `goto LABEL` | Jump to a label |
| `if errorlevel N CMD` | Execute CMD if last exit code ≥ N |
| `if not errorlevel N CMD` | Execute CMD if last exit code < N |
| `@CMD` | Run CMD silently (no `>` prefix echo) |

### Script Example with Flow Control

```text
Lair:/> write test.bat
@echo Running tests...
true
if errorlevel 1 goto fail
echo All tests passed!
goto done
:fail
echo TEST FAILED
:done
echo Finished.

Lair:/> batch test.bat
Running tests...
All tests passed!
Finished.
```

---

## Programs

Mellivora ships with **140 assembly programs** organized in `/bin` (119 utilities) and
`/games` (21 games), plus **17 sample scripts** (11 C, 6 Perl) in `/samples`.

### Games (21) — in `/games`

| Program | Controls | Description |
| --- | --- | --- |
| `snake` | Arrow keys, ESC | Classic snake — eat food, grow, don't crash |
| `tetris` | ←→ move, ↑ rotate, ↓ soft drop, Space hard drop, ESC | Tetris with 7 pieces, scoring, and levels |
| `mine` | Arrow keys, Space reveal, F flag, ESC | Minesweeper |
| `sokoban` | Arrow keys, R restart, ESC | Box-pushing puzzle with multiple levels |
| `2048` | Arrow keys / WASD, ESC | Sliding number tiles |
| `galaga` | ←→ move, Space shoot, ESC | Space shooter with enemy waves |
| `guess` | Type numbers | Number guessing with hints |
| `life` | ESC | Conway's Game of Life (78×23 auto-running) |
| `maze` | ESC | Random maze with BFS solver visualization |
| `piano` | Letter keys (A–P) | PC speaker musical keyboard (15 notes) |
| `blackjack` | H/S/Q | Blackjack card game |
| `connect4` | 1–7 columns | Connect Four against the CPU |
| `hangman` | Letter keys | Word-guessing hangman game |
| `hanoi` | Number keys | Towers of Hanoi puzzle |
| `mastermind` | Letter keys | Guess a hidden color code in ten attempts |
| `pong` | W/S, ESC | Classic Pong against the CPU |
| `puzzle15` | Arrow keys | Slide numbered tiles to solve the 15-puzzle |
| `simon` | Number keys | Repeat growing color sequences |
| `tictactoe` | Number keys (1–9) | Tic-Tac-Toe against the CPU |
| `wordle` | Letter keys | Guess a 5-letter word in six attempts |
| `worm` | Arrow keys | Grow a worm by eating food |

### Internet Programs (11) — in `/bin`

| Program | Usage | Description |
| --- | --- | --- |
| `forager` | `forager <host>` | Web browser — fetch web pages |
| `ping` | `ping <host>` | ICMP echo request with RTT display |
| `telnet` | `telnet <host> [port]` | Interactive Telnet client |
| `ftp` | `ftp <host> [port]` | FTP client with passive mode (ls, cd, get, put) |
| `gopher` | `gopher <host> [path] [port]` | Gopher protocol browser with menu formatting |
| `mail` | `mail <server>` | SMTP mail client |
| `news` | `news <server>` | NNTP newsgroup reader |
| `httpd` | `httpd [port]` | HTTP server with directory listing |
| `irc` | `irc <server> [nick]` | IRC client for chat channels |
| `ntpd` | `ntpd [server]` | Synchronize system time via NTP |
| `pkg` | `pkg list\|search\|info` | Package manager |

### Text Processing Tools (16) — in `/bin`

| Program | Usage | Description |
| --- | --- | --- |
| `grep` | `grep PATTERN FILE` | Pattern search in files |
| `sed` | `sed SEARCH REPLACE FILE` | Stream editor (search & replace) |
| `sort` | `sort [-r] [-n] FILE` | Sort lines alphabetically, reverse, or numerically |
| `tr` | `tr SET1 SET2 FILE` | Character translation |
| `cut` | `cut -f LIST [-d C] FILE` | Extract fields (supports `1,3,5-7` ranges) |
| `paste` | `paste FILE1 FILE2` | Join lines side-by-side |
| `head` | `head [-n NUM] FILE` | First N lines (default 10) |
| `tail` | `tail [-n NUM] FILE` | Last N lines (default 10) |
| `wc` | `wc FILE` | Count lines, words, bytes |
| `uniq` | `uniq [-c] [-d] FILE` | Remove adjacent duplicates |
| `rev` | `rev FILE` | Reverse each line |
| `diff` | `diff FILE1 FILE2` | Line-by-line comparison |
| `od` | `od FILE` | Octal/hex binary dump |
| `cmp` | `cmp FILE1 FILE2` | Compare two files byte by byte |
| `nl` | `nl FILE` | Number lines of a file |
| `xxd` | `xxd FILE` | Hex dump with ASCII sidebar |

### Language Interpreters (5) — in `/bin`

| Program | Usage | Description |
| --- | --- | --- |
| `tcc` | `tcc FILE.c` | C compiler — compiles and runs C subset |
| `basic` | `basic` | BASIC (PRINT, INPUT, LET, IF/THEN, GOTO, FOR/NEXT) |
| `forth` | `forth` | FORTH with stack operations and word definitions |
| `asm` | `asm` | Interactive x86 assembler REPL (~25 instruction types) |
| `perl` | `perl FILE.pl` | Perl 5 subset interpreter and REPL |

### System & File Utilities — in `/bin`

| Program | Usage | Description |
| --- | --- | --- |
| `edit` | `edit [FILE]` | Full-screen text editor (Ctrl+S save, ESC quit) |
| `top` | `top` | Live process monitor (tasks, CPU, memory) |
| `hexdump` | `hexdump FILE` | Hex + ASCII file viewer |
| `pager` | `pager FILE` | Page-by-page viewer (Space/Q) |
| `csv` | `csv FILE` | Formatted CSV viewer with colored headers |
| `find` | `find [-name PATTERN]` | Find files matching pattern |
| `basename` | `basename PATH` | Extract filename from path |
| `dirname` | `dirname PATH` | Extract directory from path |
| `sysinfo` | `sysinfo` | Detailed system information |
| `serial` | `serial [send TEXT]` | Serial port testing (bidirectional, escape to quit) |
| `apitest` | `apitest` | Syscall API exercise and validation tool |
| `id` | `id` | User/group IDs (root=0) |
| `whoami` | `whoami` | Show current user |
| `uptime` | `uptime` | System uptime display |
| `cal` | `cal` | Calendar with current day highlighted |
| `calc` | `calc` | Interactive calculator (+, −, ×, ÷, %) |
| `date` | `date` | Display or set current date and time |
| `debug` | `debug` | Inspect memory, registers, and hex dumps |
| `df` | `df` | Show disk usage statistics |
| `du` | `du [FILE]` | Show file sizes on disk |
| `free` | `free` | Display physical memory usage |
| `help` | `help [TOPIC]` | Builtin help and manual pages |
| `hive` | `hive` | Dual-pane TUI file manager |
| `ps` | `ps` | List active tasks |
| `strings` | `strings FILE` | Extract printable strings from binary |
| `touch` | `touch FILE` | Create empty file |
| `uname` | `uname` | Print system information |

### Demos & Visualizations — in `/bin`

| Program | Description |
| --- | --- |
| `mandel` | Mandelbrot set renderer (fixed-point arithmetic) |
| `starfield` | Animated 3D starfield with parallax depth |
| `matrix` | Matrix-style falling green character rain |
| `clock` | Analog ASCII clock with sin/cos hands + digital display |
| `weather` | Simulated weather station with multi-day forecast |
| `periodic` | Interactive periodic table browser with element details |
| `banner` | Colorful ASCII art banner printer |
| `colors` | VGA color palette demo |
| `hello` | Hello World template program |
| `primes` | Prime number generator |
| `fibonacci` | Fibonacci sequence |
| `cowsay` | Display a message in a cow speech bubble |
| `fortune` | Display a random fortune or quote |
| `lolcat` | Print text in rainbow colors |
| `rot13` | Encode or decode text with the ROT13 cipher |
| `typist` | Typing practice with WPM and accuracy tracking |
| `timewarp` | BASIC/PILOT/Logo editor with turtle graphics |

### Burrows GUI Applications (12) — in `/bin`

| Program | Description |
| --- | ---|
| `bterm` | BTerm GUI terminal emulator |
| `bedit` | BEdit GUI text editor |
| `bhive` | BHive GUI file manager |
| `bforager` | BForager GUI web browser |
| `bcalc` | BCalc GUI calculator |
| `bpaint` | BPaint GUI paint application |
| `bsysmon` | BSysMon GUI system monitor |
| `bnotes` | BNotes GUI sticky notes |
| `bplayer` | BPlayer GUI music player with VU meter |
| `bsettings` | BSettings GUI theme customizer |
| `bsheet` | BSheet GUI spreadsheet with formulas |
| `bview` | BView GUI image viewer (24-bit BMP) |

### The Text Editor (edit)

| Key | Action |
| --- | --- |
| Arrow keys | Move cursor |
| Page Up/Down | Scroll by screen height |
| Home/End | Beginning/end of line |
| Backspace | Delete before cursor |
| Delete | Delete at cursor |
| Enter | Insert new line |
| Ctrl+S | Save file |
| Ctrl+Q / ESC | Quit editor |

---

## Networking

Mellivora includes a full TCP/IP networking stack with an RTL8139 NIC driver. When
running in QEMU, networking is enabled by default via user-mode networking.

### Getting Online

```text
Lair:/> dhcp                   # Get an IP address automatically
Requesting IP via DHCP...
DHCP complete: 10.0.2.15

Lair:/> net                    # Check network status
NIC: RTL8139 (Up)
MAC: 52:54:00:12:34:56
IP:  10.0.2.15
Mask: 255.255.255.0
GW:  10.0.2.2
DNS: 10.0.2.3
```

### Shell Networking Commands

| Command | Description |
| --- | --- |
| `net` | Display full network status (NIC, MAC, IP, gateway, DNS) |
| `dhcp` | Request an IP address via DHCP |
| `ping HOST` | Send ICMP pings to a host (IP or hostname) |
| `ifconfig` | Show network info (or `ifconfig IP` to set IP manually) |
| `arp` | Display the ARP cache (IP → MAC mappings) |

### Browsing the Web

```text
Lair:/> forager example.com
Connecting to example.com...
<!doctype html>
<html>
<head>
    <title>Example Domain</title>
...
```

Forager resolves hostnames via DNS, performs a TCP handshake, sends an
HTTP/1.0 GET request, and displays the response body.

### Telnet Sessions

```text
Lair:/> telnet towel.blinkenlights.nl
Connecting to towel.blinkenlights.nl:23...
Connected!
```

Press Ctrl+C to disconnect.

### FTP File Transfers

```text
Lair:/> ftp ftp.example.com
220 Welcome
ftp> ls                   # List remote files
ftp> cd pub               # Change remote directory
ftp> get readme.txt       # Download a file
ftp> put myfile.txt       # Upload a file
ftp> quit
```

### Gopher Browsing

```text
Lair:/> gopher gopher.floodgap.com
[DIR] Welcome to Floodgap Gopher
[TXT] About this server
```

### Email (SMTP)

```text
Lair:/> mail mail.example.com
mail> compose
mail> quit
```

### Usenet News (NNTP)

```text
Lair:/> news news.example.com
news> list
news> group comp.os.mellivora
news> read 1
news> quit
```

### QEMU Networking Notes

QEMU user-mode networking (`-netdev user`) provides:

- Outbound TCP/UDP connections (HTTP, Telnet, FTP, etc.)
- DHCP server at 10.0.2.2 that assigns 10.0.2.15
- DNS forwarding at 10.0.2.3 (resolves public hostnames)
- Gateway at 10.0.2.2 with NAT to host network

Limitations: Inbound connections to the VM require QEMU port forwarding (`-netdev user,hostfwd=...`).

For complete networking documentation, see the [Networking Guide](NETWORKING_GUIDE.md).

---

## Burrows Desktop Environment

Launch the windowed desktop with:

```text
Lair:/> burrows
```

Or run `burrows` from the command line.

### Desktop Features

- **640×480×32-bit** graphics via Bochs VBE/BGA framebuffer
- **Window manager** — up to 16 draggable windows with title bars, close buttons
- **Taskbar** with application launcher and clock
- **Mouse support** — PS/2 IRQ12 driver with cursor tracking
- **4 themes** — Blue, Dark, Light, Amber
- **5 screensaver modes** — Starfield, Matrix, Pipes, Bouncing logo, Plasma
- **Double buffering** — flicker-free rendering

### GUI Applications

| Application | Description |
| --- | --- |
| **BTerm** (`bterm`) | GUI terminal emulator with shell access |
| **BEdit** (`bedit`) | GUI text editor with file open/save |
| **BHive** (`bhive`) | GUI file manager with directory browsing |
| **BForager** (`bforager`) | GUI web browser with clickable links |
| **BCalc** (`bcalc`) | GUI calculator with button interface |
| **BPaint** (`bpaint`) | Drawing application with color palette |
| **BSysMon** (`bsysmon`) | GUI task and memory monitor |
| **BNotes** (`bnotes`) | Sticky notes application |
| **BPlayer** (`bplayer`) | Music player with VU meter (WAV playback) |
| **BSettings** (`bsettings`) | Desktop theme customizer |
| **BSheet** (`bsheet`) | Spreadsheet with formulas |
| **BView** (`bview`) | Image viewer (24-bit BMP) |

### Returning to Text Mode

Press the designated key or close all windows to return to the HB Lair text shell.

---

## The C Compiler (TCC)

Mellivora includes a Tiny C Compiler that compiles a subset of C into flat binaries
and runs them immediately — all inside the OS.

### Compiling and Running C Programs

```text
Lair:/> tcc /samples/hello.c
Compiling hello.c...
Running...
Hello, World!
Lair:/>
```

### Available C Samples (11 files in `/samples`)

| File | Description |
| --- | --- |
| `hello.c` | Hello World |
| `fib.c` | Fibonacci sequence |
| `primes.c` | Prime number sieve |
| `calc.c` | Integer calculator |
| `matrix.c` | Matrix rain animation |
| `hanoi.c` | Tower of Hanoi solver |
| `bf.c` | Brainfuck interpreter |
| `wumpus.c` | Hunt the Wumpus game |
| `boxes.c` | Box drawing demo |
| `stars.c` | Starfield animation |
| `echo.c` | Echo arguments |

### Supported C Features

- Variables (`int` type, global and local)
- Functions with parameters and return values
- Control flow: `if`/`else`, `while`, `for`
- Operators: `+`, `-`, `*`, `/`, `%`, comparisons, logical, bitwise
- `printf()` with `%d` and `%s` format specifiers
- `putchar()`, `getchar()`
- Arrays and pointers (basic support)
- String literals

### Writing Your Own C Programs

```text
Lair:/> write myprogram.c
int main() {
    printf("Hello from my C program!\n");
    int x = 42;
    printf("x = %d\n", x);
    return 0;
}

Lair:/> tcc myprogram.c
```

---

## The Perl Interpreter

Mellivora includes a Perl 5 subset interpreter that runs `.pl` scripts or
starts an interactive REPL.

### Running Perl Scripts

```text
Lair:/> perl /samples/hello.pl
Hello, World!

Lair:/> perl /samples/factorial.pl
```

### Available Perl Samples (6 files in `/samples`)

| File | Description |
| --- | --- |
| `hello.pl` | Hello World |
| `factorial.pl` | Factorial calculator |
| `fizzbuzz.pl` | FizzBuzz |
| `guess.pl` | Number guessing game |
| `arrays.pl` | Array operations demo |
| `strings.pl` | String manipulation demo |

---

## Tips & Tricks

### Tab Completion

Start typing a filename and press **Tab** to auto-complete:

```text
Lair:/> cat rea[Tab]
Lair:/> cat readme             ← completed automatically
```

If multiple files match, press Tab repeatedly to cycle through them.

### Quick File Inspection

```text
Lair:/> wc /docs/readme        # How big is it?
Lair:/> grep memory /docs/notes # Search for "memory"
Lair:/> hex /bin/hello          # Look at binary structure
Lair:/> strings /bin/hello      # Find text in a binary
```

### Using which to Find Programs

```text
Lair:/> which snake
snake is /games/snake (external)
Lair:/> which cat
cat is a built-in command
Lair:/> which nonexistent
nonexistent: not found
```

### Startup Automation

```text
Lair:/> write init.bat
@clear
echo Welcome to Mellivora OS!
date
echo
dir
echo Type 'help' for commands.

Lair:/> batch init.bat
```

### Color Customization

```text
Lair:/> color A 0              # Green text on black background
Lair:/> color F 1              # White text on blue background
Lair:/> color 7 0              # Reset to default (light gray on black)
```

Color values (hex): 0=Black, 1=Blue, 2=Green, 3=Cyan, 4=Red, 5=Magenta, 6=Brown,
7=LightGray, 8=DarkGray, 9=LightBlue, A=LightGreen, B=LightCyan, C=LightRed,
D=LightMagenta, E=Yellow, F=White

### Networking Quick Start

```text
Lair:/> dhcp && forager example.com  # Get IP, then fetch a page
Lair:/> dhcp && ping 8.8.8.8         # Get IP, then ping Google DNS
```

---

## Limitations

- **Single foreground program**: Preemptive multitasking supports up to 16 tasks, but
  the shell runs one foreground program at a time.
- **RTL8139 only**: Networking requires an RTL8139-compatible NIC. QEMU provides one
  by default. No Wi-Fi support.
- **QEMU user-mode networking**: Outbound connections work; inbound requires port
  forwarding configuration.
- **No file permissions**: All files are accessible to all operations.
- **Case-sensitive filenames**: `README.txt` and `readme.txt` are different files.
- **128 MB RAM limit**: Physical memory manager identity-maps up to 128 MB.
- **Root: 455 files, Subdirs: 224 files**: Directory entry limits per directory.
- **16-level directory nesting**: Maximum subdirectory depth.
- **Tab completion**: Completes filenames in the current directory only (not PATH-aware).
- **DNS cache**: 8-entry cache — repeated queries for the same hostname are served from cache.
- **TCP single connection**: One active TCP connection at a time per socket.

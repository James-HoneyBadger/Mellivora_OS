# Mellivora OS — User Guide

Welcome to Mellivora OS! This guide covers everything you need to know to use the
HB Lair shell, manage files, run programs, network, and get the most out of the system.

> **Version 2.1.0** — 79 programs, 48 syscalls, full TCP/IP networking, Burrows desktop

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

The shell remembers the last 8 commands. Use **Up** and **Down** arrows to browse.
Press **Enter** to re-execute a recalled command.

---

## Directory Structure

Mellivora organizes 96 files into subdirectories:

```text
/
├── bin/          65 utility and tool programs (edit, grep, tcc, http, ...)
├── games/        14 games (snake, tetris, chess, rogue, galaga, ...)
├── samples/      11 C source files (hello.c, fib.c, wumpus.c, ...)
├── docs/          5 text files (readme, license, notes, todo, poem)
└── script.bat    Example batch script
```

Directories support up to 16 levels of nesting. The root directory holds up to 227
entries; subdirectories hold up to 56 entries each.

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
| `stat FILE` | Show file metadata (size, type, block location) |

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
| `gui` | Launch the Burrows desktop environment |

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

### System Operations

| Command | Description |
| --- | --- |
| `shutdown` | Power off (ACPI S5 shutdown, works in QEMU) |
| `format` | Format HBFS filesystem (**erases all files!** — requires `y` confirm) |
| `sleep N` | Pause for N seconds (Ctrl+C to abort) |

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

Mellivora ships with **79 assembly programs** organized in `/bin` (65 utilities) and
`/games` (14 games), plus **11 C samples** in `/samples`.

### Games (14) — in `/games`

| Program | Controls | Description |
| --- | --- | --- |
| `snake` | Arrow keys, ESC | Classic snake — eat food, grow, don't crash |
| `tetris` | ←→ move, ↑ rotate, ↓ soft drop, Space hard drop, ESC | Tetris with 7 pieces, scoring, and levels |
| `mine` | Arrow keys, Space reveal, F flag, ESC | Minesweeper |
| `sokoban` | Arrow keys, R restart, ESC | Box-pushing puzzle with multiple levels |
| `2048` | Arrow keys / WASD, ESC | Sliding number tiles |
| `galaga` | ←→ move, Space shoot, ESC | Space shooter with enemy waves |
| `chess` | Algebraic notation (e.g., e2e4), ESC | Two-player chess with check detection |
| `rogue` | Arrow keys / WASD, ESC | ASCII roguelike dungeon with FOV and inventory |
| `kingdom` | Number choices | Medieval kingdom management simulation |
| `outbreak` | Arrow keys | Zombie survival strategy game |
| `life` | ESC | Conway's Game of Life (78×23 auto-running) |
| `maze` | ESC | Random maze with BFS solver visualization |
| `neurovault` | Number keys | Pattern memory game |
| `guess` | Type numbers | Number guessing with hints |

### Internet Clients (7) — in `/bin`

| Program | Usage | Description |
| --- | --- | --- |
| `http` | `http <host>` | HTTP/1.0 GET client — fetch web pages |
| `ping` | `ping <host>` | ICMP echo request with RTT display |
| `telnet` | `telnet <host> [port]` | Interactive Telnet client |
| `ftp` | `ftp <host> [port]` | FTP client with passive mode (ls, cd, get, put) |
| `gopher` | `gopher <host> [path] [port]` | Gopher protocol browser with menu formatting |
| `mail` | `mail <server>` | SMTP mail client |
| `news` | `news <server>` | NNTP newsgroup reader |

### Text Processing Tools (13) — in `/bin`

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

### Language Interpreters (4) — in `/bin`

| Program | Usage | Description |
| --- | --- | --- |
| `tcc` | `tcc FILE.c` | C compiler — compiles and runs C subset |
| `basic` | `basic` | BASIC (PRINT, INPUT, LET, IF/THEN, GOTO, FOR/NEXT) |
| `forth` | `forth` | FORTH with stack operations and word definitions |
| `asm` | `asm` | Interactive x86 assembler REPL (~25 instruction types) |

### System & File Utilities (21) — in `/bin`

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
| `sleep` | `sleep SECONDS` | Pause for N seconds |
| `cal` | `cal` | Calendar with current day highlighted |
| `calc` | `calc` | Interactive calculator (+, −, ×, ÷, %) |
| `seq` | `seq N` | Print numbers 1 to N |
| `true` | `true` | Exit success (for scripts) |
| `false` | `false` | Exit failure (for scripts) |
| `yes` | `yes [STRING]` | Print STRING repeatedly (default "y") |

### Demos & Visualizations (13) — in `/bin`

| Program | Description |
| --- | --- |
| `mandel` | Mandelbrot set renderer (fixed-point arithmetic) |
| `starfield` | Animated 3D starfield with parallax depth |
| `matrix` | Matrix-style falling green character rain |
| `piano` | PC speaker musical keyboard (15 notes) |
| `clock` | Analog ASCII clock with sin/cos hands + digital display |
| `weather` | Simulated weather station with multi-day forecast |
| `periodic` | Interactive periodic table browser with element details |
| `banner` | Colorful ASCII art banner printer |
| `colors` | VGA color palette demo |
| `hello` | Hello World template program |
| `primes` | Prime number generator |
| `fibonacci` | Fibonacci sequence |
| `tee` | Split output to file and stdout |

### Burrows GUI Applications (7) — in `/bin`

| Program | Description |
| --- | --- |
| `burrow` | Desktop environment launcher |
| `bterm` | GUI terminal emulator |
| `bedit` | GUI text editor |
| `bfiles` | GUI file manager |
| `bcalc` | GUI calculator |
| `bpaint` | GUI paint application |
| `bsysmon` | GUI system monitor |

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
Lair:/> http example.com
Connecting to example.com...
<!doctype html>
<html>
<head>
    <title>Example Domain</title>
...
```

The HTTP client resolves hostnames via DNS, performs a TCP handshake, sends an
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
Lair:/> gui
```

Or run `burrow` from the command line.

### Desktop Features

- **640×480×32-bit** graphics via Bochs VBE/BGA framebuffer
- **Window manager** — up to 16 draggable windows with title bars, close buttons
- **Taskbar** with application launcher and clock
- **Mouse support** — PS/2 IRQ12 driver with cursor tracking
- **3 themes** — Dark, Light, Classic
- **Double buffering** — flicker-free rendering

### GUI Applications

| Application | Description |
| --- | --- |
| **Terminal** (`bterm`) | GUI terminal emulator with shell access |
| **Editor** (`bedit`) | GUI text editor with file open/save |
| **Files** (`bfiles`) | GUI file manager with directory browsing |
| **Calculator** (`bcalc`) | GUI calculator with button interface |
| **Paint** (`bpaint`) | Drawing application with color palette |
| **System Monitor** (`bsysmon`) | GUI task and memory monitor |

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
Lair:/> dhcp && http example.com     # Get IP, then fetch a page
Lair:/> dhcp && ping 8.8.8.8         # Get IP, then ping Google DNS
```

---

## Limitations

- **Single foreground program**: Preemptive multitasking supports up to 4 tasks, but
  the shell runs one foreground program at a time.
- **RTL8139 only**: Networking requires an RTL8139-compatible NIC. QEMU provides one
  by default. No Wi-Fi support.
- **QEMU user-mode networking**: Outbound connections work; inbound requires port
  forwarding configuration.
- **No file permissions**: All files are accessible to all operations.
- **Case-sensitive filenames**: `README.txt` and `readme.txt` are different files.
- **128 MB RAM limit**: Physical memory manager identity-maps up to 128 MB.
- **Root: 227 files, Subdirs: 56 files**: Directory entry limits per directory.
- **16-level directory nesting**: Maximum subdirectory depth.
- **Tab completion**: Completes filenames in the current directory only (not PATH-aware).
- **No DNS caching**: Each hostname resolution makes a new DNS query.
- **TCP single connection**: One active TCP connection at a time per socket.
# Mellivora OS — User Guide

Welcome to Mellivora OS! This guide covers everything you need to know to use the
HB Lair shell, manage files, run programs, and get the most out of the system.

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
12. [The C Compiler (TCC)](#the-c-compiler-tcc)
13. [Tips & Tricks](#tips--tricks)
14. [Limitations](#limitations)

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

The shell remembers the last 8 commands. Use **Up** and **Down** arrows to browse.
Press **Enter** to re-execute a recalled command.

---

## Directory Structure

Mellivora organizes files into subdirectories:

```text
/
├── bin/          Utility programs (hello, edit, grep, sort, tcc, ...)
├── games/        Games (snake, tetris, 2048, galaga, mine, ...)
├── samples/      C source files (hello.c, fib.c, wumpus.c, ...)
├── docs/         Documentation (readme, license, notes, ...)
└── script.bat    Example batch script
```

Directories support up to 16 levels of nesting. The root directory holds up to 227
entries; subdirectories hold up to 56 entries each.

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

The shell supports basic Unix-style redirection, single-line pipelines, and command chaining:

```text
Lair:/> echo hello > greet.txt     # Write command output to a file
Lair:/> echo world >> greet.txt    # Append output to a file
Lair:/> cat < greet.txt            # Read from redirected stdin
Lair:/> cat greet.txt | wc         # Pipe output into another command
Lair:/> cat /docs/readme | head -n 5
Lair:/> cat /docs/readme | rev
Lair:/> cat /docs/readme && echo ok
Lair:/> cat missing.txt || echo fallback
```

Commands such as `cat`, `head`, `tail`, `wc`, `uniq`, `rev`, and `tac` accept piped or redirected input when no filename is supplied.

Use `cmd1 && cmd2` to run `cmd2` only when `cmd1` succeeds, and `cmd1 || cmd2` to run `cmd2` only when `cmd1` fails.

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
| `dir -l` | Long format with types and sizes |
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

### Text Processing (HBU — Honey Badger Utilities)

| Command | Description |
| --- | --- |
| `diff FILE1 FILE2` | Line-by-line file comparison (shows `<` for differences) |
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

### Program Execution

| Command | Description |
| --- | --- |
| `PROGRAM` | Just type the name — found via current dir then PATH |
| `PROGRAM args` | Pass arguments (e.g., `edit myfile.txt`) |
| `run FILE` | Explicitly execute a program file |
| `which NAME` | Show if built-in or locate external program in PATH |
| `enter` | Enter raw hex bytes to create a program |
| `batch FILE` | Execute a batch script |

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

### System Operations

| Command | Description |
| --- | --- |
| `shutdown` | Power off (ACPI S5 shutdown, works in QEMU) |
| `format` | Format HBFS filesystem (**erases all files!** — requires `y` confirm) |
| `sleep N` | Pause for N seconds (Ctrl+C to abort) |

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

The `del` and `copy` commands support `*` and `?` wildcards:

```text
Lair:/> del *.tmp            # Delete all .tmp files
Lair:/> copy *.c backup/     # Copy all .c files (future feature)
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
Lair:/> find syscall /docs/notes    # Search for "syscall" in notes
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

### Script Capabilities

Batch scripts can use:

- All shell commands (`cat`, `dir`, `del`, `run`, etc.)
- `echo` with `$VAR` expansion
- `set` and `unset` for variables
- Program execution by name
- Nested `batch` calls
- Full path support (`cat /docs/readme`)

---

## Programs

Mellivora ships with a broad set of user-space programs organized in `/bin` and `/games`
(currently 56 assembly programs).

### Games (in /games)

| Program | Controls | Description |
| --- | --- | --- |
| `snake` | Arrow keys, ESC | Classic snake — eat food, grow, don't crash |
| `tetris` | ←→ move, ↑ rotate, ↓ soft drop, Space hard drop, ESC quit | Tetris with 7 pieces, scoring, and levels |
| `mine` | Arrow keys, Space reveal, F flag, ESC quit | Minesweeper |
| `sokoban` | Arrow keys, R restart, ESC quit | Box-pushing puzzle |
| `2048` | Arrow keys / WASD, ESC quit | Sliding number tiles |
| `galaga` | ←→ move, Space shoot, ESC quit | Space shooter with enemy waves |
| `guess` | Type numbers, Enter | Number guessing with hints |
| `life` | ESC quit | Conway's Game of Life (auto-running) |
| `maze` | ESC quit | Random maze generation + BFS solve |
| `piano` | Number keys 1–9, 0, -, =, etc. | PC speaker piano (15 notes) |

### Utilities (in /bin)

| Program | Usage | Description |
| --- | --- | --- |
| `hello` | `hello` | Hello World — template program |
| `edit` | `edit [FILE]` | Full-screen text editor (Ctrl+S save, Ctrl+Q/ESC quit) |
| `tcc` | `tcc FILE.c` | Tiny C Compiler — compiles and runs C code |
| `grep` | `grep PATTERN FILE` | Search for pattern in file |
| `sort` | `sort FILE` | Sort file lines alphabetically |
| `hexdump` | `hexdump FILE` | Hex + ASCII file dump |
| `sed` | `sed SEARCH REPLACE FILE` | Stream editor (search & replace) |
| `cut` | `cut -f LIST [-d C] FILE` | Field extractor (supports lists/ranges like `1,3,5-7`) |
| `tr` | `tr SET1 SET2 FILE` | Character translator |
| `tee` | `tee INPUTFILE OUTPUTFILE` | Print file and copy it to another file |
| `head` | `head [-n NUM] [FILE]` | Print first N lines (default 10) |
| `tail` | `tail [-n NUM] [FILE]` | Print last N lines (default 10) |
| `rev` | `rev [FILE]` | Reverse each line (chars in reverse order) |
| `yes` | `yes [STRING]` | Output STRING repeatedly (default "y") until interrupted |
| `true` | `true` | Exit with success code (for scripts) |
| `false` | `false` | Exit with failure code (for scripts) |
| `whoami` | `whoami` | Print current user (always "root") |
| `seq` | `seq N` | Print numbers 1 to N (one per line) |
| `basename` | `basename PATH` | Extract filename from path |
| `dirname` | `dirname PATH` | Extract directory from path (or "." if none) |
| `id` | `id` | Print user and group IDs (root=0) |
| `sleep` | `sleep SECONDS` | Pause for N seconds |
| `od` | `od [FILE]` | Octal/hex dump of file |
| `csv` | `csv FILE` | Formatted CSV viewer with colored headers |
| `wc` | `wc FILE` | Line, word, byte count |
| `pager` | `pager FILE` | Page-by-page file viewer |
| `cal` | `cal` | Calendar for current month |
| `calc` | `calc` | Interactive calculator (+, -, *, /, %) |
| `mandel` | `mandel` | Mandelbrot set renderer |
| `basic` | `basic` | BASIC language interpreter |
| `banner` | `banner` | Colorful ASCII art banner |
| `colors` | `colors` | VGA color palette demo |
| `fibonacci` | `fibonacci` | Fibonacci sequence |
| `primes` | `primes` | Prime number calculator |
| `sysinfo` | `sysinfo` | Detailed system information |
| `uptime` | `uptime` | System uptime display |

### Networking (in /bin)

| Program | Usage | Description |
| --- | --- | --- |
| `ping` | `ping <host>` | Send 4 ICMP echo requests, show RTT |
| `http` | `http <url>` | HTTP/1.0 GET client — fetch and display web pages |
| `telnet` | `telnet <host> [port]` | Interactive telnet client (Ctrl+C to quit) |
| `gopher` | `gopher <host> [path] [port]` | Gopher protocol browser with menu formatting |
| `ftp` | `ftp <host> [port]` | Interactive FTP client with PASV mode |
| `mail` | `mail <server>` | Email client — send via SMTP, read via POP3 |
| `news` | `news <server>` | Usenet/NNTP newsgroup reader |

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

Usage:

```text
Lair:/> edit myfile.txt      # Open specific file
Lair:/> edit                  # Opens scratch.txt by default
```

---

## Networking

Mellivora includes a full TCP/IP networking stack with an RTL8139 NIC driver. When
running in QEMU, networking is enabled by default.

### Getting Online

```text
Lair:/> dhcp                   # Get an IP address automatically
Requesting IP via DHCP...
DHCP complete: 10.0.2.15

Lair:/> net                    # Check network status
=== Network Status ===
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
| `ping HOST` | Send 4 ICMP pings to a host (IP or hostname) |
| `ifconfig` | Show network info (or `ifconfig IP` to set IP manually) |
| `arp` | Display the ARP cache (IP → MAC mappings) |

### Using the Network Programs

**Fetch a web page:**

```text
Lair:/> http example.com
Connecting to example.com...
<!doctype html>...
```

**Connect to a remote server:**

```text
Lair:/> telnet towel.blinkenlights.nl
Connecting to towel.blinkenlights.nl:23...
Connected!
```

**Browse Gopher:**

```text
Lair:/> gopher gopher.floodgap.com
[DIR] Welcome to Floodgap Gopher
[TXT] About this server
```

**Transfer files via FTP:**

```text
Lair:/> ftp ftp.example.com
220 Welcome to FTP server
ftp> ls
ftp> get readme.txt
ftp> quit
```

**Send and read email:**

```text
Lair:/> mail mail.example.com
mail> compose
mail> inbox
mail> read 1
mail> quit
```

**Read Usenet newsgroups:**

```text
Lair:/> news news.example.com
news> list
news> group comp.os.mellivora
news> read 1
news> quit
```

For complete documentation, see the [Networking Guide](NETWORKING_GUIDE.md).

---

## The C Compiler (TCC)

Mellivora includes a Tiny C Compiler that compiles a subset of C into ELF executables
and runs them immediately — all inside the OS.

### Compiling and Running C Programs

```text
Lair:/> tcc /samples/hello.c
Compiling hello.c...
Running...
Hello, World!
Lair:/>
```

### Available C Samples (in /samples)

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
- Operators: `+`, `-`, `*`, `/`, `%`, comparisons, logical
- `printf()` with `%d` and `%s` format specifiers
- `putchar()`, `getchar()`
- Arrays and pointers (basic support)

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
Lair:/> find memory /docs/notes # Search for "memory"
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
clear
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

---

## Limitations

- **Cooperative multitasking:** Only one program runs at a time unless programs explicitly yield. No preemptive scheduling yet.
- **Networking requires QEMU RTL8139:** Only the RTL8139 NIC is supported. QEMU user-mode networking allows outbound connections but blocks unsolicited inbound.
- **No file permissions:** All files accessible to all operations.
- **Case-sensitive filenames:** `README.txt` and `readme.txt` are different files.
- **128 MB RAM limit:** Physical memory manager supports up to 128 MB.
- **Root: 227 files, Subdirs: 56 files:** Directory entry limits.
- **16-level directory nesting:** Maximum subdirectory depth.
- **Tab completion:** Only completes filenames in the current directory (not PATH-aware).

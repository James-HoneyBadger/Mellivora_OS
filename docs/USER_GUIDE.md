# Mellivora OS — User Guide

Documentation status: validated against `main` on 2026-04-08.

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
11. [The C Compiler (TCC)](#the-c-compiler-tcc)
12. [Tips & Tricks](#tips--tricks)
13. [Limitations](#limitations)

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

- **Single-tasking:** Only one program runs at a time.
- **No networking:** No network stack.
- **No piping or redirection:** Commands cannot be chained with `|` or `>`.
- **No file permissions:** All files accessible to all operations.
- **Case-sensitive filenames:** `README.txt` and `readme.txt` are different files.
- **128 MB RAM limit:** Physical memory manager supports up to 128 MB.
- **Root: 227 files, Subdirs: 56 files:** Directory entry limits.
- **16-level directory nesting:** Maximum subdirectory depth.
- **Tab completion:** Only completes filenames in the current directory (not PATH-aware).

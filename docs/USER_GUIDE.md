# Mellivora OS — User Guide

## Getting Started

When Mellivora boots, you are greeted with a blue banner and dropped into the **HB DOS**
shell (Honey Badger Disk Operating System). The prompt looks like:

```
HBDOS:/>
```

The part after the colon shows your current directory (`/` is the root). Type commands at
the prompt and press **Enter** to execute them.

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
| **Ctrl+C** | Cancel current input and get a fresh prompt |
| **Home** | Move cursor to beginning of line |
| **End** | Move cursor to end of line |

### In Programs

| Key | Action |
| --- | --- |
| **Ctrl+C** | Hard-abort — immediately terminates the running program and returns to the shell |
| **ESC** | Many programs use ESC to quit (games, calculator, etc.) |

> **Note:** Ctrl+C is a hard abort. The program does not get a chance to clean up or save.

### Command History

The shell remembers the last 8 commands. Use **Up** and **Down** arrows to browse through
them. Press **Enter** to re-execute a recalled command.

---

## Shell Commands Reference

### Help & Information

#### `help`

Display the built-in help listing all available commands.

```
HBDOS:/> help
```

#### `ver`

Show detailed system and version information, including kernel version, filesystem details,
hardware drivers, memory, and feature list.

```
HBDOS:/> ver
```

#### `time`

Display uptime in seconds since boot.

```
HBDOS:/> time
```

#### `date`

Show the current date and time from the real-time clock (YYYY-MM-DD HH:MM:SS format).

```
HBDOS:/> date
2025-01-15 14:32:07
```

#### `mem`

Display memory information: free pages, free memory in MB, and timer ticks since boot.

```
HBDOS:/> mem
```

#### `disk`

Show disk information: total sectors and disk size in MB.

```
HBDOS:/> disk
```

#### `df`

Filesystem usage: total blocks, used blocks, free blocks, and number of files.

```
HBDOS:/> df
```

---

### Screen Control

#### `clear`

Clear the screen and reset the cursor to the top-left.

```
HBDOS:/> clear
```

#### `beep`

Play a beep through the PC speaker (1000 Hz, brief duration).

```
HBDOS:/> beep
```

---

### File Listing & Navigation

#### `dir` / `ls`

List all files in the current directory. Shows file type, size, and name.

```
HBDOS:/> dir
```

Output format:
```
  [TEXT]     1082  readme.txt
  [EXEC]     664  banner
  [DIR ]       0  mydir
  [BTCH]     128  startup.bat
```

File types displayed:
- `[TEXT]` — Text file
- `[EXEC]` — Executable program
- `[DIR ]` — Subdirectory
- `[BTCH]` — Batch script

#### `cd DIR`

Change to a directory.

```
HBDOS:/> cd mydir
HBDOS:mydir>
```

Special directories:
- `cd /` — Return to root
- `cd ..` — Go up one level (returns to root, as subdirectories are single-level)

#### `pwd`

Print the current working directory name.

```
HBDOS:mydir> pwd
mydir
```

#### `mkdir DIR`

Create a new subdirectory.

```
HBDOS:/> mkdir projects
```

> **Note:** Subdirectories currently support one level of nesting. Each directory can hold
> up to 28 entries.

---

### File Viewing

#### `cat FILE`

Display the entire contents of a text file.

```
HBDOS:/> cat readme.txt
```

#### `more FILE`

Page through a file 23 lines at a time. At each page break:
- Press **any key** to continue to the next page
- Press **ESC** or **Q** to quit

```
HBDOS:/> more notes.txt
```

#### `hex FILE`

Display a hexadecimal dump of a file (first 512 bytes). Shows offset, hex bytes, and ASCII
representation.

```
HBDOS:/> hex banner
```

#### `wc FILE`

Count lines, words, and bytes in a file.

```
HBDOS:/> wc readme.txt
  42 lines, 287 words, 1082 bytes
```

#### `find PATTERN FILE`

Search for a text pattern in a file. Shows matching lines with line numbers.

```
HBDOS:/> find syscall notes.txt
```

---

### File Creation & Editing

#### `write FILE`

Create or overwrite a file with text content. Type your text line by line. End input
by pressing **Enter** on an empty line.

```
HBDOS:/> write hello.txt
Hello, world!
This is a test file.

HBDOS:/>
```

#### `append FILE`

Append text to an existing file. Works the same as `write` but adds to the end.

```
HBDOS:/> append hello.txt
Another line added.

HBDOS:/>
```

#### `edit FILE` (program)

Launch the full-screen text editor. See the [Programs](#programs) section for details.

```
HBDOS:/> edit myfile.txt
```

---

### File Management

#### `copy SRC DEST`

Copy a file to a new name.

```
HBDOS:/> copy readme.txt backup.txt
```

#### `ren OLD NEW`

Rename a file.

```
HBDOS:/> ren backup.txt archive.txt
```

#### `del FILE` / `rm FILE`

Delete a file. There is no confirmation prompt and no recycle bin — deletion is immediate
and permanent.

```
HBDOS:/> del archive.txt
```

---

### Program Execution

#### `run FILE`

Execute a program from the filesystem.

```
HBDOS:/> run hello
```

#### Running Programs Directly

You can also just type the program name. If it is not a built-in command, the shell will
try to find and execute it as a program:

```
HBDOS:/> hello
Hello, world!

HBDOS:/> snake
```

#### Passing Arguments

Programs can receive command-line arguments. Anything after the program name is passed
as the argument string:

```
HBDOS:/> edit myfile.txt
```

Here, `myfile.txt` is passed to the `edit` program, which opens that file directly.

---

### Environment Variables

#### `set NAME VALUE`

Set an environment variable.

```
HBDOS:/> set user James
```

#### `set`

With no arguments, display all set environment variables.

```
HBDOS:/> set
  user=James
  prompt=ready
```

#### `unset NAME`

Remove an environment variable.

```
HBDOS:/> unset user
```

#### Using Variables

The `echo` command expands `$VAR` references:

```
HBDOS:/> set name World
HBDOS:/> echo Hello, $name!
Hello, World!
```

> **Limits:** Up to 16 environment variables, each up to 128 bytes (name + value combined).

---

### Text Output

#### `echo TEXT`

Print text to the screen. Supports `$VAR` expansion for environment variables.

```
HBDOS:/> echo System ready.
System ready.
```

---

### System Operations

#### `shutdown`

Power off the system (sends ACPI S5 shutdown command). Works in QEMU; on real hardware,
behavior depends on ACPI support.

```
HBDOS:/> shutdown
```

#### `format`

Format the HBFS filesystem. **This erases all files!** You must type `y` to confirm.

```
HBDOS:/> format
WARNING: This will erase all data! Continue? (y/N) y
Formatting...done.
```

---

### Hex Entry

#### `enter`

Enter raw hexadecimal bytes to create an executable program. Type hex bytes separated by
spaces. End with an empty line, then provide a filename to save.

```
HBDOS:/> enter
Hex> B8 01 00 00 00 BB 41 00 00 00 CD 80
Hex>
12 bytes entered. Save as: test
```

This is useful for quick experiments without needing the full toolchain.

---

### Batch Scripts

#### `batch FILE`

Execute a batch script — a text file where each line is a shell command.

```
HBDOS:/> batch startup.bat
```

#### Creating a Batch Script

Use `write` to create a batch script:

```
HBDOS:/> write startup.bat
echo === System Starting ===
date
echo Files:
dir
echo === Ready ===

HBDOS:/>
```

When executed with `batch startup.bat`, each line runs sequentially, preceded by a `> `
prefix showing which command is being executed.

> **Tip:** Batch scripts support all shell commands including `run`, `set`, `echo` with
> variable expansion, and even nested `batch` calls.

---

## Programs

Mellivora ships with 14 user-space programs. All run in ring 3 (user mode) with full
syscall access.

### hello

A minimal "Hello, world!" program. Good as a template for learning.

```
HBDOS:/> hello
Hello, world from Mellivora OS!
```

### banner

Displays a colorful ASCII art banner demonstrating VGA color capabilities.

```
HBDOS:/> banner
```

### colors

Shows all 16 VGA text-mode color combinations (foreground × background samples).

```
HBDOS:/> colors
```

### fibonacci

Generates and displays the Fibonacci sequence.

```
HBDOS:/> fibonacci
```

### primes

Calculates and displays prime numbers using a sieve algorithm.

```
HBDOS:/> primes
```

### guess

A number guessing game. The computer picks a random number and you try to guess it.
Gives "too high" / "too low" hints.

```
HBDOS:/> guess
```

### sysinfo

Displays detailed system information: CPU features, memory, disk, filesystem stats,
and uptime.

```
HBDOS:/> sysinfo
```

### cal

Calendar display showing the current month in a grid format. Highlights the current
day (white on red) and Sundays (in red).

```
HBDOS:/> cal
```

### calc

Interactive command-line calculator. Supports addition, subtraction, multiplication,
division, and modulo. Shows results in both decimal and hexadecimal.

```
HBDOS:/> calc
Calc> 42 + 17
= 59  (0x3B)
Calc> 100 / 7
= 14  (0xE)
Calc> quit
```

Operators: `+`, `-`, `*`, `/`, `%`

Press **ESC** or type `quit` to exit.

### edit

A full-screen text editor with the following features:

- Arrow key navigation
- Insert and delete text
- Page Up / Page Down scrolling
- Status bar showing filename, cursor position, line count, and file size
- Load and save files

**Keyboard shortcuts in the editor:**

| Key | Action |
| --- | --- |
| **Arrow keys** | Move cursor |
| **Page Up/Down** | Scroll by screen height |
| **Home** | Move to beginning of line |
| **End** | Move to end of line |
| **Backspace** | Delete character before cursor |
| **Delete** | Delete character at cursor |
| **Enter** | Insert new line |
| **Ctrl+S** | Save file |
| **Ctrl+Q** | Quit editor |
| **ESC** | Quit editor |

Usage:
```
HBDOS:/> edit myfile.txt
```

If given a filename argument, the editor opens that file directly. Without an argument,
it opens `scratch.txt` by default.

### snake

Classic Snake game. Control the snake with arrow keys, eat food to grow, avoid walls
and your own tail.

| Key | Action |
| --- | --- |
| **Arrow keys** | Change direction |
| **ESC** | Quit game |

```
HBDOS:/> snake
```

### mine

Minesweeper game. Uncover cells and flag mines on a grid.

```
HBDOS:/> mine
```

### sokoban

Sokoban puzzle game. Push boxes onto target locations.

| Key | Action |
| --- | --- |
| **Arrow keys** | Move player |
| **R** | Restart level |
| **ESC** | Quit game |

```
HBDOS:/> sokoban
```

### tetris

Classic Tetris game with falling tetrominoes.

| Key | Action |
| --- | --- |
| **Left/Right** | Move piece horizontally |
| **Down** | Soft drop |
| **Up** | Rotate piece |
| **Space** | Hard drop |
| **ESC** | Quit game |

```
HBDOS:/> tetris
```

---

## Tips & Tricks

### Tab Completion

Start typing a filename and press **Tab** to auto-complete. If multiple files match,
subsequent Tab presses cycle through the matches.

```
HBDOS:/> cat rea[Tab]
HBDOS:/> cat readme.txt
```

### Quick File Inspection

Use `wc` to check file sizes, `find` to search for content, and `hex` to inspect binary
programs:

```
HBDOS:/> wc notes.txt
HBDOS:/> find memory notes.txt
HBDOS:/> hex hello
```

### Startup Automation

Create a batch script to run commands automatically:

```
HBDOS:/> write init.bat
clear
echo Welcome to Mellivora OS!
date
echo
dir
echo Type 'help' for commands.

HBDOS:/> batch init.bat
```

### Checking Free Space

```
HBDOS:/> df
```

Shows total, used, and free blocks plus file count.

### Environment for Scripts

Set variables before running a batch script to customize behavior:

```
HBDOS:/> set greeting Hello
HBDOS:/> write say.bat
echo $greeting, user!

HBDOS:/> batch say.bat
> echo Hello, user!
Hello, user!
```

---

## Limitations

- **Single-tasking:** Only one program runs at a time. There is no background processing
  or multitasking.
- **No networking:** No network stack is included.
- **28-file directory limit:** Each directory can hold at most 28 entries.
- **Single-level subdirectories:** Directories support one level of nesting from root.
- **No file permissions:** All files are accessible by all operations.
- **Case-sensitive filenames:** `README.txt` and `readme.txt` are different files.
- **No piping or redirection:** Commands cannot be chained with `|` or redirected with `>`.
- **128 MB RAM limit:** The physical memory manager supports the QEMU default of 128 MB.

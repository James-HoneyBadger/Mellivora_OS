# Mellivora OS - Changelog

## v1.7 - Path-Based File Access

### Path Resolution in hbfs_read_file

- **Full path support**: All file operations (cat, batch, run, diff, head, tail, etc.) now accept absolute and relative paths — e.g., `cat /docs/readme`, `run /bin/hello`, `diff /docs/readme /docs/notes`
- **Automatic path splitting**: `hbfs_read_file` scans filenames for `/`; if found, splits into directory part and basename, cd's into the directory, reads the file, then restores the user's original working directory
- **file_save_cwd / file_restore_cwd**: Separate CWD save/restore functions using dedicated BSS variables (`file_save_lba`, `file_save_sects`, `file_save_depth`, `file_save_name`, `file_save_stack`), avoiding conflicts with `path_save_cwd` used by the PATH search
- **Relative paths**: Supports `../bin/hello`, `games/snake`, `./readme` — resolves via `cmd_cd_internal` which handles `.`, `..`, absolute, and multi-component paths
- **Zero call-site changes**: All 19 callers of `hbfs_read_file` gain path support automatically

### Build Stats

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

### Bug Fixes

- **Critical PATH fallthrough fix**: `.path_not_found` was falling through into `.found_program` — now correctly jumps to `.not_found`
- **NASM optimization oscillation**: Added `-O0` flag to kernel build to prevent label oscillation errors during assembly

### Build Stats

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

### Build Stats

- Disk image: 48 files, 172 blocks used
- Kernel: ~9250+ lines of x86 assembly

---

## v1.4 - HBFS Filesystem Expansion

### Bug Fixes

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
- Banner string updated: HB DOS v1.3 → v1.4
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

### Enhancements

- **edit.asm**: Now accepts filename from command line (`edit myfile.txt`) via SYS_GETARGS instead of always editing scratch.txt
- **Syscall count**: 33 syscalls (added SYS_GETARGS = 32)
- **Version text**: Updated to v1.3 with new feature descriptions

### Documentation

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

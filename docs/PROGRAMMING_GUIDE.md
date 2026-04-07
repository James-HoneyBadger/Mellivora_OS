# Mellivora OS — Programming Guide

A hands-on guide to writing programs for Mellivora OS in NASM assembly language.

---

## Table of Contents

1. [Quick Start: Hello World](#quick-start-hello-world)
2. [Program Structure](#program-structure)
3. [The syscalls.inc Include File](#the-syscallsinc-include-file)
4. [Building Programs](#building-programs)
5. [Register Conventions](#register-conventions)
6. [Console Output](#console-output)
7. [Console Input](#console-input)
8. [Color and Screen Control](#color-and-screen-control)
9. [File I/O with File Descriptors](#file-io-with-file-descriptors)
10. [Whole-File I/O](#whole-file-io)
11. [Command-Line Arguments](#command-line-arguments)
12. [Memory Allocation](#memory-allocation)
13. [Timing and Delays](#timing-and-delays)
14. [Sound](#sound)
15. [Date and Time](#date-and-time)
16. [Environment Variables](#environment-variables)
17. [Direct VGA Access](#direct-vga-access)
18. [Game Loop Pattern](#game-loop-pattern)
19. [Printing Numbers](#printing-numbers)
20. [Complete Example: File Viewer](#complete-example-file-viewer)
21. [Debugging Tips](#debugging-tips)

---

## Quick Start: Hello World

Create a file called `programs/myprog.asm`:

```nasm
%include "syscalls.inc"

start:
        ; Print a string
        mov eax, SYS_PRINT
        mov ebx, message
        int 0x80

        ; Exit
        mov eax, SYS_EXIT
        xor ebx, ebx           ; exit code 0
        int 0x80

message db "Hello from my first Mellivora program!", 10, 0
```

Build and test:

```bash
make full
make run
```

In the Mellivora shell:

```text
HBDOS:/> myprog
Hello from my first Mellivora program!
HBDOS:/>
```

---

## Program Structure

Every Mellivora program follows this structure:

```nasm
%include "syscalls.inc"     ; MUST be the first line

start:                      ; Entry point (jumped to from syscalls.inc header)
        ; ... your code ...

        ; Always exit cleanly
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; Data section
my_string db "Hello", 10, 0

; BSS section (uninitialized data)
my_buffer resb 1024
```

### What `syscalls.inc` provides

The include file sets up the program header:

1. `[BITS 32]` — 32-bit code
2. `[ORG 0x00200000]` — programs load at 2 MB
3. `jmp start` — jumps past shared code to your `start:` label
4. All `SYS_*` constants (syscall numbers)
5. VGA constants (`VGA_BASE`, `VGA_WIDTH`, `VGA_HEIGHT`)
6. Key constants (`KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`)
7. Color constant (`COLOR_DEFAULT`)
8. File type constants (`FTYPE_TEXT`, `FTYPE_EXEC`, etc.)
9. `print_dec` — shared function to print EAX as decimal

### Important Rules

1. **Always `%include "syscalls.inc"` first** — it sets up ORG and the entry jump
2. **Your code starts at `start:`** — this label is required
3. **Always call `SYS_EXIT`** — or the exit trampoline will catch a bare `RET`
4. **You are in ring 3** — no direct I/O port access (use syscalls instead)
5. **Max program size: 1 MB** — code + data + BSS combined

---

## The syscalls.inc Include File

Here is a summary of everything `syscalls.inc` defines:

### Syscall Numbers

| Constant | Value | Purpose |
| --- | --- | --- |
| `SYS_EXIT` | 0 | Terminate program |
| `SYS_PUTCHAR` | 1 | Print one character |
| `SYS_GETCHAR` | 2 | Read one character (blocking) |
| `SYS_PRINT` | 3 | Print null-terminated string |
| `SYS_READ_KEY` | 4 | Non-blocking key poll |
| `SYS_OPEN` | 5 | Open file descriptor |
| `SYS_READ` | 6 | Read from file descriptor |
| `SYS_WRITE` | 7 | Write to file descriptor |
| `SYS_CLOSE` | 8 | Close file descriptor |
| `SYS_DELETE` | 9 | Delete a file |
| `SYS_SEEK` | 10 | Seek in file descriptor |
| `SYS_STAT` | 11 | Get file info |
| `SYS_MKDIR` | 12 | Create directory |
| `SYS_READDIR` | 13 | Read directory |
| `SYS_SETCURSOR` | 14 | Set cursor position |
| `SYS_GETTIME` | 15 | Get tick count |
| `SYS_SLEEP` | 16 | Sleep for N ticks |
| `SYS_CLEAR` | 17 | Clear screen |
| `SYS_SETCOLOR` | 18 | Set text color |
| `SYS_MALLOC` | 19 | Allocate memory |
| `SYS_FREE` | 20 | Free memory |
| `SYS_EXEC` | 21 | Execute program |
| `SYS_DISK_READ` | 22 | Raw disk read (kernel only) |
| `SYS_DISK_WRITE` | 23 | Raw disk write (kernel only) |
| `SYS_BEEP` | 24 | PC speaker beep |
| `SYS_DATE` | 25 | Get RTC date/time |
| `SYS_CHDIR` | 26 | Change directory |
| `SYS_GETCWD` | 27 | Get current directory |
| `SYS_SERIAL` | 28 | Print to serial port |
| `SYS_GETENV` | 29 | Get environment variable |
| `SYS_FREAD` | 30 | Read entire file |
| `SYS_FWRITE` | 31 | Write entire file |
| `SYS_GETARGS` | 32 | Get command-line arguments |

### Other Constants

| Constant | Value | Purpose |
| --- | --- | --- |
| `VGA_BASE` | `0xB8000` | VGA framebuffer address |
| `VGA_WIDTH` | 80 | Screen columns |
| `VGA_HEIGHT` | 25 | Screen rows |
| `KEY_UP` | `0x80` | Up arrow key code |
| `KEY_DOWN` | `0x81` | Down arrow key code |
| `KEY_LEFT` | `0x82` | Left arrow key code |
| `KEY_RIGHT` | `0x83` | Right arrow key code |
| `COLOR_DEFAULT` | `0x07` | Default text color (light gray on black) |

### Shared Function: `print_dec`

Prints the value in EAX as a decimal number. Preserves all registers (uses `pushad`/`popad`).

```nasm
        mov eax, 42
        call print_dec          ; prints "42"
```

---

## Building Programs

### Automatic Build

Place your `.asm` file in the `programs/` directory and run:

```bash
make full
```

The Makefile automatically discovers all `.asm` files in `programs/`, assembles them,
and includes them in the disk image.

### Manual Build

```bash
nasm -f bin -Iprograms/ -o programs/myprog.bin programs/myprog.asm
```

### Adding to the Disk Image

After building, run:

```bash
make populate
```

Or just use `make full` which does everything in one step.

---

## Register Conventions

### Syscall Calling Convention

| Register | Role |
| --- | --- |
| `EAX` | Syscall number (input) / return value (output) |
| `EBX` | First argument |
| `ECX` | Second argument |
| `EDX` | Third argument |
| `ESI` | Fourth argument |
| `EDI` | Fifth argument |

### General Rules

- **EAX is always clobbered** by syscalls (used for return value)
- Other registers are generally preserved across syscalls, but save anything critical
- Use `pushad`/`popad` to save/restore all registers in functions
- The stack is available and starts below the exit trampoline
- **Direction flag (DF)** should be clear (CLD) before string operations

---

## Console Output

### Print a Single Character

```nasm
        mov eax, SYS_PUTCHAR
        mov ebx, 'A'
        int 0x80
```

Special characters:
- `10` (0x0A) — newline
- `13` (0x0D) — carriage return
- `9` (0x09) — tab

### Print a String

```nasm
        mov eax, SYS_PRINT
        mov ebx, greeting
        int 0x80

greeting db "Welcome to Mellivora!", 10, 0
```

The string must be null-terminated (end with a `0` byte).

### Print a Newline

```nasm
        mov eax, SYS_PUTCHAR
        mov ebx, 10             ; newline
        int 0x80
```

### Print a Number

Use the shared `print_dec` function:

```nasm
        mov eax, 12345
        call print_dec          ; prints "12345"
```

### Print in Hex

Here's a reusable hex-print routine:

```nasm
print_hex:
        ; Print EAX as 8-digit hex
        pushad
        mov ecx, 8
.next:
        rol eax, 4
        mov ebx, eax
        and ebx, 0x0F
        add ebx, '0'
        cmp ebx, '9'
        jle .digit
        add ebx, 7             ; 'A'-'9'-1
.digit:
        push eax
        push ecx
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ecx
        pop eax
        dec ecx
        jnz .next
        popad
        ret
```

---

## Console Input

### Read a Character (Blocking)

Waits until the user presses a key:

```nasm
        mov eax, SYS_GETCHAR
        int 0x80
        ; AL now contains the ASCII code
```

### Poll for Key (Non-Blocking)

Returns immediately. EAX = 0 if no key is available:

```nasm
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .no_key
        ; AL = key code
.no_key:
```

This is essential for game loops where you don't want to block waiting for input.

### Read a Line of Text

Here's a reusable line-reading function:

```nasm
; Read a line into buffer at EDI, max ECX chars
; Returns length in EAX
read_line:
        push edi
        push ecx
        xor edx, edx           ; length counter
.loop:
        push edx
        push edi
        push ecx
        mov eax, SYS_GETCHAR
        int 0x80
        pop ecx
        pop edi
        pop edx

        cmp al, 10             ; Enter?
        je .done
        cmp al, 8              ; Backspace?
        je .backspace

        cmp edx, ecx           ; Buffer full?
        jge .loop

        stosb                   ; Store character
        inc edx

        ; Echo the character
        push edx
        push edi
        push ecx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ecx
        pop edi
        pop edx
        jmp .loop

.backspace:
        test edx, edx
        jz .loop
        dec edi
        dec edx
        ; Erase character on screen
        push edx
        push edi
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 8
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 8
        int 0x80
        pop ecx
        pop edi
        pop edx
        jmp .loop

.done:
        mov byte [edi], 0      ; Null-terminate
        mov eax, edx           ; Return length
        pop ecx
        pop edi
        ret
```

---

## Color and Screen Control

### Set Text Color

```nasm
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E          ; Yellow on black
        int 0x80
```

Color attribute format: `[background:4 bits][foreground:4 bits]`

| Bit 7–4 (Background) | Bit 3–0 (Foreground) |
| --- | --- |
| 0 = Black | 0 = Black |
| 1 = Blue | 1 = Blue |
| 2 = Green | 2 = Green |
| 3 = Cyan | 3 = Cyan |
| 4 = Red | 4 = Red |
| 5 = Magenta | 5 = Magenta |
| 6 = Brown | 6 = Brown |
| 7 = Light Gray | 7 = Light Gray |
| — | 8 = Dark Gray |
| — | 9 = Light Blue |
| — | A = Light Green |
| — | B = Light Cyan |
| — | C = Light Red |
| — | D = Light Magenta |
| — | E = Yellow |
| — | F = White |

> Background colors 8–F produce blinking text on real hardware but may show bright
> backgrounds in QEMU.

### Example: Colored Output

```nasm
        ; Print in red
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C          ; Light red on black
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, error_msg
        int 0x80

        ; Reset to default
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

error_msg db "Error: file not found!", 10, 0
```

### Clear Screen

```nasm
        mov eax, SYS_CLEAR
        int 0x80
```

### Set Cursor Position

```nasm
        mov eax, SYS_SETCURSOR
        mov ebx, 10            ; column (0–79)
        mov ecx, 5             ; row (0–24)
        int 0x80
```

---

## File I/O with File Descriptors

### Open, Read, Close

```nasm
        ; Open a file
        mov eax, SYS_OPEN
        mov ebx, filename
        mov ecx, 1             ; mode: 1=read
        int 0x80
        cmp eax, -1
        je .error
        mov [fd], eax           ; Save file descriptor

        ; Read up to 4096 bytes
        mov eax, SYS_READ
        mov ebx, [fd]
        mov ecx, buffer
        mov edx, 4096
        int 0x80
        ; EAX = bytes actually read (0=EOF, -1=error)
        mov [bytes_read], eax

        ; Close the file
        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

filename db "readme.txt", 0
fd       dd 0
bytes_read dd 0
buffer   resb 4096
```

### Open, Write, Close

```nasm
        ; Open for writing
        mov eax, SYS_OPEN
        mov ebx, filename
        mov ecx, 2             ; mode: 2=write
        int 0x80
        cmp eax, -1
        je .error
        mov [fd], eax

        ; Write data
        mov eax, SYS_WRITE
        mov ebx, [fd]
        mov ecx, data
        mov edx, data_len
        int 0x80

        ; Close
        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

filename db "output.txt", 0
data     db "Data written by program", 10
data_len equ $ - data
fd       dd 0
```

### Seek

```nasm
        ; Seek to beginning (rewind)
        mov eax, SYS_SEEK
        mov ebx, [fd]
        mov ecx, 0             ; offset
        mov edx, 0             ; whence: 0=SEEK_SET
        int 0x80

        ; Seek to end
        mov eax, SYS_SEEK
        mov ebx, [fd]
        mov ecx, 0
        mov edx, 2             ; whence: 2=SEEK_END
        int 0x80
        ; EAX = file size (new position at end)
```

---

## Whole-File I/O

For simple programs, whole-file I/O is easier than file descriptors:

### Read Entire File

```nasm
        mov eax, SYS_FREAD
        mov ebx, filename       ; Filename string
        mov ecx, buffer         ; Destination buffer
        int 0x80
        ; EAX = bytes read (0 if file not found)

filename db "data.txt", 0
buffer   resb 8192
```

### Write Entire File

```nasm
        mov eax, SYS_FWRITE
        mov ebx, filename       ; Filename string
        mov ecx, data           ; Source buffer
        mov edx, data_len       ; Size in bytes
        int 0x80
        ; EAX = 0 success, -1 error

filename db "output.txt", 0
data     db "File contents here", 10
data_len equ $ - data
```

---

## Command-Line Arguments

Programs can receive arguments from the shell. When a user types:

```text
HBDOS:/> myprog hello world
```

The string `"hello world"` is available via `SYS_GETARGS`.

### Reading Arguments

```nasm
%include "syscalls.inc"

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        ; EAX = length of argument string (0 if none)

        test eax, eax
        jz .no_args

        ; Print the arguments
        mov eax, SYS_PRINT
        mov ebx, got_args
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, arg_buf
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .done

.no_args:
        mov eax, SYS_PRINT
        mov ebx, no_args
        int 0x80

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

got_args db "Arguments: ", 0
no_args  db "No arguments provided.", 10, 0
arg_buf  resb 512
```

---

## Memory Allocation

### Allocate Memory

```nasm
        mov eax, SYS_MALLOC
        mov ebx, 8192          ; Request 8 KB
        int 0x80
        ; EAX = physical address of allocated memory (0 if failed)
        ; Note: actual allocation is rounded up to 4 KB pages
        test eax, eax
        jz .out_of_memory
        mov [my_buffer], eax
```

### Free Memory

```nasm
        mov eax, SYS_FREE
        mov ebx, [my_buffer]   ; Address returned by SYS_MALLOC
        mov ecx, 8192          ; Same size as allocated
        int 0x80
```

> **Note:** Allocation is in 4 KB page granularity. Requesting 100 bytes still allocates
> a full 4 KB page.

---

## Timing and Delays

### Get Current Time

```nasm
        mov eax, SYS_GETTIME
        int 0x80
        ; EAX = tick count since boot (100 ticks = 1 second)
```

### Sleep

```nasm
        mov eax, SYS_SLEEP
        mov ebx, 100            ; Sleep for 1 second (100 ticks)
        int 0x80
```

### Measuring Elapsed Time

```nasm
        ; Record start time
        mov eax, SYS_GETTIME
        int 0x80
        mov [start_time], eax

        ; ... do some work ...

        ; Calculate elapsed
        mov eax, SYS_GETTIME
        int 0x80
        sub eax, [start_time]
        ; EAX = elapsed ticks
        ; Divide by 100 for seconds
        xor edx, edx
        mov ecx, 100
        div ecx
        ; EAX = seconds

start_time dd 0
```

---

## Sound

### Play a Tone

```nasm
        mov eax, SYS_BEEP
        mov ebx, 440            ; Frequency in Hz (A4 note)
        mov ecx, 50             ; Duration in ticks (0.5 seconds)
        int 0x80
```

### Silence

```nasm
        mov eax, SYS_BEEP
        mov ebx, 0              ; Frequency 0 = turn off speaker
        mov ecx, 0
        int 0x80
```

### Musical Notes

| Note | Frequency (Hz) |
| --- | --- |
| C4 | 262 |
| D4 | 294 |
| E4 | 330 |
| F4 | 349 |
| G4 | 392 |
| A4 | 440 |
| B4 | 494 |
| C5 | 523 |

---

## Date and Time

### Read the Real-Time Clock

```nasm
        mov eax, SYS_DATE
        mov ebx, date_buf       ; 6-byte buffer
        int 0x80
        ; EAX = full year (e.g., 2025)
        ; date_buf[0] = seconds (0–59)
        ; date_buf[1] = minutes (0–59)
        ; date_buf[2] = hours (0–23)
        ; date_buf[3] = day of month (1–31)
        ; date_buf[4] = month (1–12)
        ; date_buf[5] = year (2-digit, e.g., 25)

date_buf resb 6
```

---

## Environment Variables

### Read an Environment Variable

```nasm
        mov eax, SYS_GETENV
        mov ebx, var_name
        int 0x80
        ; EAX = pointer to value string (or 0 if not set)
        test eax, eax
        jz .not_set

        ; Print the value
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80

var_name db "user", 0
```

> **Note:** There is no `SYS_SETENV` syscall. Environment variables are set from the
> shell using the `set` command.

---

## Direct VGA Access

For games and graphical programs, you can write directly to the VGA framebuffer.

### VGA Text Mode Layout

The framebuffer starts at `VGA_BASE` (0xB8000). Each cell is 2 bytes:

```text
Byte 0: ASCII character
Byte 1: Color attribute [bg:4][fg:4]
```

Screen coordinates map to offsets as:
```text
offset = (row × VGA_WIDTH + column) × 2
```

### Write a Character at Position

```nasm
; Put character AL with color AH at column BL, row BH
put_char_at:
        pushad
        movzx edi, bh
        imul edi, VGA_WIDTH
        movzx ebx, bl
        add edi, ebx
        shl edi, 1
        add edi, VGA_BASE
        mov [edi], ax           ; Write char + color
        popad
        ret
```

### Fill Screen with Color

```nasm
        mov edi, VGA_BASE
        mov ax, 0x1F20          ; Space character, white on blue
        mov ecx, VGA_WIDTH * VGA_HEIGHT
        rep stosw
```

### Draw a Border

```nasm
; Draw a box using box-drawing characters
draw_border:
        ; Top-left corner
        mov al, 0xC9            ; ╔
        mov ah, 0x0F            ; White on black
        mov bl, 0               ; Column 0
        mov bh, 0               ; Row 0
        call put_char_at

        ; Top border
        mov ecx, 78
        mov bl, 1
.top:
        mov al, 0xCD            ; ═
        call put_char_at
        inc bl
        dec ecx
        jnz .top

        ; Top-right corner
        mov al, 0xBB            ; ╗
        call put_char_at
        ret
```

---

## Game Loop Pattern

Most games in Mellivora follow this pattern:

```nasm
%include "syscalls.inc"

start:
        call game_init

.game_loop:
        ; 1. Poll for input (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .no_input
        call handle_input
        cmp byte [game_over], 1
        je .exit
.no_input:

        ; 2. Update game state
        call update_game

        ; 3. Render
        call draw_screen

        ; 4. Frame delay (100ms = 10 ticks for ~10 FPS)
        mov eax, SYS_SLEEP
        mov ebx, 10
        int 0x80

        ; 5. Check for game over
        cmp byte [game_over], 1
        jne .game_loop

.exit:
        ; Restore default color
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

game_over db 0
```

### Handling Arrow Keys

```nasm
handle_input:
        cmp al, 0x1B            ; ESC
        je .quit
        cmp al, KEY_UP
        je .move_up
        cmp al, KEY_DOWN
        je .move_down
        cmp al, KEY_LEFT
        je .move_left
        cmp al, KEY_RIGHT
        je .move_right
        ret

.quit:
        mov byte [game_over], 1
        ret
.move_up:
        dec byte [player_y]
        ret
.move_down:
        inc byte [player_y]
        ret
.move_left:
        dec byte [player_x]
        ret
.move_right:
        inc byte [player_x]
        ret
```

### Variable Frame Rate

For smoother gameplay, track elapsed time instead of using fixed delays:

```nasm
        ; Get time at frame start
        mov eax, SYS_GETTIME
        int 0x80
        mov [frame_start], eax

        ; ... process input, update, render ...

        ; Calculate remaining time for this frame
        mov eax, SYS_GETTIME
        int 0x80
        sub eax, [frame_start]
        mov ebx, 10             ; Target: 10 ticks per frame
        sub ebx, eax
        jle .no_sleep           ; Already took too long
        mov eax, SYS_SLEEP
        int 0x80
.no_sleep:

frame_start dd 0
```

---

## Printing Numbers

### Decimal (provided by syscalls.inc)

```nasm
        mov eax, 42
        call print_dec          ; prints "42"
```

### With Leading Text

```nasm
        mov eax, SYS_PRINT
        mov ebx, score_label
        int 0x80

        mov eax, [score]
        call print_dec

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

score_label db "Score: ", 0
score dd 0
```

### Signed Numbers

`print_dec` handles unsigned values. For signed numbers:

```nasm
print_signed:
        test eax, eax
        jns .positive
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop eax
        neg eax
.positive:
        call print_dec
        ret
```

---

## Complete Example: File Viewer

A program that reads a file and displays it with line numbers:

```nasm
%include "syscalls.inc"

start:
        ; Get filename from arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        ; Read the file
        mov eax, SYS_FREAD
        mov ebx, arg_buf
        mov ecx, file_buf
        int 0x80
        test eax, eax
        jz .not_found
        mov [file_size], eax

        ; Display with line numbers
        mov esi, file_buf
        mov dword [line_num], 1

.print_line:
        ; Print line number
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80

        mov eax, [line_num]
        call print_dec

        mov eax, SYS_PRINT
        mov ebx, separator
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

        ; Print characters until newline or end
.print_char:
        lodsb
        test al, al
        jz .done
        cmp al, 10
        je .newline

        push esi
        mov eax, SYS_PUTCHAR
        movzx ebx, al
        int 0x80
        pop esi
        jmp .print_char

.newline:
        push esi
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop esi
        inc dword [line_num]
        jmp .print_line

.done:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_msg
        int 0x80
        jmp .exit

.not_found:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_msg
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; Data
separator db ": ", 0
usage_msg db "Usage: view <filename>", 10, 0
err_msg   db "Error: file not found!", 10, 0

; BSS
line_num  dd 0
file_size dd 0
arg_buf   resb 512
file_buf  resb 65536
```

---

## Debugging Tips

### Serial Output

Use `SYS_SERIAL` to send debug messages to the serial port. View them by running QEMU
with `-serial stdio`:

```nasm
        mov eax, SYS_SERIAL
        mov ebx, debug_msg
        int 0x80

debug_msg db "[DEBUG] Reached checkpoint 1", 10, 0
```

### Check Return Values

Always check syscall return values:

```nasm
        mov eax, SYS_OPEN
        mov ebx, filename
        mov ecx, 1
        int 0x80
        cmp eax, -1
        je .handle_error        ; Don't ignore this!
```

### Print Register Values

```nasm
; Debug: print EAX value
debug_print_eax:
        pushad
        push eax                ; Save value
        mov eax, SYS_PRINT
        mov ebx, dbg_prefix
        int 0x80
        pop eax
        call print_dec
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop eax
        popad
        ret

dbg_prefix db "EAX=", 0
```

### Common Mistakes

1. **Forgetting to null-terminate strings** — `SYS_PRINT` reads until `\0`
2. **Not preserving registers** — Syscalls clobber EAX; save it if needed
3. **Buffer overflows** — Always check sizes before copying
4. **Missing `SYS_EXIT`** — Without it, execution falls into garbage after your code
5. **Using `SYS_DISK_READ`/`SYS_DISK_WRITE`** — These are blocked for ring 3 programs;
   use file I/O syscalls instead
6. **Writing past program space** — Programs have 1 MB; plan BSS accordingly

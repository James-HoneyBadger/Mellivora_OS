# Mellivora OS — Programming Guide

This guide teaches you how to write programs for Mellivora OS in both x86 assembly and
C. It covers the syscall interface, program structure, common patterns, and the TCC
compiler.

---

## Table of Contents

1. [Program Environment](#program-environment)
2. [Your First Assembly Program](#your-first-assembly-program)
3. [Syscall Reference](#syscall-reference)
4. [Console I/O](#console-io)
5. [File I/O](#file-io)
6. [Screen Control](#screen-control)
7. [Keyboard Input](#keyboard-input)
8. [Timing & Sound](#timing--sound)
9. [Memory Allocation](#memory-allocation)
10. [Directory Operations](#directory-operations)
11. [Serial Port I/O](#serial-port-io)
12. [Environment & Arguments](#environment--arguments)
13. [Networking](#networking)
14. [GUI Programming](#gui-programming)
15. [Game Loop Pattern](#game-loop-pattern)
16. [Building Assembly Programs](#building-assembly-programs)
17. [C Programming with TCC](#c-programming-with-tcc)
18. [Debugging Tips](#debugging-tips)
19. [Complete Syscall Table](#complete-syscall-table)

---

## Program Environment

### Memory Layout

Programs are loaded at `0x00200000` (2 MB) and run in Ring 3 (user mode).

| Address | Purpose |
| --- | --- |
| `0x00200000` | Program load address (code + data) |
| `0x002FFFF0` | SYS_EXIT trampoline (safety net) |
| `0x002FFFEC` | Initial stack pointer (grows downward) |

### Execution Model

- **Single-tasking:** Only one program runs at a time
- **Flat memory:** No paging, no memory protection between program sections
- **Ring 3:** User privilege level — no direct port I/O or privileged instructions
- **Syscall interface:** All OS services via `INT 0x80`
- **Exit methods:** Call `SYS_EXIT` (syscall 0), or simply `RET` (hits trampoline)

### Register Conventions

| Register | Usage |
| --- | --- |
| RAX | Syscall number / return value |
| RBX | First argument |
| RCX | Second argument |
| RDX | Third argument |
| RSI | Fourth argument |
| RDI | Fifth argument / secondary return |
| RSP | Stack pointer (program's own stack) |

---

## Your First Assembly Program

### hello.asm — Minimal Example

```nasm
; hello.asm — Hello World for Mellivora OS
%include "syscalls.inc"

    ; Print a string
    mov rax, 3          ; SYS_PRINT
    mov rbx, message    ; pointer to null-terminated string
    int 0x80

    ; Exit cleanly
    mov rax, 0          ; SYS_EXIT
    xor rbx, rbx       ; exit code 0
    int 0x80

message: db "Hello, World!", 10, 0
```

### Building and Running

```bash
nasm -f bin -O0 -o hello hello.asm
```

Then copy `hello` to the disk image and run it from the shell:

```text
Lair:/> hello
Hello, World!
Lair:/>
```

### Key Points

- **`%include "syscalls.inc"`**: Provides `BITS 64`, `ORG 0x200000`, and all syscall constants
- **`INT 0x80`**: All OS services go through this interrupt
- **`SYS_EXIT` (RAX=0)**: Always exit cleanly, or the trampoline does it for you
- **`-O0`**: Disable NASM optimizations (critical — prevents short jump issues)

---

## Syscall Reference

Every syscall uses the same convention:

```nasm
mov rax, SYSCALL_NUMBER
mov rbx, arg1
mov rcx, arg2
mov rdx, arg3
int 0x80
; Return value in RAX (and sometimes RCX, RDI)
```

### Syscall Numbers

```nasm
; Define these at the top of your program, or %include "syscalls.inc"
SYS_EXIT        equ 0
SYS_PUTCHAR     equ 1
SYS_GETCHAR     equ 2
SYS_PRINT       equ 3
SYS_READ_KEY    equ 4
SYS_OPEN        equ 5
SYS_READ        equ 6
SYS_WRITE       equ 7
SYS_CLOSE       equ 8
SYS_DELETE      equ 9
SYS_SEEK        equ 10
SYS_STAT        equ 11
SYS_MKDIR       equ 12
SYS_READDIR     equ 13
SYS_SETCURSOR   equ 14
SYS_GETTIME     equ 15
SYS_SLEEP       equ 16
SYS_CLEAR       equ 17
SYS_SETCOLOR    equ 18
SYS_MALLOC      equ 19
SYS_FREE        equ 20
SYS_EXEC        equ 21
SYS_DISK_READ   equ 22
SYS_DISK_WRITE  equ 23
SYS_BEEP        equ 24
SYS_DATE        equ 25
SYS_CHDIR       equ 26
SYS_GETCWD      equ 27
SYS_SERIAL      equ 28
SYS_GETENV      equ 29
SYS_FREAD       equ 30
SYS_FWRITE      equ 31
SYS_GETARGS     equ 32
SYS_SERIAL_IN   equ 33
SYS_STDIN_READ  equ 34
SYS_YIELD       equ 35
SYS_MOUSE       equ 36
SYS_FRAMEBUF    equ 37
SYS_GUI         equ 38
SYS_SOCKET      equ 39
SYS_CONNECT     equ 40
SYS_SEND        equ 41
SYS_RECV        equ 42
SYS_BIND        equ 43
SYS_LISTEN      equ 44
SYS_ACCEPT      equ 45
SYS_DNS         equ 46
SYS_SOCKCLOSE   equ 47
SYS_PING        equ 48
SYS_SETDATE     equ 49
SYS_AUDIO_PLAY  equ 50
SYS_AUDIO_STOP  equ 51
SYS_AUDIO_STATUS equ 52
SYS_KILL        equ 53
SYS_GETPID      equ 54
SYS_CLIP_COPY   equ 55
SYS_CLIP_PASTE  equ 56
SYS_NOTIFY      equ 57
SYS_FILE_OPEN_DLG equ 58
SYS_FILE_SAVE_DLG equ 59
SYS_PIPE_CREATE equ 60
SYS_PIPE_WRITE  equ 61
SYS_PIPE_READ   equ 62
SYS_PIPE_CLOSE  equ 63
SYS_SHMGET      equ 64
SYS_SHMADDR     equ 65
SYS_PROCLIST    equ 66
SYS_MEMINFO     equ 67
SYS_CHMOD       equ 68
SYS_CHOWN       equ 69
SYS_SYMLINK     equ 70
SYS_READLINK    equ 71
SYS_SIGNAL      equ 72
SYS_RAISE       equ 73
SYS_MQ_CREATE   equ 74
SYS_MQ_SEND     equ 75
SYS_MQ_RECV     equ 76
SYS_MQ_CLOSE    equ 77
SYS_STRACE      equ 78
SYS_LISTENV     equ 79
SYS_RENAME      equ 80
SYS_SETENV      equ 81
SYS_RMDIR       equ 82
SYS_TRUNCATE    equ 83
```

Or include the provided header:

```nasm
%include "syscalls.inc"
```

---

## Console I/O

### Print a String

```nasm
mov rax, 3          ; SYS_PRINT
mov rbx, msg        ; pointer to null-terminated string
int 0x80

msg: db "Hello!", 10, 0    ; 10 = newline
```

### Print a Single Character

```nasm
mov rax, 1          ; SYS_PUTCHAR
mov rbx, 'A'        ; character to print
int 0x80
```

### Read a Character (Blocking)

```nasm
mov rax, 2          ; SYS_GETCHAR
int 0x80
; RAX now contains the ASCII code of the key pressed
```

### Read a String (Character by Character)

```nasm
read_line:
    mov rdi, buffer
    xor rcx, rcx       ; character count

.loop:
    mov rax, 2          ; SYS_GETCHAR
    int 0x80

    cmp al, 10          ; Enter?
    je .done
    cmp al, 13
    je .done

    stosb               ; store char and advance RDI
    inc rcx

    mov rax, 1          ; echo it back
    mov rbx, rax
    movzx rbx, al
    int 0x80

    cmp rcx, 255        ; buffer limit
    jb .loop

.done:
    mov byte [rdi], 0   ; null-terminate
    ret

buffer: times 256 db 0
```

### Print a Decimal Number

```nasm
; Print the number in RAX as decimal
print_number:
    push rbp
    mov rbp, rsp
    sub rsp, 12         ; buffer on stack
    mov rdi, rbp
    dec rdi
    mov byte [rdi], 0   ; null terminator

    test rax, rax
    jnz .convert
    dec rdi
    mov byte [rdi], '0'
    jmp .print

.convert:
    mov rcx, 10
.digit:
    test rax, rax
    jz .print
    xor rdx, rdx
    div rcx             ; RAX/10, remainder in RDX
    add dl, '0'
    dec rdi
    mov [rdi], dl
    jmp .digit

.print:
    mov rax, 3          ; SYS_PRINT
    mov rbx, rdi
    int 0x80
    leave
    ret
```

---

## File I/O

### Simple File Read (Recommended)

The easiest way to read a file — one syscall, returns entire contents:

```nasm
mov rax, 30         ; SYS_FREAD
mov rbx, filename   ; filename (can include path: "/docs/readme")
mov rcx, buffer     ; destination buffer
int 0x80
; RAX = bytes read (0 if file not found)

filename: db "readme", 0
buffer: times 65536 db 0
```

### Simple File Write

```nasm
mov rax, 31         ; SYS_FWRITE
mov rbx, filename   ; filename
mov rcx, data       ; source buffer
mov rdx, data_len   ; byte count
int 0x80
; RAX = 0 on success, -1 on failure

filename: db "output.txt", 0
data: db "Hello, file!", 10
data_len equ $ - data
```

### File Descriptor API (Open/Read/Write/Close)

For more control, use the fd-based API:

```nasm
; Open file for reading
mov rax, 5          ; SYS_OPEN
mov rbx, filename   ; filename
mov rcx, 1          ; mode: 1=read, 2=write
int 0x80
; RAX = file descriptor (-1 on error)
mov [fd], rax

; Read up to 1024 bytes
mov rax, 6          ; SYS_READ
mov rbx, [fd]       ; file descriptor
mov rcx, buffer     ; destination
mov rdx, 1024       ; max bytes
int 0x80
; RAX = bytes actually read

; Seek to offset
mov rax, 10         ; SYS_SEEK
mov rbx, [fd]
mov rcx, 0          ; offset from start
int 0x80

; Close file
mov rax, 8          ; SYS_CLOSE
mov rbx, [fd]
int 0x80

fd: dd 0
filename: db "myfile.txt", 0
buffer: times 1024 db 0
```

### Check if File Exists (STAT)

```nasm
mov rax, 11         ; SYS_STAT
mov rbx, filename   ; filename
int 0x80
; RAX = file size in bytes (-1 if not found)
; RCX = block count

cmp rax, -1
je .not_found
; File exists, RAX = size
```

### Delete a File

```nasm
mov rax, 9          ; SYS_DELETE
mov rbx, filename
int 0x80
; RAX = 0 success, -1 failure
```

### Read Files from Other Directories

`SYS_FREAD` supports full paths:

```nasm
mov rax, 30
mov rbx, path
mov rcx, buffer
int 0x80

path: db "/docs/readme", 0       ; absolute path
; or:  db "../docs/readme", 0    ; relative path
```

---

## Screen Control

### Clear Screen

```nasm
mov rax, 17         ; SYS_CLEAR
int 0x80
```

### Set Cursor Position

```nasm
mov rax, 14         ; SYS_SETCURSOR
mov rbx, 10         ; column (0–79)
mov rcx, 5          ; row (0–24)
int 0x80
```

### Set Text Color

```nasm
mov rax, 18         ; SYS_SETCOLOR
mov rbx, 0x0A       ; light green on black
int 0x80
```

Color byte: high nibble = background, low nibble = foreground.

### Direct VGA Access

For performance-critical rendering (games), write directly to VGA memory:

```nasm
VGA_BASE equ 0xB8000

; Write 'X' at column 10, row 5 in red
mov rdi, VGA_BASE
mov rax, 5
imul rax, 160       ; row * 80 * 2
add rax, 20         ; col * 2
add rdi, rax
mov word [rdi], 0x0C58   ; 0x0C = red, 'X' = 0x58
```

**Warning:** VGA writes are safe from Ring 3 because the flat memory model maps all
physical memory. But use syscalls for general output — direct VGA is only needed for
games and animations that must update many cells per frame.

### Draw a Colored Box

```nasm
; Draw a 20×5 box at (10, 3) with blue background
draw_box:
    mov rcx, 5          ; height
    mov rdx, 3          ; start row

.row:
    push rcx
    mov rax, 14         ; SYS_SETCURSOR
    mov rbx, 10         ; start column
    mov rcx, rdx
    int 0x80

    mov rcx, 20         ; width
.col:
    mov rax, 1          ; SYS_PUTCHAR
    mov rbx, ' '
    int 0x80
    dec rcx
    jnz .col

    inc rdx
    pop rcx
    dec rcx
    jnz .row
    ret
```

---

## Keyboard Input

### Blocking Read

```nasm
mov rax, 2          ; SYS_GETCHAR
int 0x80
; Waits until a key is pressed, returns ASCII in RAX
```

### Non-Blocking Poll

```nasm
mov rax, 4          ; SYS_READ_KEY
int 0x80
; RAX = ASCII code, or 0 if no key pending
test rax, rax
jz .no_key
; Process key in RAX
```

### Arrow Key Codes

| Code | Key |
| --- | --- |
| `0x80` | Up Arrow |
| `0x81` | Down Arrow |
| `0x82` | Left Arrow |
| `0x83` | Right Arrow |

### Reading Arrow Keys

```nasm
poll_input:
    mov rax, 4          ; SYS_READ_KEY
    int 0x80
    test rax, rax
    jz .no_input

    cmp al, 0x80        ; Up
    je .move_up
    cmp al, 0x81        ; Down
    je .move_down
    cmp al, 0x82        ; Left
    je .move_left
    cmp al, 0x83        ; Right
    je .move_right
    cmp al, 27          ; ESC
    je .quit
    cmp al, ' '         ; Space
    je .action
    jmp .no_input
```

---

## Timing & Sound

### Get Current Time

```nasm
mov rax, 15         ; SYS_GETTIME
int 0x80
; RAX = tick_count (100 ticks = 1 second)
```

### Sleep

```nasm
mov rax, 16         ; SYS_SLEEP
mov rbx, 50         ; sleep for 50 ticks (0.5 seconds)
int 0x80
```

### Frame Rate Control

```nasm
game_loop:
    mov rax, 15         ; SYS_GETTIME
    int 0x80
    mov [frame_start], rax

    ; ... game logic and rendering ...

    ; Wait for next frame (target: 10 FPS = 10 ticks per frame)
    mov rax, 15
    int 0x80
    sub rax, [frame_start]
    cmp rax, 10
    jae .no_wait
    mov rbx, 10
    sub rbx, rax
    mov rax, 16         ; SYS_SLEEP
    int 0x80
.no_wait:
    jmp game_loop

frame_start: dd 0
```

### Play a Tone

```nasm
mov rax, 24         ; SYS_BEEP
mov rbx, 440        ; frequency in Hz (440 = A4)
mov rcx, 20         ; duration in ticks (200ms)
int 0x80
```

### Stop Sound

```nasm
mov rax, 24         ; SYS_BEEP
xor rbx, rbx       ; frequency 0 = silence
xor rcx, rcx
int 0x80
```

### Musical Scale Example

```nasm
; Play C major scale
play_scale:
    mov rsi, notes
    mov rcx, 8

.play:
    push rcx
    movzx rbx, word [rsi]  ; frequency
    mov rax, 24             ; SYS_BEEP
    mov rcx, 15             ; duration
    int 0x80

    mov rax, 16             ; SYS_SLEEP
    mov rbx, 20             ; gap between notes
    int 0x80

    add rsi, 2
    pop rcx
    dec rcx
    jnz .play
    ret

notes: dw 262, 294, 330, 349, 392, 440, 494, 523  ; C4 to C5
```

---

## Memory Allocation

### Allocate Memory

Memory is allocated in 4 KB page granularity:

```nasm
mov rax, 19         ; SYS_MALLOC
mov rbx, 8192       ; request 8192 bytes (gets 2 pages = 8 KB)
int 0x80
; RAX = physical address of allocated memory (0 = failure)
test rax, rax
jz .out_of_memory
mov [my_buffer], rax
```

### Free Memory

```nasm
mov rax, 20         ; SYS_FREE
mov rbx, [my_buffer]   ; address returned by SYS_MALLOC
mov rcx, 8192          ; same size as allocated
int 0x80
```

### Important Notes

- Minimum allocation is 4 KB (one page), regardless of requested size
- All sizes are rounded up to the next 4 KB boundary
- There is no heap — allocations come directly from the physical page allocator
- Always free what you allocate to avoid leaking pages

---

## Directory Operations

### Create a Directory

```nasm
mov rax, 12         ; SYS_MKDIR
mov rbx, dirname
int 0x80
; RAX = 0 success, -1 failure

dirname: db "mydir", 0
```

### Change Directory

```nasm
mov rax, 26         ; SYS_CHDIR
mov rbx, dirname
int 0x80
; RAX = 0 success, -1 failure
```

### Get Current Directory

```nasm
mov rax, 27         ; SYS_GETCWD
mov rbx, cwd_buf
int 0x80

cwd_buf: times 256 db 0
```

### List Directory Entries

```nasm
list_files:
    xor rcx, rcx       ; entry index

.next:
    push rcx
    mov rax, 13         ; SYS_READDIR
    mov rbx, name_buf   ; buffer for entry name
    int 0x80
    ; RAX = file type (0 = end of directory)
    ; RCX = file size

    test rax, rax
    jz .done

    ; Print the filename
    push rax
    mov rax, 3          ; SYS_PRINT
    mov rbx, name_buf
    int 0x80
    mov rax, 1
    mov rbx, 10         ; newline
    int 0x80
    pop rax

    pop rcx
    inc rcx
    jmp .next

.done:
    pop rcx
    ret

name_buf: times 256 db 0
```

---

## Serial Port I/O

### Write to Serial Port

Useful for debugging — output appears in the host terminal (QEMU `-serial stdio`):

```nasm
mov rax, 28         ; SYS_SERIAL
mov rbx, debug_msg
int 0x80

debug_msg: db "[DEBUG] Reached checkpoint 1", 10, 0
```

### Read from Serial Port

```nasm
mov rax, 33         ; SYS_SERIAL_IN
int 0x80
; RAX = character received from serial port
```

---

## Environment & Arguments

### Get Command-Line Arguments

```nasm
mov rax, 32         ; SYS_GETARGS
mov rbx, args_buf   ; buffer (max 512 bytes)
int 0x80
; RAX = length of argument string
; args_buf contains everything after the program name

args_buf: times 512 db 0
```

### Get an Environment Variable

```nasm
mov rax, 29         ; SYS_GETENV
mov rbx, var_name
int 0x80
; RDI = pointer to value string (inside kernel's env table)
; If not found, RDI is undefined — check before using

var_name: db "PATH", 0
```

---

## Networking

Mellivora provides 10 networking syscalls (39–48) for socket operations, DNS resolution,
and ICMP ping. The `programs/lib/net.inc` library wraps these into convenient functions.

### Creating a TCP Connection

```nasm
%include "syscalls.inc"
%include "lib/net.inc"

start:
        ; 1. Resolve hostname to IP
        mov rsi, hostname
        call net_dns            ; RAX = IP address (0 = failed)
        test rax, rax
        jz .error
        mov [server_ip], rax

        ; 2. Create a TCP socket
        mov rax, NET_TCP        ; NET_TCP = 1
        call net_socket         ; RAX = socket fd (-1 = error)
        cmp rax, -1
        je .error
        mov [sockfd], rax

        ; 3. Connect to port 80
        mov rax, [sockfd]
        mov rbx, [server_ip]
        mov rcx, 80
        call net_connect        ; RAX = 0 (success) or -1 (error)
        cmp rax, -1
        je .error

        ; 4. Send data
        mov rax, [sockfd]
        mov rsi, message
        call net_send_line      ; Sends string + CRLF

        ; 5. Receive response
        mov rax, [sockfd]
        mov rbx, recv_buf
        mov rcx, 512
        call net_recv           ; RAX = bytes (0=no data, -1=closed)

        ; 6. Close socket
        mov rax, [sockfd]
        call net_close

.error:
        mov rax, SYS_EXIT
        xor rbx, rbx
        int 0x80

hostname:  db "example.com", 0
message:   db "GET / HTTP/1.0", 0

section .bss
server_ip: resd 1
sockfd:    resd 1
recv_buf:  resb 513
```

### Socket Lifecycle

1. **Create** → `net_socket` with `NET_TCP` (1) or `NET_UDP` (2)
2. **Connect** → `net_connect` with IP and port (for clients)
3. **Send/Receive** → `net_send`, `net_recv`, `net_send_line`, `net_recv_line`
4. **Close** → `net_close`

For servers: `net_bind` → `net_listen` → `net_accept` (returns new socket fd)

### Sending Line-Oriented Protocols

Many text protocols (HTTP, FTP, SMTP, NNTP) use CRLF-terminated lines:

```nasm
        ; net_send_line appends \r\n automatically
        mov rax, [sockfd]
        mov rsi, helo_cmd
        call net_send_line      ; Sends "HELO mellivora\r\n"

        ; net_recv_line reads until \n and null-terminates
        mov rax, [sockfd]
        mov rdi, response_buf
        mov rcx, 256
        call net_recv_line      ; RAX = bytes, buffer is null-terminated

helo_cmd: db "HELO mellivora", 0
```

### DNS Resolution

```nasm
        mov rsi, hostname
        call net_dns
        test rax, rax
        jz .dns_failed          ; 0 = resolution failed
        ; RAX = 32-bit IP address in little-endian
```

The kernel maintains an 8-entry DNS cache. Repeated lookups for the same hostname
return the cached result without a network request.

### ICMP Ping

```nasm
        mov rax, [target_ip]
        call net_ping           ; RAX = RTT in timer ticks, or -1 = timeout
        cmp rax, -1
        je .timed_out
```

### Parsing IP Addresses

```nasm
        mov rsi, ip_string
        call net_parse_ip       ; RAX = 32-bit IP (0 = parse error)

ip_string: db "10.0.2.2", 0
```

### Networking Syscall Reference

| Syscall | # | RBX | RCX | RDX | RAX Return |
| --- | --- | --- | --- | --- | --- |
| SYS_SOCKET | 39 | type (1/2) | — | — | fd or -1 |
| SYS_CONNECT | 40 | fd | IP | port | 0 or -1 |
| SYS_SEND | 41 | fd | buffer | length | bytes or -1 |
| SYS_RECV | 42 | fd | buffer | max len | bytes, 0, -1 |
| SYS_BIND | 43 | fd | port | — | 0 or -1 |
| SYS_LISTEN | 44 | fd | — | — | 0 or -1 |
| SYS_ACCEPT | 45 | fd | — | — | new fd or -1 |
| SYS_DNS | 46 | hostname | — | — | IP or 0 |
| SYS_SOCKCLOSE | 47 | fd | — | — | 0 |
| SYS_PING | 48 | IP | — | — | RTT or -1 |

---

## GUI Programming

Mellivora's Burrows desktop environment provides a windowed GUI accessible through
`SYS_GUI` (syscall 38) and its 20 sub-functions. The `lib/gui.inc` library wraps
these into convenient functions.

### Setting Up a GUI Application

```nasm
%include "syscalls.inc"
%include "lib/gui.inc"

start:
        ; Create a window: x=50, y=40, w=300, h=200
        mov rax, 50
        mov rbx, 40
        mov rcx, 300
        mov rdx, 200
        mov rsi, title
        call gui_create_window
        cmp rax, -1
        je .exit
        mov [win_id], rax
```

### Window Drawing

All drawing coordinates are relative to the window's content area (0,0 is the
top-left corner inside the title bar and border):

```nasm
        ; Fill window background
        mov rax, [win_id]
        xor rbx, rbx           ; x=0
        xor rcx, rcx           ; y=0
        mov rdx, 300            ; width
        mov rsi, 200            ; height
        mov rdi, 0x2F2F3F       ; dark blue-gray
        call gui_fill_rect

        ; Draw text
        mov rax, [win_id]
        mov rbx, 10             ; x offset
        mov rcx, 20             ; y offset
        mov rsi, label_text
        mov rdi, 0xFFFFFF       ; white
        call gui_draw_text

        ; Draw single pixel
        mov rax, [win_id]
        mov rbx, 150            ; x
        mov rcx, 100            ; y
        mov rsi, 0xFF0000       ; red
        call gui_draw_pixel
```

### The GUI Event Loop

Every GUI application follows the compose → flip → poll pattern:

```nasm
.event_loop:
        ; 1. Compose the desktop (all windows, taskbar, etc.)
        call gui_compose

        ; 2. Flip back buffer to screen
        call gui_flip

        ; 3. Poll for events
        call gui_poll_event
        ; RAX = event type, RBX = param1, RCX = param2

        cmp rax, EVT_CLOSE
        je .close

        cmp rax, EVT_KEY_PRESS
        je .handle_key

        cmp rax, EVT_MOUSE_CLICK
        je .handle_click

        ; Yield to avoid busy-waiting
        mov rax, SYS_YIELD
        int 0x80

        jmp .event_loop

.close:
        mov rax, [win_id]
        call gui_destroy_window
.exit:
        mov rax, SYS_EXIT
        xor rbx, rbx
        int 0x80
```

### Event Types

| Constant | Value | RBX | RCX |
| --- | --- | --- | --- |
| `EVT_NONE` | 0 | — | — |
| `EVT_MOUSE_CLICK` | 1 | x position | y position |
| `EVT_MOUSE_MOVE` | 2 | x position | y position |
| `EVT_KEY_PRESS` | 3 | key code | — |
| `EVT_CLOSE` | 4 | window id | — |

### Themes

Applications can read and apply themes:

```nasm
        ; Get current theme into buffer
        mov rax, theme_buf
        call gui_get_theme

        ; Set a theme (0=Blue, 1=Dark, 2=Light, 3=Amber)
        mov rax, SYS_GUI
        mov rbx, GUI_SET_THEME
        mov rcx, 1              ; Dark theme
        int 0x80
```

### Mouse Input (Outside GUI)

For programs that need raw mouse coordinates without the Burrows desktop:

```nasm
        mov rax, SYS_MOUSE     ; syscall 36
        int 0x80
        ; RAX = X position, RBX = Y position, RCX = button state
        ; Buttons: bit 0 = left, bit 1 = right, bit 2 = middle
```

### Framebuffer Access (Advanced)

For programs that need direct pixel access without the window manager:

```nasm
        ; Get framebuffer info
        mov rax, SYS_FRAMEBUF  ; syscall 37
        mov rbx, 0             ; sub-fn 0 = get info
        int 0x80
        ; RAX = LFB address, RBX = width, RCX = height, RDX = bpp

        ; Switch to 640×480×32 mode
        mov rax, SYS_FRAMEBUF
        mov rbx, 1             ; sub-fn 1 = set mode
        int 0x80

        ; Restore text mode when done
        mov rax, SYS_FRAMEBUF
        mov rbx, 2             ; sub-fn 2 = restore text
        int 0x80
```

### Audio Playback (Sound Blaster 16)

Programs can play audio via the SB16 driver:

```nasm
        ; Play a PCM buffer (8-bit, 11025 Hz, mono)
        mov rax, 50             ; SYS_AUDIO_PLAY
        mov rbx, audio_data     ; pointer to PCM data
        mov rcx, audio_len      ; length in bytes
        mov rdx, 11025          ; format: sample rate in bits 0-15
        int 0x80

        ; Check playback status
        mov rax, 52             ; SYS_AUDIO_STATUS
        int 0x80
        ; RAX = state (0=idle, 1=playing, 2=done)
        ; RBX = sb16_present (0/1)

        ; Stop playback
        mov rax, 51             ; SYS_AUDIO_STOP
        int 0x80
```

Format flags for RDX: bits 0–15 = sample rate Hz, bit 16 = 16-bit,
bit 17 = stereo, bit 18 = signed samples.

### Inter-Process Communication

#### Pipes

```nasm
        ; Create a pipe
        mov rax, 60             ; SYS_PIPE_CREATE
        int 0x80
        ; RAX = pipe_id (-1 on error)
        mov [pipe_id], rax

        ; Write to pipe
        mov rax, 61             ; SYS_PIPE_WRITE
        mov rbx, [pipe_id]
        mov rcx, message        ; buffer
        mov rdx, msg_len        ; length
        int 0x80

        ; Read from pipe
        mov rax, 62             ; SYS_PIPE_READ
        mov rbx, [pipe_id]
        mov rcx, recv_buf
        mov rdx, 256            ; max bytes
        int 0x80
        ; RAX = bytes read

        ; Close pipe
        mov rax, 63             ; SYS_PIPE_CLOSE
        mov rbx, [pipe_id]
        int 0x80
```

#### Shared Memory

```nasm
        ; Get or create a shared memory region
        mov rax, 64             ; SYS_SHMGET
        mov rbx, 1              ; key (any integer)
        mov rcx, 4096           ; size
        int 0x80
        ; RAX = shm_id (-1 on error)
        mov [shm_id], rax

        ; Get the data pointer
        mov rax, 65             ; SYS_SHMADDR
        mov rbx, [shm_id]
        int 0x80
        ; RAX = pointer to 4 KB region
        mov [shm_ptr], rax
```

### Process Management

```nasm
        ; Get current PID
        mov rax, 54             ; SYS_GETPID
        int 0x80
        ; RAX = current task PID

        ; List tasks (slot 0–15)
        mov rax, 66             ; SYS_PROCLIST
        mov rbx, 0              ; slot index
        mov rcx, task_buf       ; 16-byte buffer
        int 0x80
        ; RAX = 0 if slot active, -1 if empty

        ; Get memory info
        mov rax, 67             ; SYS_MEMINFO
        int 0x80
        ; RAX = free pages, RBX = total at boot
```

### Built-in Burrows Apps

These programs in the `programs/` directory use `gui.inc` and demonstrate GUI patterns:

| Program | Description | Pattern Demonstrated |
| --- | --- | --- |
| `bcalc` | BCalc | Button grid, click handling |
| `bedit` | BEdit | Text input, scrolling |
| `bhive` | BHive | List view, directory navigation |
| `bforager` | BForager | Address bar, link navigation, protocol handoff |
| `bpaint` | BPaint | Pixel drawing, tool selection |
| `bsysmon` | BSysMon | Real-time data display |
| `bterm` | BTerm | Text rendering, keyboard I/O |
| `bnotes` | BNotes | Simple data persistence |
| `bplayer` | BPlayer | Audio playback, progress bar |
| `bsettings` | BSettings | Theme switching, preferences |
| `bsheet` | BSheet | Grid layout, formula evaluation |
| `bview` | BView | Image rendering, file loading |

---

## Game Loop Pattern

Here's the standard pattern used by games like Snake, Tetris, and 2048:

```nasm
%include "syscalls.inc"

main:
    ; Initialize game state
    call init_game

    ; Clear screen and draw initial frame
    mov rax, 17         ; SYS_CLEAR
    int 0x80
    call draw_game

game_loop:
    ; 1. Handle input (non-blocking)
    mov rax, 4          ; SYS_READ_KEY
    int 0x80
    test rax, rax
    jz .no_input
    call handle_input
    cmp byte [game_over], 1
    je .exit
.no_input:

    ; 2. Update game state
    call update_game

    ; 3. Render
    call draw_game

    ; 4. Frame delay (10 ticks = 100ms = 10 FPS)
    mov rax, 16         ; SYS_SLEEP
    mov rbx, 10
    int 0x80

    ; 5. Check game-over condition
    cmp byte [game_over], 1
    jne game_loop

.exit:
    ; Restore default color
    mov rax, 18         ; SYS_SETCOLOR
    mov rbx, 0x07
    int 0x80

    ; Print score or message
    mov rax, 3
    mov rbx, goodbye_msg
    int 0x80

    ; Exit
    mov rax, 0
    xor rbx, rbx
    int 0x80

; --- Data ---
game_over: db 0
goodbye_msg: db "Thanks for playing!", 10, 0
```

---

## Building Assembly Programs

### Single Program

```bash
nasm -f bin -O0 -o programs/myprogram programs/myprogram.asm
```

### Using syscalls.inc

Place your program in the `programs/` directory alongside `syscalls.inc`:

```nasm
%include "syscalls.inc"

    mov rax, SYS_PRINT
    mov rbx, msg
    int 0x80

    mov rax, SYS_EXIT
    xor rbx, rbx
    int 0x80

msg: db "It works!", 10, 0
```

### Adding to the Disk Image

1. Build the program: `nasm -f bin -O0 -o programs/myprog programs/myprog.asm`
2. Add it to `populate.py` in the appropriate list (`UTILITY_PROGRAMS` or `GAME_PROGRAMS`)
3. Run `make full` to rebuild everything including the disk image

### The -O0 Flag

**Always use `-O0`** (disable optimizations). Without it, NASM may generate short jumps
that break when the binary is loaded at `0x200000` instead of `0x0`. This is the single
most common source of program crashes.

---

## C Programming with TCC

Mellivora includes a built-in Tiny C Compiler (TCC) that can compile and run C programs
directly inside the OS.

### Hello World in C

```c
int main() {
    printf("Hello from C!\n");
    return 0;
}
```

Save as a file and compile:

```text
Lair:/> write hello.c
int main() {
    printf("Hello from C!\n");
    return 0;
}

Lair:/> tcc hello.c
Compiling hello.c...
Running...
Hello from C!
```

### Available Functions

| Function | Description |
| --- | --- |
| `printf(fmt, ...)` | Print formatted string (`%d`, `%s` supported) |
| `putchar(c)` | Print a single character |
| `getchar()` | Read a character (blocking) |

### Supported C Features

- **Types:** `int` (32-bit), `char`, pointers
- **Variables:** Global and local, including arrays
- **Control flow:** `if`/`else`, `while`, `for`, `do`/`while`
- **Functions:** Declaration, parameters, return values, recursion
- **Operators:** Arithmetic, comparison, logical, bitwise
- **Pointers:** Basic pointer arithmetic and dereferencing

### Example: Fibonacci

```c
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    int i;
    for (i = 0; i < 20; i++) {
        printf("fib(%d) = %d\n", i, fib(i));
    }
    return 0;
}
```

### Example: Interactive Calculator

```c
int main() {
    int a, b, result;
    char op;

    printf("Enter: num op num\n");
    printf("> ");

    a = 0; b = 0;
    // Read first number
    char c = getchar();
    while (c >= '0' && c <= '9') {
        a = a * 10 + (c - '0');
        c = getchar();
    }
    // Skip space
    op = getchar();
    getchar(); // skip space
    // Read second number
    c = getchar();
    while (c >= '0' && c <= '9') {
        b = b * 10 + (c - '0');
        c = getchar();
    }

    if (op == '+') result = a + b;
    if (op == '-') result = a - b;
    if (op == '*') result = a * b;
    if (op == '/') result = a / b;

    printf("%d %c %d = %d\n", a, op, b, result);
    return 0;
}
```

### C Sample Files

The `/samples` directory contains ready-to-compile examples:

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

### Perl Samples

The `/samples` directory also contains Perl scripts for the in-OS interpreter:

| File | Description |
| --- | --- |
| `hello.pl` | Hello World |
| `factorial.pl` | Factorial calculator |
| `fizzbuzz.pl` | FizzBuzz |
| `guess.pl` | Number guessing game |
| `arrays.pl` | Array operations demo |
| `strings.pl` | String manipulation demo |

---

## Debugging Tips

### Serial Port Debugging

The most useful debugging technique — output goes to the host terminal:

```nasm
; Sprinkle these throughout your code
mov rax, 28         ; SYS_SERIAL
mov rbx, .dbg1
int 0x80
jmp .cont1
.dbg1: db "[DBG] Before loop", 10, 0
.cont1:
```

Run QEMU with serial output:

```bash
qemu-system-x86_64 -hda mellivora.img -serial stdio
```

### Print Register Values

```nasm
; Print RAX as hex for debugging
debug_print_eax:
    push rbp
    push rsi
    push rdi
    push rcx
    push rax
    mov rsi, rax
    mov rdi, hex_buf + 10
    mov rcx, 8

.hex_loop:
    mov rax, rsi
    and rax, 0xF
    cmp rax, 10
    jb .digit
    add rax, 'A' - 10
    jmp .store
.digit:
    add rax, '0'
.store:
    dec rdi
    mov [rdi], al
    shr rsi, 4
    dec rcx
    jnz .hex_loop

    mov rax, 3
    mov rbx, hex_prefix
    int 0x80
    mov rax, 3
    mov rbx, hex_buf + 2
    int 0x80
    mov rax, 3
    mov rbx, newline_str
    int 0x80
    pop rax
    pop rcx
    pop rdi
    pop rsi
    pop rbp
    ret

hex_prefix: db "0x", 0
hex_buf: db "  00000000", 0
newline_str: db 10, 0
```

### Common Pitfalls

1. **Missing `-O0` flag:** NASM optimizations break programs loaded at 0x200000
2. **Forgetting `SYS_EXIT`:** Program will slide into garbage memory (though the
   trampoline catches `RET`)
3. **Buffer overflows:** No memory protection — overwriting past your buffer corrupts
   other data or code
4. **Clobbered registers:** Syscalls may modify RAX, RCX, RDX — save important values
   before calling
5. **Blocking I/O in game loops:** Use `SYS_READ_KEY` (non-blocking), not `SYS_GETCHAR`
   (blocking) in game loops
6. **Color leaks:** Always reset color to `0x07` before exiting
7. **Stack alignment:** RSP starts near top of program space — don't use too much stack

---

## Complete Syscall Table

Quick reference for all 84 syscalls (0–83):

| # | Name | RBX | RCX | RDX | Returns |
| --- | --- | --- | --- | --- | --- |
| 0 | EXIT | exit code | — | — | — |
| 1 | PUTCHAR | char | — | — | 0 |
| 2 | GETCHAR | — | — | — | char |
| 3 | PRINT | string ptr | — | — | 0 |
| 4 | READ_KEY | — | — | — | char or 0 |
| 5 | OPEN | filename | mode | — | fd or -1 |
| 6 | READ | fd | buffer | count | bytes read |
| 7 | WRITE | fd | buffer | count | bytes written |
| 8 | CLOSE | fd | — | — | 0 |
| 9 | DELETE | filename | — | — | 0/-1 |
| 10 | SEEK | fd | offset | — | new pos |
| 11 | STAT | filename | — | — | size/-1, RCX=blocks |
| 12 | MKDIR | dirname | — | — | 0/-1 |
| 13 | READDIR | name buf | index | — | type, RCX=size |
| 14 | SETCURSOR | X | Y | — | 0 |
| 15 | GETTIME | — | — | — | ticks |
| 16 | SLEEP | ticks | — | — | 0 |
| 17 | CLEAR | — | — | — | 0 |
| 18 | SETCOLOR | color | — | — | 0 |
| 19 | MALLOC | size | — | — | addr or 0 |
| 20 | FREE | addr | size | — | 0 |
| 21 | EXEC | filename | — | — | 0 |
| 22 | DISK_READ | LBA | count | buffer | 0/-1 |
| 23 | DISK_WRITE | LBA | count | buffer | 0/-1 |
| 24 | BEEP | freq | duration | — | 0 |
| 25 | DATE | 6-byte buf | — | — | year |
| 26 | CHDIR | dirname | — | — | 0/-1 |
| 27 | GETCWD | dest buf | — | — | 0 |
| 28 | SERIAL | string ptr | — | — | 0 |
| 29 | GETENV | var name | — | — | RDI=value |
| 30 | FREAD | filename | buffer | — | bytes |
| 31 | FWRITE | filename | buffer | size | 0/-1 |
| 32 | GETARGS | dest buf | — | — | length |
| 33 | SERIAL_IN | — | — | — | char |
| 34 | STDIN_READ | buffer | max len | — | bytes or -1 |
| 35 | YIELD | — | — | — | 0 |
| 36 | MOUSE | — | — | — | RAX=x, RBX=y, RCX=btns |
| 37 | FRAMEBUF | sub-fn | — | — | (varies by sub-fn) |
| 38 | GUI | sub-fn | (varies) | (varies) | (varies) |
| 39 | SOCKET | type (1=TCP, 2=UDP) | — | — | fd or -1 |
| 40 | CONNECT | socket fd | IP address | port | 0 or -1 |
| 41 | SEND | socket fd | buffer ptr | length | bytes or -1 |
| 42 | RECV | socket fd | buffer ptr | max length | bytes, 0, or -1 |
| 43 | BIND | socket fd | port | — | 0 or -1 |
| 44 | LISTEN | socket fd | — | — | 0 or -1 |
| 45 | ACCEPT | socket fd | — | — | new fd or -1 |
| 46 | DNS | hostname ptr | — | — | IP or 0 |
| 47 | SOCKCLOSE | socket fd | — | — | 0 |
| 48 | PING | IP address | — | — | RTT ticks or -1 |
| 49 | SETDATE | 6-byte buf | century | — | 0 |
| 50 | AUDIO_PLAY | buffer | length | format | 0/-1 |
| 51 | AUDIO_STOP | — | — | — | 0 |
| 52 | AUDIO_STATUS | — | — | — | state, RBX=present |
| 53 | KILL | pid | — | — | 0/-1 |
| 54 | GETPID | — | — | — | pid |
| 55 | CLIP_COPY | buffer | length | — | 0 |
| 56 | CLIP_PASTE | buffer | max len | — | bytes |
| 57 | NOTIFY | text ptr | — | color | 0 |
| 58 | FILE_OPEN_DLG | title | — | filter | 1/0, RCX=name |
| 59 | FILE_SAVE_DLG | title | — | filter | 1/0, RCX=name |
| 60 | PIPE_CREATE | — | — | — | pipe_id or -1 |
| 61 | PIPE_WRITE | pipe_id | buffer | length | bytes |
| 62 | PIPE_READ | pipe_id | buffer | max len | bytes |
| 63 | PIPE_CLOSE | pipe_id | — | — | 0 |
| 64 | SHMGET | key | size | — | shm_id or -1 |
| 65 | SHMADDR | shm_id | — | — | pointer |
| 66 | PROCLIST | slot (0–15) | 16B buf | — | 0/-1 |
| 67 | MEMINFO | — | — | — | free pages, RBX=total |
| 68 | CHMOD | filename | perms (9-bit) | — | 0/-1 |
| 69 | CHOWN | filename | owner UID | — | 0/-1 |
| 70 | SYMLINK | link name | target path | — | 0/-1 |
| 71 | READLINK | link name | output buf | — | length or -1 |
| 72 | SIGNAL | signal (1–4) | handler | — | prev handler |
| 73 | RAISE | target PID | signal (1–4) | — | 0/-1 |
| 74 | MQ_CREATE | key (nonzero) | — | — | mq_id or -1 |
| 75 | MQ_SEND | mq_id | data ptr | length | 0/-1 |
| 76 | MQ_RECV | mq_id | dest buf | max len | bytes or 0/-1 |
| 77 | MQ_CLOSE | mq_id | — | — | 0/-1 |
| 78 | STRACE | sub-fn (0–3) | dest/max | — | varies |
| 79 | LISTENV | dest buf | max size | — | bytes written |
| 80 | RENAME | old name | new name | — | 0/-1 |
| 81 | SETENV | "NAME=VALUE" | — | — | 0 |
| 82 | RMDIR | dirname | — | — | 0/-1 |
| 83 | TRUNCATE | filename | new size | — | 0/-1 |

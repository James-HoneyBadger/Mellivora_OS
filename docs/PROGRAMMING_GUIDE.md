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
| EAX | Syscall number / return value |
| EBX | First argument |
| ECX | Second argument |
| EDX | Third argument |
| ESI | Fourth argument |
| EDI | Fifth argument / secondary return |
| ESP | Stack pointer (program's own stack) |

---

## Your First Assembly Program

### hello.asm — Minimal Example

```nasm
; hello.asm — Hello World for Mellivora OS
BITS 32
ORG 0x200000

    ; Print a string
    mov eax, 3          ; SYS_PRINT
    mov ebx, message    ; pointer to null-terminated string
    int 0x80

    ; Exit cleanly
    mov eax, 0          ; SYS_EXIT
    xor ebx, ebx       ; exit code 0
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

- **`BITS 32`**: We're in 32-bit protected mode
- **`ORG 0x200000`**: Program is loaded at this address
- **`INT 0x80`**: All OS services go through this interrupt
- **`SYS_EXIT` (EAX=0)**: Always exit cleanly, or the trampoline does it for you
- **`-O0`**: Disable NASM optimizations (critical — prevents short jump issues)

---

## Syscall Reference

Every syscall uses the same convention:

```nasm
mov eax, SYSCALL_NUMBER
mov ebx, arg1
mov ecx, arg2
mov edx, arg3
int 0x80
; Return value in EAX (and sometimes ECX, EDI)
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
```

Or include the provided header:

```nasm
%include "syscalls.inc"
```

---

## Console I/O

### Print a String

```nasm
mov eax, 3          ; SYS_PRINT
mov ebx, msg        ; pointer to null-terminated string
int 0x80

msg: db "Hello!", 10, 0    ; 10 = newline
```

### Print a Single Character

```nasm
mov eax, 1          ; SYS_PUTCHAR
mov ebx, 'A'        ; character to print
int 0x80
```

### Read a Character (Blocking)

```nasm
mov eax, 2          ; SYS_GETCHAR
int 0x80
; EAX now contains the ASCII code of the key pressed
```

### Read a String (Character by Character)

```nasm
read_line:
    mov edi, buffer
    xor ecx, ecx       ; character count

.loop:
    mov eax, 2          ; SYS_GETCHAR
    int 0x80

    cmp al, 10          ; Enter?
    je .done
    cmp al, 13
    je .done

    stosb               ; store char and advance EDI
    inc ecx

    mov eax, 1          ; echo it back
    mov ebx, eax
    movzx ebx, al
    int 0x80

    cmp ecx, 255        ; buffer limit
    jb .loop

.done:
    mov byte [edi], 0   ; null-terminate
    ret

buffer: times 256 db 0
```

### Print a Decimal Number

```nasm
; Print the number in EAX as decimal
print_number:
    push ebp
    mov ebp, esp
    sub esp, 12         ; buffer on stack
    mov edi, ebp
    dec edi
    mov byte [edi], 0   ; null terminator

    test eax, eax
    jnz .convert
    dec edi
    mov byte [edi], '0'
    jmp .print

.convert:
    mov ecx, 10
.digit:
    test eax, eax
    jz .print
    xor edx, edx
    div ecx             ; EAX/10, remainder in EDX
    add dl, '0'
    dec edi
    mov [edi], dl
    jmp .digit

.print:
    mov eax, 3          ; SYS_PRINT
    mov ebx, edi
    int 0x80
    leave
    ret
```

---

## File I/O

### Simple File Read (Recommended)

The easiest way to read a file — one syscall, returns entire contents:

```nasm
mov eax, 30         ; SYS_FREAD
mov ebx, filename   ; filename (can include path: "/docs/readme")
mov ecx, buffer     ; destination buffer
int 0x80
; EAX = bytes read (0 if file not found)

filename: db "readme", 0
buffer: times 65536 db 0
```

### Simple File Write

```nasm
mov eax, 31         ; SYS_FWRITE
mov ebx, filename   ; filename
mov ecx, data       ; source buffer
mov edx, data_len   ; byte count
int 0x80
; EAX = 0 on success, -1 on failure

filename: db "output.txt", 0
data: db "Hello, file!", 10
data_len equ $ - data
```

### File Descriptor API (Open/Read/Write/Close)

For more control, use the fd-based API:

```nasm
; Open file for reading
mov eax, 5          ; SYS_OPEN
mov ebx, filename   ; filename
mov ecx, 1          ; mode: 1=read, 2=write
int 0x80
; EAX = file descriptor (-1 on error)
mov [fd], eax

; Read up to 1024 bytes
mov eax, 6          ; SYS_READ
mov ebx, [fd]       ; file descriptor
mov ecx, buffer     ; destination
mov edx, 1024       ; max bytes
int 0x80
; EAX = bytes actually read

; Seek to offset
mov eax, 10         ; SYS_SEEK
mov ebx, [fd]
mov ecx, 0          ; offset from start
int 0x80

; Close file
mov eax, 8          ; SYS_CLOSE
mov ebx, [fd]
int 0x80

fd: dd 0
filename: db "myfile.txt", 0
buffer: times 1024 db 0
```

### Check if File Exists (STAT)

```nasm
mov eax, 11         ; SYS_STAT
mov ebx, filename   ; filename
int 0x80
; EAX = file size in bytes (-1 if not found)
; ECX = block count

cmp eax, -1
je .not_found
; File exists, EAX = size
```

### Delete a File

```nasm
mov eax, 9          ; SYS_DELETE
mov ebx, filename
int 0x80
; EAX = 0 success, -1 failure
```

### Read Files from Other Directories

`SYS_FREAD` supports full paths:

```nasm
mov eax, 30
mov ebx, path
mov ecx, buffer
int 0x80

path: db "/docs/readme", 0       ; absolute path
; or:  db "../docs/readme", 0    ; relative path
```

---

## Screen Control

### Clear Screen

```nasm
mov eax, 17         ; SYS_CLEAR
int 0x80
```

### Set Cursor Position

```nasm
mov eax, 14         ; SYS_SETCURSOR
mov ebx, 10         ; column (0–79)
mov ecx, 5          ; row (0–24)
int 0x80
```

### Set Text Color

```nasm
mov eax, 18         ; SYS_SETCOLOR
mov ebx, 0x0A       ; light green on black
int 0x80
```

Color byte: high nibble = background, low nibble = foreground.

### Direct VGA Access

For performance-critical rendering (games), write directly to VGA memory:

```nasm
VGA_BASE equ 0xB8000

; Write 'X' at column 10, row 5 in red
mov edi, VGA_BASE
mov eax, 5
imul eax, 160       ; row * 80 * 2
add eax, 20         ; col * 2
add edi, eax
mov word [edi], 0x0C58   ; 0x0C = red, 'X' = 0x58
```

**Warning:** VGA writes are safe from Ring 3 because the flat memory model maps all
physical memory. But use syscalls for general output — direct VGA is only needed for
games and animations that must update many cells per frame.

### Draw a Colored Box

```nasm
; Draw a 20×5 box at (10, 3) with blue background
draw_box:
    mov ecx, 5          ; height
    mov edx, 3          ; start row

.row:
    push ecx
    mov eax, 14         ; SYS_SETCURSOR
    mov ebx, 10         ; start column
    mov ecx, edx
    int 0x80

    mov ecx, 20         ; width
.col:
    mov eax, 1          ; SYS_PUTCHAR
    mov ebx, ' '
    int 0x80
    dec ecx
    jnz .col

    inc edx
    pop ecx
    dec ecx
    jnz .row
    ret
```

---

## Keyboard Input

### Blocking Read

```nasm
mov eax, 2          ; SYS_GETCHAR
int 0x80
; Waits until a key is pressed, returns ASCII in EAX
```

### Non-Blocking Poll

```nasm
mov eax, 4          ; SYS_READ_KEY
int 0x80
; EAX = ASCII code, or 0 if no key pending
test eax, eax
jz .no_key
; Process key in EAX
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
    mov eax, 4          ; SYS_READ_KEY
    int 0x80
    test eax, eax
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
mov eax, 15         ; SYS_GETTIME
int 0x80
; EAX = tick_count (100 ticks = 1 second)
```

### Sleep

```nasm
mov eax, 16         ; SYS_SLEEP
mov ebx, 50         ; sleep for 50 ticks (0.5 seconds)
int 0x80
```

### Frame Rate Control

```nasm
game_loop:
    mov eax, 15         ; SYS_GETTIME
    int 0x80
    mov [frame_start], eax

    ; ... game logic and rendering ...

    ; Wait for next frame (target: 10 FPS = 10 ticks per frame)
    mov eax, 15
    int 0x80
    sub eax, [frame_start]
    cmp eax, 10
    jae .no_wait
    mov ebx, 10
    sub ebx, eax
    mov eax, 16         ; SYS_SLEEP
    int 0x80
.no_wait:
    jmp game_loop

frame_start: dd 0
```

### Play a Tone

```nasm
mov eax, 24         ; SYS_BEEP
mov ebx, 440        ; frequency in Hz (440 = A4)
mov ecx, 20         ; duration in ticks (200ms)
int 0x80
```

### Stop Sound

```nasm
mov eax, 24         ; SYS_BEEP
xor ebx, ebx       ; frequency 0 = silence
xor ecx, ecx
int 0x80
```

### Musical Scale Example

```nasm
; Play C major scale
play_scale:
    mov esi, notes
    mov ecx, 8

.play:
    push ecx
    movzx ebx, word [esi]  ; frequency
    mov eax, 24             ; SYS_BEEP
    mov ecx, 15             ; duration
    int 0x80

    mov eax, 16             ; SYS_SLEEP
    mov ebx, 20             ; gap between notes
    int 0x80

    add esi, 2
    pop ecx
    dec ecx
    jnz .play
    ret

notes: dw 262, 294, 330, 349, 392, 440, 494, 523  ; C4 to C5
```

---

## Memory Allocation

### Allocate Memory

Memory is allocated in 4 KB page granularity:

```nasm
mov eax, 19         ; SYS_MALLOC
mov ebx, 8192       ; request 8192 bytes (gets 2 pages = 8 KB)
int 0x80
; EAX = physical address of allocated memory (0 = failure)
test eax, eax
jz .out_of_memory
mov [my_buffer], eax
```

### Free Memory

```nasm
mov eax, 20         ; SYS_FREE
mov ebx, [my_buffer]   ; address returned by SYS_MALLOC
mov ecx, 8192          ; same size as allocated
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
mov eax, 12         ; SYS_MKDIR
mov ebx, dirname
int 0x80
; EAX = 0 success, -1 failure

dirname: db "mydir", 0
```

### Change Directory

```nasm
mov eax, 26         ; SYS_CHDIR
mov ebx, dirname
int 0x80
; EAX = 0 success, -1 failure
```

### Get Current Directory

```nasm
mov eax, 27         ; SYS_GETCWD
mov ebx, cwd_buf
int 0x80

cwd_buf: times 256 db 0
```

### List Directory Entries

```nasm
list_files:
    xor ecx, ecx       ; entry index

.next:
    push ecx
    mov eax, 13         ; SYS_READDIR
    mov ebx, name_buf   ; buffer for entry name
    int 0x80
    ; EAX = file type (0 = end of directory)
    ; ECX = file size

    test eax, eax
    jz .done

    ; Print the filename
    push eax
    mov eax, 3          ; SYS_PRINT
    mov ebx, name_buf
    int 0x80
    mov eax, 1
    mov ebx, 10         ; newline
    int 0x80
    pop eax

    pop ecx
    inc ecx
    jmp .next

.done:
    pop ecx
    ret

name_buf: times 256 db 0
```

---

## Serial Port I/O

### Write to Serial Port

Useful for debugging — output appears in the host terminal (QEMU `-serial stdio`):

```nasm
mov eax, 28         ; SYS_SERIAL
mov ebx, debug_msg
int 0x80

debug_msg: db "[DEBUG] Reached checkpoint 1", 10, 0
```

### Read from Serial Port

```nasm
mov eax, 33         ; SYS_SERIAL_IN
int 0x80
; EAX = character received from serial port
```

---

## Environment & Arguments

### Get Command-Line Arguments

```nasm
mov eax, 32         ; SYS_GETARGS
mov ebx, args_buf   ; buffer (max 512 bytes)
int 0x80
; EAX = length of argument string
; args_buf contains everything after the program name

args_buf: times 512 db 0
```

### Get an Environment Variable

```nasm
mov eax, 29         ; SYS_GETENV
mov ebx, var_name
int 0x80
; EDI = pointer to value string (inside kernel's env table)
; If not found, EDI is undefined — check before using

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
        mov esi, hostname
        call net_dns            ; EAX = IP address (0 = failed)
        test eax, eax
        jz .error
        mov [server_ip], eax

        ; 2. Create a TCP socket
        mov eax, NET_TCP        ; NET_TCP = 1
        call net_socket         ; EAX = socket fd (-1 = error)
        cmp eax, -1
        je .error
        mov [sockfd], eax

        ; 3. Connect to port 80
        mov eax, [sockfd]
        mov ebx, [server_ip]
        mov ecx, 80
        call net_connect        ; EAX = 0 (success) or -1 (error)
        cmp eax, -1
        je .error

        ; 4. Send data
        mov eax, [sockfd]
        mov esi, message
        call net_send_line      ; Sends string + CRLF

        ; 5. Receive response
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, 512
        call net_recv           ; EAX = bytes (0=no data, -1=closed)

        ; 6. Close socket
        mov eax, [sockfd]
        call net_close

.error:
        mov eax, SYS_EXIT
        xor ebx, ebx
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
        mov eax, [sockfd]
        mov esi, helo_cmd
        call net_send_line      ; Sends "HELO mellivora\r\n"

        ; net_recv_line reads until \n and null-terminates
        mov eax, [sockfd]
        mov edi, response_buf
        mov ecx, 256
        call net_recv_line      ; EAX = bytes, buffer is null-terminated

helo_cmd: db "HELO mellivora", 0
```

### DNS Resolution

```nasm
        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_failed          ; 0 = resolution failed
        ; EAX = 32-bit IP address in little-endian
```

The kernel maintains an 8-entry DNS cache. Repeated lookups for the same hostname
return the cached result without a network request.

### ICMP Ping

```nasm
        mov eax, [target_ip]
        call net_ping           ; EAX = RTT in timer ticks, or -1 = timeout
        cmp eax, -1
        je .timed_out
```

### Parsing IP Addresses

```nasm
        mov esi, ip_string
        call net_parse_ip       ; EAX = 32-bit IP (0 = parse error)

ip_string: db "10.0.2.2", 0
```

### Networking Syscall Reference

| Syscall | # | EBX | ECX | EDX | EAX Return |
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
        mov eax, 50
        mov ebx, 40
        mov ecx, 300
        mov edx, 200
        mov esi, title
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax
```

### Window Drawing

All drawing coordinates are relative to the window's content area (0,0 is the
top-left corner inside the title bar and border):

```nasm
        ; Fill window background
        mov eax, [win_id]
        xor ebx, ebx           ; x=0
        xor ecx, ecx           ; y=0
        mov edx, 300            ; width
        mov esi, 200            ; height
        mov edi, 0x2F2F3F       ; dark blue-gray
        call gui_fill_rect

        ; Draw text
        mov eax, [win_id]
        mov ebx, 10             ; x offset
        mov ecx, 20             ; y offset
        mov esi, label_text
        mov edi, 0xFFFFFF       ; white
        call gui_draw_text

        ; Draw single pixel
        mov eax, [win_id]
        mov ebx, 150            ; x
        mov ecx, 100            ; y
        mov esi, 0xFF0000       ; red
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
        ; EAX = event type, EBX = param1, ECX = param2

        cmp eax, EVT_CLOSE
        je .close

        cmp eax, EVT_KEY_PRESS
        je .handle_key

        cmp eax, EVT_MOUSE_CLICK
        je .handle_click

        ; Yield to avoid busy-waiting
        mov eax, SYS_YIELD
        int 0x80

        jmp .event_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
```

### Event Types

| Constant | Value | EBX | ECX |
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
        mov eax, theme_buf
        call gui_get_theme

        ; Set a theme (0=Blue, 1=Dark, 2=Light)
        mov eax, SYS_GUI
        mov ebx, GUI_SET_THEME
        mov ecx, 1              ; Dark theme
        int 0x80
```

### Mouse Input (Outside GUI)

For programs that need raw mouse coordinates without the Burrows desktop:

```nasm
        mov eax, SYS_MOUSE     ; syscall 36
        int 0x80
        ; EAX = X position, EBX = Y position, ECX = button state
        ; Buttons: bit 0 = left, bit 1 = right, bit 2 = middle
```

### Framebuffer Access (Advanced)

For programs that need direct pixel access without the window manager:

```nasm
        ; Get framebuffer info
        mov eax, SYS_FRAMEBUF  ; syscall 37
        mov ebx, 0             ; sub-fn 0 = get info
        int 0x80
        ; EAX = LFB address, EBX = width, ECX = height, EDX = bpp

        ; Switch to 640×480×32 mode
        mov eax, SYS_FRAMEBUF
        mov ebx, 1             ; sub-fn 1 = set mode
        int 0x80

        ; Restore text mode when done
        mov eax, SYS_FRAMEBUF
        mov ebx, 2             ; sub-fn 2 = restore text
        int 0x80
```

### Audio Playback (Sound Blaster 16)

Programs can play audio via the SB16 driver:

```nasm
        ; Play a PCM buffer (8-bit, 11025 Hz, mono)
        mov eax, 50             ; SYS_AUDIO_PLAY
        mov ebx, audio_data     ; pointer to PCM data
        mov ecx, audio_len      ; length in bytes
        mov edx, 11025          ; format: sample rate in bits 0-15
        int 0x80

        ; Check playback status
        mov eax, 52             ; SYS_AUDIO_STATUS
        int 0x80
        ; EAX = state (0=idle, 1=playing, 2=done)
        ; EBX = sb16_present (0/1)

        ; Stop playback
        mov eax, 51             ; SYS_AUDIO_STOP
        int 0x80
```

Format flags for EDX: bits 0–15 = sample rate Hz, bit 16 = 16-bit,
bit 17 = stereo, bit 18 = signed samples.

### Inter-Process Communication

#### Pipes

```nasm
        ; Create a pipe
        mov eax, 60             ; SYS_PIPE_CREATE
        int 0x80
        ; EAX = pipe_id (-1 on error)
        mov [pipe_id], eax

        ; Write to pipe
        mov eax, 61             ; SYS_PIPE_WRITE
        mov ebx, [pipe_id]
        mov ecx, message        ; buffer
        mov edx, msg_len        ; length
        int 0x80

        ; Read from pipe
        mov eax, 62             ; SYS_PIPE_READ
        mov ebx, [pipe_id]
        mov ecx, recv_buf
        mov edx, 256            ; max bytes
        int 0x80
        ; EAX = bytes read

        ; Close pipe
        mov eax, 63             ; SYS_PIPE_CLOSE
        mov ebx, [pipe_id]
        int 0x80
```

#### Shared Memory

```nasm
        ; Get or create a shared memory region
        mov eax, 64             ; SYS_SHMGET
        mov ebx, 1              ; key (any integer)
        mov ecx, 4096           ; size
        int 0x80
        ; EAX = shm_id (-1 on error)
        mov [shm_id], eax

        ; Get the data pointer
        mov eax, 65             ; SYS_SHMADDR
        mov ebx, [shm_id]
        int 0x80
        ; EAX = pointer to 4 KB region
        mov [shm_ptr], eax
```

### Process Management

```nasm
        ; Get current PID
        mov eax, 54             ; SYS_GETPID
        int 0x80
        ; EAX = current task PID

        ; List tasks (slot 0–15)
        mov eax, 66             ; SYS_PROCLIST
        mov ebx, 0              ; slot index
        mov ecx, task_buf       ; 16-byte buffer
        int 0x80
        ; EAX = 0 if slot active, -1 if empty

        ; Get memory info
        mov eax, 67             ; SYS_MEMINFO
        int 0x80
        ; EAX = free pages, EBX = total at boot
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
BITS 32
ORG 0x200000

main:
    ; Initialize game state
    call init_game

    ; Clear screen and draw initial frame
    mov eax, 17         ; SYS_CLEAR
    int 0x80
    call draw_game

game_loop:
    ; 1. Handle input (non-blocking)
    mov eax, 4          ; SYS_READ_KEY
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
    call draw_game

    ; 4. Frame delay (10 ticks = 100ms = 10 FPS)
    mov eax, 16         ; SYS_SLEEP
    mov ebx, 10
    int 0x80

    ; 5. Check game-over condition
    cmp byte [game_over], 1
    jne game_loop

.exit:
    ; Restore default color
    mov eax, 18         ; SYS_SETCOLOR
    mov ebx, 0x07
    int 0x80

    ; Print score or message
    mov eax, 3
    mov ebx, goodbye_msg
    int 0x80

    ; Exit
    mov eax, 0
    xor ebx, ebx
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

BITS 32
ORG 0x200000

    mov eax, SYS_PRINT
    mov ebx, msg
    int 0x80

    mov eax, SYS_EXIT
    xor ebx, ebx
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
mov eax, 28         ; SYS_SERIAL
mov ebx, .dbg1
int 0x80
jmp .cont1
.dbg1: db "[DBG] Before loop", 10, 0
.cont1:
```

Run QEMU with serial output:

```bash
qemu-system-i386 -hda mellivora.img -serial stdio
```

### Print Register Values

```nasm
; Print EAX as hex for debugging
debug_print_eax:
    pushad
    mov esi, eax
    mov edi, hex_buf + 10
    mov ecx, 8

.hex_loop:
    mov eax, esi
    and eax, 0xF
    cmp eax, 10
    jb .digit
    add eax, 'A' - 10
    jmp .store
.digit:
    add eax, '0'
.store:
    dec edi
    mov [edi], al
    shr esi, 4
    dec ecx
    jnz .hex_loop

    mov eax, 3
    mov ebx, hex_prefix
    int 0x80
    mov eax, 3
    mov ebx, hex_buf + 2
    int 0x80
    mov eax, 3
    mov ebx, newline_str
    int 0x80
    popad
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
4. **Clobbered registers:** Syscalls may modify EAX, ECX, EDX — save important values
   before calling
5. **Blocking I/O in game loops:** Use `SYS_READ_KEY` (non-blocking), not `SYS_GETCHAR`
   (blocking) in game loops
6. **Color leaks:** Always reset color to `0x07` before exiting
7. **Stack alignment:** ESP starts near top of program space — don't use too much stack

---

## Complete Syscall Table

Quick reference for all 68 syscalls (0–67):

| # | Name | EBX | ECX | EDX | Returns |
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
| 11 | STAT | filename | — | — | size/-1, ECX=blocks |
| 12 | MKDIR | dirname | — | — | 0/-1 |
| 13 | READDIR | name buf | index | — | type, ECX=size |
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
| 29 | GETENV | var name | — | — | EDI=value |
| 30 | FREAD | filename | buffer | — | bytes |
| 31 | FWRITE | filename | buffer | size | 0/-1 |
| 32 | GETARGS | dest buf | — | — | length |
| 33 | SERIAL_IN | — | — | — | char |
| 34 | STDIN_READ | buffer | max len | — | bytes or -1 |
| 35 | YIELD | — | — | — | 0 |
| 36 | MOUSE | — | — | — | EAX=x, EBX=y, ECX=btns |
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
| 52 | AUDIO_STATUS | — | — | — | state, EBX=present |
| 53 | KILL | pid | — | — | 0/-1 |
| 54 | GETPID | — | — | — | pid |
| 55 | CLIP_COPY | buffer | length | — | 0 |
| 56 | CLIP_PASTE | buffer | max len | — | bytes |
| 57 | NOTIFY | text ptr | — | color | 0 |
| 58 | FILE_OPEN_DLG | title | — | filter | 1/0, ECX=name |
| 59 | FILE_SAVE_DLG | title | — | filter | 1/0, ECX=name |
| 60 | PIPE_CREATE | — | — | — | pipe_id or -1 |
| 61 | PIPE_WRITE | pipe_id | buffer | length | bytes |
| 62 | PIPE_READ | pipe_id | buffer | max len | bytes |
| 63 | PIPE_CLOSE | pipe_id | — | — | 0 |
| 64 | SHMGET | key | size | — | shm_id or -1 |
| 65 | SHMADDR | shm_id | — | — | pointer |
| 66 | PROCLIST | slot (0–15) | 16B buf | — | 0/-1 |
| 67 | MEMINFO | — | — | — | free pages, EBX=total |

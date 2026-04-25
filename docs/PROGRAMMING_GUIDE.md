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
9. [Memory Management](#memory-management)
10. [Directory Operations](#directory-operations)
11. [Serial Port I/O](#serial-port-io)
12. [Environment & Arguments](#environment--arguments)
13. [VBE Pixel Graphics](#vbe-pixel-graphics)
14. [Game Loop Pattern](#game-loop-pattern)
15. [Building Assembly Programs](#building-assembly-programs)
16. [C Programming with TCC](#c-programming-with-tcc)
17. [Debugging Tips](#debugging-tips)
18. [Complete Syscall Table](#complete-syscall-table)

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

- **Preemptive multitasking:** User programs run as Ring 3 tasks under the scheduler
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
    xor ebx, ebx        ; exit code 0
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

- **`BITS 32`**: Programs run in 32-bit protected mode
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
SYS_EXIT            equ 0   ; Terminate process
SYS_PUTCHAR         equ 1   ; Print character: EBX=char
SYS_GETCHAR         equ 2   ; Read character (blocking) -> EAX=char
SYS_PRINT           equ 3   ; Print null-terminated string: EBX=ptr
SYS_READ_KEY        equ 4   ; Read key (non-blocking) -> EAX=key or 0
SYS_OPEN            equ 5   ; Open file: EBX=name ECX=mode -> EAX=fd
SYS_READ            equ 6   ; Read from fd: EBX=fd ECX=buf EDX=len -> EAX=bytes
SYS_WRITE           equ 7   ; Write to fd: EBX=fd ECX=buf EDX=len -> EAX=bytes
SYS_CLOSE           equ 8   ; Close fd: EBX=fd
SYS_DELETE          equ 9   ; Delete file: EBX=name -> EAX=0/-1
SYS_SEEK            equ 10  ; Seek: EBX=fd ECX=offset EDX=whence -> EAX=pos
SYS_STAT            equ 11  ; Stat: EBX=name ECX=buf -> EAX=0/-1
SYS_MKDIR           equ 12  ; Create dir: EBX=name -> EAX=0/-1
SYS_READDIR         equ 13  ; Read dir entry: EBX=buf ECX=index -> EAX=type ECX=size
SYS_SETCURSOR       equ 14  ; Set cursor: EBX=col ECX=row
SYS_GETTIME         equ 15  ; Get RTC time -> EAX=packed time
SYS_SLEEP           equ 16  ; Sleep: EBX=ticks (100 ticks = 1 sec)
SYS_CLEAR           equ 17  ; Clear screen
SYS_SETCOLOR        equ 18  ; Set text color: EBX=fg ECX=bg
SYS_MALLOC          equ 19  ; Allocate heap: EBX=size -> EAX=ptr
SYS_FREE            equ 20  ; Free heap: EBX=ptr
SYS_EXEC            equ 21  ; Execute program: EBX=name ECX=args -> EAX=0/-1
SYS_DISK_READ       equ 22  ; Raw disk read (restricted from user-mode)
SYS_SBRK            equ 23  ; Adjust program break: EBX=increment -> EAX=old_brk
SYS_BEEP            equ 24  ; PC speaker beep: EBX=freq_hz ECX=duration_ticks
SYS_DATE            equ 25  ; Get date -> EAX=day EBX=month ECX=year
SYS_CHDIR           equ 26  ; Change directory: EBX=path -> EAX=0/-1
SYS_GETCWD          equ 27  ; Get current dir: EBX=buf -> EAX=len
SYS_SERIAL          equ 28  ; Write serial: EBX=char
SYS_GETENV          equ 29  ; Get env var: EBX=name ECX=buf -> EAX=len/-1
SYS_FREAD           equ 30  ; Read whole file: EBX=name ECX=buf -> EAX=bytes
SYS_FWRITE          equ 31  ; Write file: EBX=name ECX=buf EDX=size ESI=type
SYS_GETARGS         equ 32  ; Get command-line args: EBX=buf -> EAX=len
SYS_SERIAL_IN       equ 33  ; Read serial: -> EAX=char or -1
SYS_STDIN_READ      equ 34  ; Read piped stdin: EBX=buf -> EAX=bytes (-1=no pipe)
SYS_YIELD           equ 35  ; Yield CPU to next task
SYS_MOUSE           equ 36  ; Mouse state: -> EAX=x EBX=y ECX=buttons
SYS_FRAMEBUF        equ 37  ; Framebuffer ops: EBX=sub (0=info,1=set,2=restore,3=text,4=present)
SYS_GUI             equ 38  ; Burrows GUI sub-calls: EBX=sub
SYS_SOCKET          equ 39  ; Create socket: EBX=type(1=TCP,2=UDP) -> EAX=fd
SYS_CONNECT         equ 40  ; Connect: EBX=fd ECX=ip EDX=port -> EAX=0/-1
SYS_SEND            equ 41  ; Send: EBX=fd ECX=buf EDX=len -> EAX=bytes
SYS_RECV            equ 42  ; Recv: EBX=fd ECX=buf EDX=max -> EAX=bytes
SYS_BIND            equ 43  ; Bind: EBX=fd ECX=port -> EAX=0/-1
SYS_LISTEN          equ 44  ; Listen: EBX=fd -> EAX=0/-1
SYS_ACCEPT          equ 45  ; Accept: EBX=fd -> EAX=new_fd
SYS_DNS             equ 46  ; Resolve hostname: EBX=name -> EAX=ip (0=fail)
SYS_SOCKCLOSE       equ 47  ; Close socket: EBX=fd
SYS_PING            equ 48  ; ICMP ping: EBX=ip -> EAX=rtt/-1
SYS_SETDATE         equ 49  ; Set RTC date: EBX=buf[sec,min,hr,day,mon,yr]
SYS_AUDIO_PLAY      equ 50  ; Play PCM audio: EBX=buf ECX=len EDX=fmt
SYS_AUDIO_STOP      equ 51  ; Stop audio playback
SYS_AUDIO_STATUS    equ 52  ; Query audio: -> EAX=state EBX=present
SYS_KILL            equ 53  ; Kill task: EBX=pid -> EAX=0/-1
SYS_GETPID          equ 54  ; Get own PID -> EAX=pid
SYS_CLIPBOARD_COPY  equ 55  ; Copy to clipboard: EBX=buf ECX=len
SYS_CLIPBOARD_PASTE equ 56  ; Paste from clipboard: EBX=buf ECX=max -> EAX=len
SYS_NOTIFY          equ 57  ; Show notification: EBX=text EDX=color
SYS_FILE_OPEN_DLG   equ 58  ; File open dialog -> EAX=1/0 ECX=chosen name
SYS_FILE_SAVE_DLG   equ 59  ; File save dialog -> EAX=1/0 ECX=chosen name
SYS_PIPE_CREATE     equ 60  ; Create pipe -> EAX=pipe_id
SYS_PIPE_WRITE      equ 61  ; Write pipe: EBX=id ECX=buf EDX=len
SYS_PIPE_READ       equ 62  ; Read pipe: EBX=id ECX=buf EDX=max -> EAX=read
SYS_PIPE_CLOSE      equ 63  ; Close pipe: EBX=id
SYS_SHMGET          equ 64  ; Get shared memory: EBX=key ECX=size -> EAX=shm_id
SYS_SHMADDR         equ 65  ; Map shared memory: EBX=shm_id -> EAX=ptr
SYS_PROCLIST        equ 66  ; List tasks: EBX=slot ECX=buf(16B) -> EAX=0/-1
SYS_MEMINFO         equ 67  ; Memory info: -> EAX=free_pages EBX=boot_free
SYS_CHMOD           equ 68  ; Change permissions: EBX=name ECX=perms
SYS_CHOWN           equ 69  ; Change owner: EBX=name ECX=uid
SYS_SYMLINK         equ 70  ; Create symlink: EBX=linkname ECX=target
SYS_READLINK        equ 71  ; Read symlink: EBX=linkname ECX=buf -> EAX=len
SYS_SETPRIORITY     equ 72  ; Set task priority: EBX=pid(0=self) ECX=prio
SYS_GETPRIORITY     equ 73  ; Get task priority: EBX=pid(0=self) -> EAX=prio
SYS_SIGNAL          equ 74  ; Send signal: EBX=pid ECX=signum
SYS_SETPGID         equ 75  ; Set PGID: EBX=pid ECX=pgid
SYS_GETPGID         equ 76  ; Get PGID: EBX=pid -> EAX=pgid
SYS_SIGMASK         equ 77  ; Signal mask: EBX=op ECX=mask -> EAX=old
SYS_TASKNAME        equ 78  ; Set task name: EBX=name_ptr
SYS_REALLOC         equ 79  ; Reallocate: EBX=ptr ECX=new_size EDX=old_size -> EAX=ptr
SYS_GETENV_SLOT     equ 80  ; Get env slot: EBX=index ECX=buf(128) -> EAX=0/-1
SYS_DMESG_WRITE     equ 81  ; Write to dmesg log: EBX=msg_ptr
SYS_SEM_CREATE      equ 88  ; Create semaphore: EBX=initial_value -> EAX=sem_id/-1
SYS_SEM_WAIT        equ 89  ; Wait (P) semaphore: EBX=sem_id -> EAX=0/-1
SYS_SEM_POST        equ 90  ; Post (V) semaphore: EBX=sem_id -> EAX=0
SYS_SEM_CLOSE       equ 91  ; Close semaphore: EBX=sem_id -> EAX=0
SYS_WAITPID         equ 92  ; Wait for task: EBX=pid -> EAX=exit_code/-1
SYS_GETMTIME        equ 93  ; Get file timestamps: EBX=name -> EAX=mtime ECX=ctime
SYS_SETMTIME        equ 94  ; Set file mtime: EBX=name ECX=timestamp(0=now) -> EAX=0/-1
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

## Memory Management

Mellivora OS provides two ways for programs to manage memory: a simple page-level allocator (`SYS_MALLOC`/`SYS_FREE`) and a more traditional heap allocator via `SYS_SBRK`.

### `SYS_SBRK` (Recommended for `malloc` implementations)

For dynamic memory allocation similar to Unix, use `SYS_SBRK` to grow or shrink the program's data segment. This is the preferred method for building a `malloc` heap.

- **`SYS_SBRK` (EAX=23)**
  - **Input:** `EBX` = signed integer increment.
    - Positive value: increases the program break (allocates memory).
    - Negative value: decreases the program break (frees memory).
    - Zero: returns the current program break without changing it.
  - **Output:** `EAX` = the *old* program break address on success. On failure (e.g., requesting memory that would collide with the stack), returns `-1`.

#### Example: Requesting 4KB of memory

```nasm
mov eax, 23         ; SYS_SBRK
mov ebx, 4096       ; Increment by 4096 bytes
int 0x80
; EAX now holds the start of the newly allocated 4KB block
; (the old program break).
cmp eax, -1
je .error_handler
; ... use memory at [eax] ...
```

#### Example: Getting current break

```nasm
mov eax, 23
mov ebx, 0
int 0x80
; EAX = current program break
```

### `SYS_MALLOC` / `SYS_FREE` (Legacy)

These syscalls operate directly on 4KB physical memory pages. They are less flexible than `sbrk` and are generally not recommended for new applications.

- **`SYS_MALLOC` (EAX=19)**: Allocates one or more 4KB pages.
  - **Input:** `EBX` = size in bytes (will be rounded up to the nearest 4KB).
  - **Output:** `EAX` = physical address of allocated block, or 0 on failure.
- **`SYS_FREE` (EAX=20)**: Frees pages allocated with `SYS_MALLOC`.
  - **Input:** `EBX` = physical address, `ECX` = size in bytes.

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

## VBE Pixel Graphics

Mellivora supports high-resolution 32 bpp framebuffer modes via the `SYS_FRAMEBUF`
syscall (37). All VBE programs use **double buffering** — render to a shadow buffer,
then call `SYS_FRAMEBUF/4` to blit it to the screen.

### Setup

```nasm
; 1. Enter VBE mode (640×480×32)
mov eax, SYS_FRAMEBUF
mov ebx, 1          ; sub: set mode
mov ecx, 640        ; width
mov edx, 480        ; height
mov esi, 32         ; bpp
int 0x80

; 2. Get shadow buffer address + dimensions
mov eax, SYS_FRAMEBUF
xor ebx, ebx        ; sub: get info
int 0x80
; EAX = shadow buffer address
; EBX = width (pixels)
; ECX = height (pixels)
; EDX = bits per pixel
mov [fb_addr],   eax
mov [fb_width],  ebx
mov [fb_height], ecx
; pitch = width * bytes_per_pixel
mov eax, ebx
imul eax, 4
mov [fb_pitch], eax
```

### Drawing Pixels

Write 32-bit `0x00RRGGBB` values directly to the shadow buffer:

```nasm
; Plot pixel at (x, y) with color
; EBX=x, ECX=y, EDX=color (0x00RRGGBB)
plot_pixel:
    mov eax, [fb_pitch]
    imul eax, ecx           ; y * pitch
    add eax, [fb_addr]
    lea eax, [eax + ebx*4]  ; + x * 4
    mov [eax], edx
    ret
```

### Filling a Rectangle

```nasm
; EBX=left, ECX=top, EDX=width, ESI=height, EDI=color
fill_rect:
    pushad
    mov ebp, ecx            ; save top
.row:
    test esi, esi
    jz .done
    mov eax, [fb_pitch]
    imul eax, ebp
    add eax, [fb_addr]
    lea eax, [eax + ebx*4]
    push ecx
    mov ecx, edx
.col:
    mov [eax], edi
    add eax, 4
    dec ecx
    jnz .col
    pop ecx
    inc ebp
    dec esi
    jmp .row
.done:
    popad
    ret
```

### Present (Double Buffer Flip)

Call once per frame after all rendering:

```nasm
mov eax, SYS_FRAMEBUF
mov ebx, 4          ; sub: present — blit shadow -> LFB
int 0x80
```

### Restoring Text Mode

```nasm
mov eax, SYS_FRAMEBUF
mov ebx, 2          ; sub: restore text mode
int 0x80
```

### Drawing Text in VBE Mode

```nasm
; ECX=x, EDX=y, ESI=string_ptr, EDI=fg_color (0x00RRGGBB)
mov eax, SYS_FRAMEBUF
mov ebx, 3          ; sub: draw text
mov ecx, 100        ; x pixel position
mov edx, 50         ; y pixel position
mov esi, my_string
mov edi, 0xFFFFFF   ; white
int 0x80
```

### VBE Game Loop Pattern

```nasm
; Initialize VBE (see Setup above)

game_loop:
    ; 1. Non-blocking key check
    mov eax, SYS_READ_KEY
    xor ebx, ebx
    int 0x80
    test eax, eax
    jz .no_key
    ; handle key in EAX ...
.no_key:

    ; 2. Update game state
    call update

    ; 3. Clear shadow buffer
    pushad
    mov edi, [fb_addr]
    xor eax, eax
    mov ecx, [fb_pitch]
    imul ecx, [fb_height]
    shr ecx, 2          ; dwords
    rep stosd
    popad

    ; 4. Render to shadow buffer
    call draw_frame

    ; 5. Flip: blit shadow -> real LFB
    mov eax, SYS_FRAMEBUF
    mov ebx, 4
    int 0x80

    ; 6. Frame cap
    mov eax, SYS_SLEEP
    mov ebx, 2          ; ~20 ms
    int 0x80

    jmp game_loop
```

### Common VBE Constants

| Constant | Value | Description |
| --------- | ----- | ----------- |
| `SYS_FRAMEBUF` | 37 | Framebuffer syscall |
| `SYS_FRAMEBUF_INFO` | 0 | Sub: get info |
| `SYS_FRAMEBUF_SETMODE` | 1 | Sub: set mode |
| `SYS_FRAMEBUF_RESTORE` | 2 | Sub: restore text |
| `SYS_FRAMEBUF_TEXT` | 3 | Sub: draw text |
| `SYS_FRAMEBUF_PRESENT` | 4 | Sub: blit shadow→LFB |

### Sprite Drawing

Use `%include "sprite.inc"` for pixel-art sprites (see `API_REFERENCE.md` for the full
`sprite.inc` documentation):

```nasm
%include "syscalls.inc"
%include "sprite.inc"   ; provides sprite_draw, sprite_draw_opaque, etc.

SPRITE_BEGIN spr_coin, 8, 8
  dd 0x00000000, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0x00000000
  dd 0xFFFFD700, 0xFFFFFF00, 0xFFFFFF00, 0xFFFFD700, 0xFFFFD700, 0xFFFFFF00, 0xFFFFFF00, 0xFFFFD700
  dd 0xFFFFD700, 0xFFFFFF00, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFFF00, 0xFFFFD700
  dd 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700
  dd 0xFFFFD700, 0xFFFFFF00, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFFF00, 0xFFFFD700
  dd 0xFFFFD700, 0xFFFFFF00, 0xFFFFFF00, 0xFFFFD700, 0xFFFFD700, 0xFFFFFF00, 0xFFFFFF00, 0xFFFFD700
  dd 0x00000000, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0xFFFFD700, 0x00000000
  dd 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
SPRITE_END

    ; Draw coin at (100, 200) — transparent pixels skipped automatically
    mov ebx, 100
    mov ecx, 200
    mov esi, spr_coin
    call sprite_draw
```

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

- **Types:** `int` (64-bit), `char`, pointers
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

Quick reference for all 94 syscalls. See `programs/syscalls.inc` for the authoritative list.

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
| 22 | DISK_READ | LBA | count | buffer | 0/-1 (denied Ring 3) |
| 23 | DISK_WRITE | LBA | count | buffer | 0/-1 (denied Ring 3) |
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
| 34 | STDIN_READ | buffer | — | — | bytes/-1 |
| 35 | YIELD | — | — | — | 0 |
| 36 | MOUSE | — | — | — | EAX=x, EBX=y, ECX=buttons |
| 37 | FRAMEBUF | sub (0–4) | varies | varies | varies |
| 38 | GUI | sub-function | varies | varies | varies |
| 39 | SOCKET | type (1=TCP, 2=UDP) | — | — | fd or -1 |
| 40 | CONNECT | fd | ip | port | 0/-1 |
| 41 | SEND | fd | buffer | len | bytes |
| 42 | RECV | fd | buffer | max | bytes |
| 43 | BIND | fd | port | — | 0/-1 |
| 44 | LISTEN | fd | — | — | 0/-1 |
| 45 | ACCEPT | fd | — | — | new fd |
| 46 | DNS | hostname | — | — | ip (0=fail) |
| 47 | SOCKCLOSE | fd | — | — | 0 |
| 48 | PING | ip | — | — | rtt/-1 |
| 49 | SETDATE | buf | century | — | 0 |
| 50 | AUDIO_PLAY | buf | len | fmt | 0/-1 |
| 51 | AUDIO_STOP | — | — | — | 0 |
| 52 | AUDIO_STATUS | — | — | — | EAX=state, EBX=present |
| 53 | KILL | pid | — | — | 0/-1 |
| 54 | GETPID | — | — | — | pid |
| 55 | CLIPBOARD_COPY | buf | len | — | 0 |
| 56 | CLIPBOARD_PASTE | buf | max | — | len |
| 57 | NOTIFY | text | — | color | 0 |
| 58 | FILE_OPEN_DLG | title | — | filter | EAX=1/0, ECX=name |
| 59 | FILE_SAVE_DLG | title | — | filter | EAX=1/0, ECX=name |
| 60 | PIPE_CREATE | — | — | — | pipe_id |
| 61 | PIPE_WRITE | id | buf | len | written |
| 62 | PIPE_READ | id | buf | max | read |
| 63 | PIPE_CLOSE | id | — | — | 0 |
| 64 | SHMGET | key | size | — | shm_id |
| 65 | SHMADDR | shm_id | — | — | ptr |
| 66 | PROCLIST | slot | buf (48 B) | — | 0/-1 |
| 67 | MEMINFO | — | — | — | EAX=free_pages, EBX=total |
| 68 | CHMOD | filename | perms | — | 0/-1 |
| 69 | CHOWN | filename | uid | — | 0/-1 |
| 70 | SYMLINK | linkname | target | — | 0/-1 |
| 71 | READLINK | linkname | buf | — | len/-1 |
| 72 | SETPRIORITY | pid (0=self) | prio | — | 0/-1 |
| 73 | GETPRIORITY | pid (0=self) | — | — | prio/-1 |
| 74 | SIGNAL | pid | signum | — | 0/-1 |
| 75 | SETPGID | pid (0=self) | pgid | — | 0/-1 |
| 76 | GETPGID | pid (0=self) | — | — | pgid/-1 |
| 77 | SIGMASK | op (0–3) | mask | — | old_mask/-1 |
| 78 | TASKNAME | name_ptr | — | — | 0 |
| 79 | REALLOC | ptr | new_size | old_size | new_ptr or 0 |
| 80 | GETENV_SLOT | index | buf (128 B) | — | 0/-1 |
| 81 | DMESG_WRITE | msg_ptr | — | — | 0 |
| 88 | SEM_CREATE | initial_value | — | — | sem_id/-1 |
| 89 | SEM_WAIT | sem_id | — | — | 0/-1 |
| 90 | SEM_POST | sem_id | — | — | 0 |
| 91 | SEM_CLOSE | sem_id | — | — | 0 |
| 92 | WAITPID | pid | — | — | exit_code/-1 |
| 93 | GETMTIME | filename | — | — | EAX=mtime, ECX=ctime |
| 94 | SETMTIME | filename | timestamp (0=now) | — | 0/-1 |

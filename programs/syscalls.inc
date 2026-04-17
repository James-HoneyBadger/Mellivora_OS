;
; syscalls.inc - Shared system call definitions for Mellivora OS programs
;
; Include this at the top of every program:
;   %include "syscalls.inc"
;

; Program header
[BITS 32]
[ORG 0x00200000]

; Jump past shared code to program entry point
jmp start

; System call numbers (INT 0x80)
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
SYS_MKDIR       equ 12          ; Create directory: EBX=name -> EAX=0 success
SYS_READDIR     equ 13          ; Read directory entry: EBX=buf ECX=index -> EAX=type(-1=end) ECX=size
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
SYS_FREAD       equ 30          ; Read entire file: EBX=name ECX=buf -> EAX=bytes
SYS_FWRITE      equ 31          ; Write file: EBX=name ECX=buf EDX=size ESI=type(0=text)
SYS_GETARGS     equ 32          ; Get command-line args: EBX=buf -> EAX=length
SYS_SERIAL_IN   equ 33          ; Read serial port: -> EAX=char or -1
SYS_STDIN_READ  equ 34          ; Read piped stdin: EBX=buf -> EAX=bytes (-1 if none)
SYS_YIELD       equ 35          ; Cooperative yield: switch to next ready task
SYS_MOUSE       equ 36          ; Read mouse: -> EAX=x, EBX=y, ECX=buttons
SYS_FRAMEBUF    equ 37          ; Framebuffer: EBX=sub (0=info,1=set,2=restore)
SYS_GUI         equ 38          ; Burrows GUI: EBX=sub-function
SYS_SOCKET      equ 39          ; Create socket: EBX=type(1=TCP,2=UDP) -> EAX=fd
SYS_CONNECT     equ 40          ; Connect: EBX=fd ECX=ip EDX=port -> EAX=0/-1
SYS_SEND        equ 41          ; Send: EBX=fd ECX=buf EDX=len -> EAX=bytes
SYS_RECV        equ 42          ; Recv: EBX=fd ECX=buf EDX=maxlen -> EAX=bytes
SYS_BIND        equ 43          ; Bind: EBX=fd ECX=port -> EAX=0/-1
SYS_LISTEN      equ 44          ; Listen: EBX=fd -> EAX=0/-1
SYS_ACCEPT      equ 45          ; Accept: EBX=fd -> EAX=new_fd
SYS_DNS         equ 46          ; DNS: EBX=hostname -> EAX=ip (0=fail)
SYS_SOCKCLOSE   equ 47          ; Close socket: EBX=fd -> EAX=0
SYS_PING        equ 48          ; Ping: EBX=ip -> EAX=rtt/-1
SYS_SETDATE     equ 49          ; Set RTC: EBX=buf[sec,min,hr,day,mon,yr] ECX=century
SYS_AUDIO_PLAY  equ 50          ; Play PCM: EBX=buf ECX=len EDX=fmt -> EAX=0/-1
SYS_AUDIO_STOP  equ 51          ; Stop playback -> EAX=0
SYS_AUDIO_STATUS equ 52         ; Query: -> EAX=state(0/1/2) EBX=present
SYS_KILL        equ 53          ; Kill task: EBX=pid -> EAX=0/-1
SYS_GETPID      equ 54          ; Get PID: -> EAX=pid
SYS_CLIPBOARD_COPY  equ 55      ; Copy: EBX=buf ECX=len -> EAX=0
SYS_CLIPBOARD_PASTE equ 56      ; Paste: EBX=buf ECX=maxlen -> EAX=len
SYS_NOTIFY      equ 57          ; Notify: EBX=text EDX=color -> EAX=0
SYS_FILE_OPEN_DLG equ 58        ; Open dialog: EBX=title EDX=filter -> EAX=1/0 ECX=name
SYS_FILE_SAVE_DLG equ 59        ; Save dialog: EBX=title EDX=filter -> EAX=1/0 ECX=name
SYS_PIPE_CREATE equ 60          ; Create pipe: -> EAX=pipe_id
SYS_PIPE_WRITE  equ 61          ; Write pipe: EBX=id ECX=buf EDX=len -> EAX=written
SYS_PIPE_READ   equ 62          ; Read pipe: EBX=id ECX=buf EDX=max -> EAX=read
SYS_PIPE_CLOSE  equ 63          ; Close pipe: EBX=id -> EAX=0
SYS_SHMGET      equ 64          ; Get shared mem: EBX=key ECX=size -> EAX=shm_id
SYS_SHMADDR     equ 65          ; Get shm address: EBX=shm_id -> EAX=ptr
SYS_PROCLIST    equ 66          ; Get task info: EBX=slot ECX=buf(16B) -> EAX=0/-1
SYS_MEMINFO     equ 67          ; Get mem info: -> EAX=free_pages EBX=boot_free_pages
; v3.0 syscalls
SYS_CHMOD       equ 68          ; Change perms: EBX=filename ECX=perms -> EAX=0/-1
SYS_CHOWN       equ 69          ; Change owner: EBX=filename ECX=uid -> EAX=0/-1
SYS_SYMLINK     equ 70          ; Create link: EBX=linkname ECX=target -> EAX=0/-1
SYS_READLINK    equ 71          ; Read link: EBX=linkname ECX=buf -> EAX=len/-1

; v4.0 syscalls
SYS_SETPRIORITY equ 72          ; Set priority: EBX=pid(0=self) ECX=prio -> EAX=0/-1
SYS_GETPRIORITY equ 73          ; Get priority: EBX=pid(0=self) -> EAX=prio/-1
SYS_SIGNAL      equ 74          ; Send signal: EBX=pid ECX=signum -> EAX=0/-1
SYS_SETPGID     equ 75          ; Set PGID: EBX=pid(0=self) ECX=pgid -> EAX=0/-1
SYS_GETPGID     equ 76          ; Get PGID: EBX=pid(0=self) -> EAX=pgid/-1
SYS_SIGMASK     equ 77          ; Signal mask: EBX=op ECX=mask -> EAX=old/-1
SYS_TASKNAME    equ 78          ; Set task name: EBX=name_ptr -> EAX=0
SYS_REALLOC     equ 79          ; Realloc: EBX=ptr ECX=new_size EDX=old_size -> EAX=ptr/0

; v4.0 Priority levels
PRIO_HIGH       equ 0
PRIO_NORMAL     equ 1
PRIO_LOW        equ 2
PRIO_IDLE       equ 3

; v4.0 Signal numbers
SIGINT          equ 2
SIGKILL         equ 9
SIGUSR1         equ 10
SIGUSR2         equ 12
SIGALRM         equ 14
SIGTERM         equ 15
SIGCHLD         equ 17
SIGTSTP         equ 20
SIGCONT         equ 25

; Audio format flags (for SYS_AUDIO_PLAY EDX)
SB_FMT_16BIT    equ (1 << 16)
SB_FMT_STEREO   equ (1 << 17)
SB_FMT_SIGNED   equ (1 << 18)

; SYS_GUI sub-functions (EBX)
GUI_CREATE_WINDOW  equ 0        ; ECX=x|y EDX=w|h ESI=title -> EAX=win_id
GUI_DESTROY_WINDOW equ 1        ; ECX=win_id
GUI_FILL_RECT      equ 2        ; ECX=win_id EDX=x|y ESI=w|h EDI=color
GUI_DRAW_TEXT      equ 3        ; ECX=win_id EDX=x|y ESI=text EDI=color
GUI_POLL_EVENT     equ 4        ; -> EAX=type EBX=param1 ECX=param2
GUI_GET_THEME      equ 5        ; ECX=dest_buf
GUI_SET_THEME      equ 6        ; ECX=src_buf
GUI_DRAW_PIXEL     equ 7        ; ECX=win_id EDX=x|y ESI=color
GUI_DRAW_LINE      equ 8        ; ECX=win_id EDX=x1|y1 ESI=x2|y2 EDI=color
GUI_COMPOSE        equ 9        ; Compose desktop to backbuffer
GUI_FLIP           equ 10       ; Flip backbuffer to screen
GUI_DESKTOP        equ 11       ; Reserved
GUI_DRAW_BUTTON    equ 20       ; ECX=win_id ESI=param_block
GUI_DRAW_CHECKBOX  equ 21       ; ECX=win_id ESI=param_block
GUI_DRAW_PROGRESS  equ 22       ; ECX=win_id ESI=param_block
GUI_DRAW_TEXTBOX   equ 23       ; ECX=win_id ESI=param_block
GUI_DRAW_LISTBOX   equ 24       ; ECX=win_id ESI=param_block
GUI_DRAW_LABEL     equ 25       ; ECX=win_id ESI=param_block
GUI_DRAW_RECT      equ 26       ; ECX=win_id ESI=param_block

; GUI event types
EVT_NONE        equ 0
EVT_MOUSE_CLICK equ 1
EVT_MOUSE_MOVE  equ 2
EVT_KEY_PRESS   equ 3
EVT_CLOSE       equ 4

; VGA constants
VGA_BASE        equ 0xB8000
VGA_WIDTH       equ 80
VGA_HEIGHT      equ 25

; Arrow key codes
KEY_UP          equ 0x80
KEY_DOWN        equ 0x81
KEY_LEFT        equ 0x82
KEY_RIGHT       equ 0x83

; Function key codes
KEY_F1          equ 0x84
KEY_F2          equ 0x85
KEY_F3          equ 0x86
KEY_F4          equ 0x87
KEY_F5          equ 0x88
KEY_F6          equ 0x89
KEY_F7          equ 0x8A
KEY_F8          equ 0x8B
KEY_F9          equ 0x8C
KEY_F10         equ 0x8D

; Default color
COLOR_DEFAULT   equ 0x07

; File types (for SYS_FWRITE and SYS_STAT)
FTYPE_TEXT      equ 1
FTYPE_DIR       equ 2
FTYPE_EXEC      equ 3
FTYPE_BATCH     equ 4
FTYPE_LINK      equ 5

;---------------------------------------
; Shared print_dec: print EAX as decimal
; Clobbers no registers (uses pushad/popad)
;---------------------------------------
print_dec:
        pushad
        xor ecx, ecx
        mov ebx, 10
        test eax, eax
        jnz .pd_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        jmp .pd_done
.pd_nonzero:
.pd_push:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .pd_push
.pd_pop:
        pop ebx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pd_pop
.pd_done:
        popad
        ret

; top.asm - Real-time system activity monitor for Mellivora OS
; Displays live uptime, memory stats, directory listing with bar graph,
; and a ticking clock. Refreshes every second. Press 'q' or ESC to exit.
%include "syscalls.inc"

SCREEN_W        equ 80
SCREEN_H        equ 25
REFRESH_TICKS   equ 100         ; 1 second at 100Hz
MAX_DIR_ENTRIES equ 16          ; Max directory entries to show

start:
        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [start_ticks], eax

        ; Get initial CWD
        mov eax, SYS_GETCWD
        mov ebx, cwd_buf
        int 0x80

.main_loop:
        call draw_screen

        ; Wait for refresh, checking keys
        mov ecx, REFRESH_TICKS
.tick_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .no_key
        cmp al, 'q'
        je .exit
        cmp al, 'Q'
        je .exit
        cmp al, 27             ; ESC
        je .exit
.no_key:
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        dec ecx
        jnz .tick_loop

        jmp .main_loop

.exit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; draw_screen - Render the full monitor display
;---------------------------------------
draw_screen:
        PUSHALL

        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; === Header bar (row 0) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F           ; White on blue
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, header_str
        int 0x80

        ; Pad rest of header row with spaces
        mov eax, SYS_SETCURSOR
        mov ebx, 57
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, header_pad
        int 0x80

        ; === Uptime (row 2) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; Light cyan
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_uptime
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; White
        int 0x80

        ; Get current ticks, compute seconds
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, 100
        div ecx                 ; EAX = total seconds
        mov [total_secs], eax

        ; Hours
        xor edx, edx
        mov ecx, 3600
        div ecx
        push rdx
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 'h'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Minutes
        pop rax
        xor edx, edx
        mov ecx, 60
        div ecx
        push rdx
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 'm'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Seconds
        pop rax
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 's'
        int 0x80

        ; === Date (row 2, right side) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 40
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_date
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_DATE
        mov ebx, date_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, date_buf
        int 0x80

        ; === CWD (row 3) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 3
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_cwd
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, cwd_buf
        int 0x80

        ; === Separator (row 4) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 4
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, separator
        int 0x80

        ; === Memory visualization (row 5-6) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_mem_map
        int 0x80

        ; Draw a memory map bar using direct VGA
        mov ecx, 6              ; row
        xor ebx, ebx           ; col
        mov edx, 0              ; segment index
.mem_bar:
        cmp ebx, 64
        jge .mem_bar_done

        ; Color code by memory region
        cmp ebx, 8
        jb .mem_kern
        cmp ebx, 16
        jb .mem_prog
        cmp ebx, 24
        jb .mem_pmm
        cmp ebx, 32
        jb .mem_heap
        jmp .mem_free
.mem_kern:
        mov al, 0xDB           ; Full block
        mov ah, 0x4F           ; White on red = kernel
        jmp .mem_write
.mem_prog:
        mov al, 0xDB
        mov ah, 0x2F           ; White on green = programs
        jmp .mem_write
.mem_pmm:
        mov al, 0xDB
        mov ah, 0x1F           ; White on blue = PMM bitmap
        jmp .mem_write
.mem_heap:
        mov al, 0xDB
        mov ah, 0x5F           ; White on magenta = heap
        jmp .mem_write
.mem_free:
        mov al, 0xB0           ; Light shade = free
        mov ah, 0x08           ; Dark gray
.mem_write:
        ; Write to VGA: offset = (row*80+col)*2 + VGA_BASE
        push rbx
        push rcx
        imul ecx, 80
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop rcx
        pop rbx
        inc ebx
        jmp .mem_bar
.mem_bar_done:

        ; Legend (row 6 right side)
        mov eax, SYS_SETCURSOR
        mov ebx, 66
        mov ecx, 6
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, legend_str
        int 0x80

        ; === Directory listing header (row 8) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F           ; White on blue
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, dir_header
        int 0x80

        ; === Directory entries (rows 9-24) ===
        xor esi, esi            ; entry index
        mov dword [file_count], 0
        mov dword [total_size], 0
.dir_loop:
        ; Read directory entry
        mov eax, SYS_READDIR
        mov ebx, dirent_buf
        mov ecx, esi
        int 0x80
        cmp eax, -1
        je .dir_done
        cmp eax, 0              ; FTYPE_FREE
        je .dir_skip

        ; We have a valid entry
        inc dword [file_count]
        add [total_size], ecx   ; ECX = file size from readdir

        ; Only display first MAX_DIR_ENTRIES
        mov edx, [file_count]
        cmp edx, MAX_DIR_ENTRIES
        ja .dir_skip

        ; Set cursor to row 8 + file_count
        push rax
        push rcx
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, [file_count]
        add ecx, 8
        cmp ecx, 24
        jg .dir_skip_pop
        int 0x80

        ; Set color based on type
        pop rcx                 ; file size
        pop rax                 ; file type
        push rcx
        push rax

        cmp eax, 2              ; Directory
        je .dir_type_dir
        cmp eax, 3              ; Executable
        je .dir_type_exec
        cmp eax, 4              ; Batch
        je .dir_type_batch
        ; Default: text file
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp .dir_print_entry
.dir_type_dir:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        jmp .dir_print_entry
.dir_type_exec:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        jmp .dir_print_entry
.dir_type_batch:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0D
        int 0x80

.dir_print_entry:
        ; Print filename
        mov eax, SYS_PRINT
        mov ebx, dirent_buf
        int 0x80

        ; Print size in parentheses
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, size_open
        int 0x80

        pop rax                 ; file type
        pop rcx                 ; file size
        mov eax, ecx
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, size_close
        int 0x80
        jmp .dir_skip

.dir_skip_pop:
        pop rcx
        pop rax

.dir_skip:
        inc esi
        cmp esi, 256
        jb .dir_loop
.dir_done:

        ; === Status bar (row 24) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70           ; Black on light gray
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 24
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, status_left
        int 0x80
        mov eax, [file_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, status_files
        int 0x80
        mov eax, [total_size]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, status_bytes
        int 0x80

        ; Pad to end of row
        mov eax, SYS_PRINT
        mov ebx, status_right
        int 0x80

        POPALL
        ret

; === Data ===
header_str:     db " Mellivora OS  -  System Monitor (top)   ", 0
header_pad:     db "                       ", 0
lbl_uptime:     db " Uptime: ", 0
lbl_date:       db "Date: ", 0
lbl_cwd:        db " CWD:    ", 0
lbl_mem_map:    db " Memory Map:  [0MB", 0
separator:      db "----------------------------------------------------------------------", 0x0A, 0
legend_str:     db "K P B H F", 0
dir_header:     db " File                       Size  Type                                    ", 0
size_open:      db " (", 0
size_close:     db " bytes)", 0
status_left:    db " ", 0
status_files:   db " files  |  ", 0
status_bytes:   db " bytes total            Press q to quit", 0
status_right:   db "         ", 0

date_buf:       times 32 db 0
cwd_buf:        times 256 db 0
dirent_buf:     times 256 db 0
start_ticks:    dd 0
total_secs:     dd 0
file_count:     dd 0
total_size:     dd 0

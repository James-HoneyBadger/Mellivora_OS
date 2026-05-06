; top.asm - Real-time process + system monitor for Mellivora OS
; Shows: uptime, memory usage bar, CPU/process table, disk stats
; Refreshes every second. Press 'q' or ESC to exit, 'k' to kill PID.
%include "syscalls.inc"

VGA_BASE        equ 0xB8000
SCREEN_W        equ 80
SCREEN_H        equ 25
MAX_TASKS       equ 128
PROC_BUF_SIZE   equ 48    ; matches sys_proclist output

; Task states
TASK_FREE       equ 0
TASK_READY      equ 1
TASK_RUNNING    equ 2
TASK_BLOCKED    equ 3
TASK_STOPPED    equ 4
TASK_ZOMBIE     equ 5

; Colors
COL_HDR         equ 0x1F   ; white on blue
COL_LABEL       equ 0x0B   ; light cyan
COL_VALUE       equ 0x0F   ; bright white
COL_RUNNING     equ 0x0A   ; green
COL_READY       equ 0x0E   ; yellow
COL_BLOCKED     equ 0x07   ; grey
COL_ZOMBIE      equ 0x0C   ; red
COL_STOPPED     equ 0x0D   ; magenta
COL_BAR_FREE    equ 0x2F   ; white on green
COL_BAR_USED    equ 0x4F   ; white on red
COL_STATUS      equ 0x70   ; black on grey

start:
        mov eax, SYS_GETTIME
        int 0x80
        mov [start_ticks], eax

.main_loop:
        call .draw_screen

        ; Wait ~1s checking for keypress
        mov ecx, 100
.wait:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .no_key
        cmp al, 'q'
        je .exit
        cmp al, 'Q'
        je .exit
        cmp al, 27
        je .exit
.no_key:
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        dec ecx
        jnz .wait
        jmp .main_loop

.exit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ==========================================
.draw_screen:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; ---- Header bar (row 0) ----
        call .vga_row0_blue
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hdr_str
        int 0x80

        ; ---- Uptime (row 1) ----
        mov eax, SYS_SETCOLOR
        mov ebx, COL_LABEL
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_uptime
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_VALUE
        int 0x80
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, 100
        div ecx     ; total seconds
        mov ebx, eax
        xor edx, edx
        mov ecx, 3600
        div ecx
        push edx                                    ; save remainder
        call .print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 'h'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop eax
        xor edx, edx
        mov ecx, 60
        div ecx
        push edx
        call .print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 'm'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop eax
        call .print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 's'
        int 0x80

        ; Date/time (row 1 right)
        mov eax, SYS_SETCURSOR
        mov ebx, 50
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_LABEL
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_date
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_VALUE
        int 0x80
        mov eax, SYS_DATE
        mov ebx, date_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, date_buf
        int 0x80

        ; ---- Memory bar (row 2) ----
        mov eax, SYS_MEMINFO
        int 0x80
        ; EAX=free_pages EBX=total_pages
        mov [mem_free], eax
        mov [mem_total], ebx
        mov eax, SYS_SETCOLOR
        mov ebx, COL_LABEL
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_mem
        int 0x80
        ; Print used MB / total MB
        mov eax, SYS_SETCOLOR
        mov ebx, COL_VALUE
        int 0x80
        mov eax, [mem_total]
        sub eax, [mem_free]     ; used pages
        shr eax, 8              ; pages/256 ≈ MB (4KB pages → 256 pages/MB)
        call .print_dec
        mov eax, SYS_PRINT
        mov ebx, lbl_mb_slash
        int 0x80
        mov eax, [mem_total]
        shr eax, 8
        call .print_dec
        mov eax, SYS_PRINT
        mov ebx, lbl_mb_used
        int 0x80

        ; Draw 40-char bar
        mov eax, [mem_total]
        test eax, eax
        jz .skip_membar
        mov ecx, eax
        mov eax, [mem_total]
        sub eax, [mem_free]     ; used
        imul eax, 40
        xor edx, edx
        div ecx                 ; EAX = used bar length (0..40)
        mov [mem_bar_used], eax

        ; Draw bar at row 2 col 30
        mov ebx, 30
        mov ecx, 2
        xor edi, edi
.membar_draw:
        cmp edi, 40
        jge .skip_membar
        cmp edi, [mem_bar_used]
        jl .mb_used_col
        mov ah, COL_BAR_FREE
        jmp .mb_write
.mb_used_col:
        mov ah, COL_BAR_USED
.mb_write:
        mov al, 0xDB
        push ebx
        push ecx
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop ecx
        pop ebx
        inc ebx
        inc edi
        jmp .membar_draw
.skip_membar:

        ; ---- Process table header (row 4) ----
        mov eax, SYS_SETCOLOR
        mov ebx, COL_HDR
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 4
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, proc_hdr
        int 0x80

        ; ---- Process rows (rows 5..22) ----
        xor esi, esi            ; slot index
        mov dword [show_row], 5
        mov dword [proc_count], 0
        mov dword [running_count], 0

.proc_loop:
        cmp esi, MAX_TASKS
        jge .proc_done
        mov eax, SYS_PROCLIST
        mov ebx, esi
        mov ecx, proc_buf
        int 0x80
        cmp eax, -1
        je .proc_next

        ; Check state
        mov eax, [proc_buf]     ; state
        cmp eax, TASK_FREE
        je .proc_next
        inc dword [proc_count]
        cmp eax, TASK_RUNNING
        jne .not_running
        inc dword [running_count]
.not_running:
        mov eax, [show_row]
        cmp eax, 23
        jge .proc_next   ; no more screen rows

        ; Set cursor
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, [show_row]
        int 0x80

        ; Set color by state
        mov eax, [proc_buf]
        cmp eax, TASK_RUNNING
        je .sc_run
        cmp eax, TASK_READY
        je .sc_rdy
        cmp eax, TASK_BLOCKED
        je .sc_blk
        cmp eax, TASK_ZOMBIE
        je .sc_zom
        cmp eax, TASK_STOPPED
        je .sc_stp
        mov ebx, COL_BLOCKED
        jmp .sc_set
.sc_run: mov ebx, COL_RUNNING
jmp .sc_set
.sc_rdy: mov ebx, COL_READY
jmp .sc_set
.sc_blk: mov ebx, COL_BLOCKED
jmp .sc_set
.sc_zom: mov ebx, COL_ZOMBIE
jmp .sc_set
.sc_stp: mov ebx, COL_STOPPED
.sc_set:
        mov eax, SYS_SETCOLOR
        int 0x80

        ; Print slot
        mov eax, esi
        call .print_dec_w4
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Print PID
        mov eax, [proc_buf + 4]
        call .print_dec_w6
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Print state string
        mov eax, [proc_buf]
        call .print_state

        ; Print priority
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, [proc_buf + 16]
        call .print_prio

        ; Print name
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        lea ebx, [proc_buf + 32]
        mov eax, SYS_PRINT
        int 0x80

        inc dword [show_row]

.proc_next:
        inc esi
        jmp .proc_loop
.proc_done:

        ; ---- Totals row (row 23) ----
        mov eax, SYS_SETCOLOR
        mov ebx, COL_LABEL
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 23
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_procs
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_VALUE
        int 0x80
        mov eax, [proc_count]
        call .print_dec
        mov eax, SYS_PRINT
        mov ebx, lbl_running
        int 0x80
        mov eax, [running_count]
        call .print_dec
        mov eax, SYS_PRINT
        mov ebx, lbl_running2
        int 0x80

        ; ---- Status bar (row 24) ----
        mov eax, SYS_SETCOLOR
        mov ebx, COL_STATUS
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 24
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, status_bar
        int 0x80

        popad
        ret

; Fill header row with blue background (direct VGA)
.vga_row0_blue:
        push eax
        push ecx
        push edi
        mov edi, VGA_BASE
        mov ecx, SCREEN_W
.vrb:
        mov word [edi], 0x1F20  ; space on blue
        add edi, 2
        dec ecx
        jnz .vrb
        pop edi
        pop ecx
        pop eax
        ret

; Print state name (EAX = state)
.print_state:
        push ebx
        cmp eax, TASK_FREE
        je .ps_free
        cmp eax, TASK_READY
        je .ps_rdy
        cmp eax, TASK_RUNNING
        je .ps_run
        cmp eax, TASK_BLOCKED
        je .ps_blk
        cmp eax, TASK_STOPPED
        je .ps_stp
        cmp eax, TASK_ZOMBIE
        je .ps_zom
        mov ebx, ps_unk
        jmp .ps_pr
.ps_free: mov ebx, ps_free
jmp .ps_pr
.ps_rdy:  mov ebx, ps_ready
jmp .ps_pr
.ps_run:  mov ebx, ps_run
jmp .ps_pr
.ps_blk:  mov ebx, ps_blk
jmp .ps_pr
.ps_stp:  mov ebx, ps_stp
jmp .ps_pr
.ps_zom:  mov ebx, ps_zom
.ps_pr:   mov eax, SYS_PRINT
int 0x80
        pop ebx
        ret

; Print priority name
.print_prio:
        push ebx
        cmp eax, 0
        je .pp_hi
        cmp eax, 1
        je .pp_nm
        cmp eax, 2
        je .pp_lo
        mov ebx, pp_idle
        jmp .pp_pr
.pp_hi: mov ebx, pp_high
jmp .pp_pr
.pp_nm: mov ebx, pp_norm
jmp .pp_pr
.pp_lo: mov ebx, pp_low
.pp_pr: mov eax, SYS_PRINT
int 0x80
        pop ebx
        ret

; print EAX as decimal (left-justified)
.print_dec:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        mov edi, dec_buf + 15
        mov byte [edi], 0
        mov ecx, 10
.pd_lp: xor edx, edx
div ecx
        add dl, '0'
        dec edi
        mov [edi], dl
        test eax, eax
        jnz .pd_lp
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; print EAX as decimal, right-justified in 4-char field
.print_dec_w4:
        push eax
        push ebx
        push ecx
        push edx
        ; convert to string
        mov ecx, dec_buf + 7
        mov byte [ecx], 0
        mov edx, 10
.pd4_lp: dec ecx
        xor ebx, ebx
        push ecx
        mov ecx, edx
        div ecx
        pop ecx
        add bl, '0'
        mov [ecx], bl
        test eax, eax
        jnz .pd4_lp
        ; pad to 4 chars
        push ecx
        mov eax, dec_buf + 7
        sub eax, ecx            ; length
        pop ecx
        mov ebx, 4
        sub ebx, eax
.pd4_pad: test ebx, ebx
jle .pd4_done
        push ebx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop ebx
        dec ebx
        jmp .pd4_pad
.pd4_done:
        mov eax, SYS_PRINT
        mov ebx, ecx
        int 0x80
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; print EAX right-justified in 6-char field
.print_dec_w6:
        push eax
        ; Just use print_dec padded with spaces
        mov [tmp_val], eax
        ; count digits
        xor ecx, ecx
        push eax
.pdw6_cnt:
        xor edx, edx
        push ecx
        mov ecx, 10
        div ecx
        pop ecx
        inc ecx
        test eax, eax
        jnz .pdw6_cnt
        pop eax
        mov ebx, 6
        sub ebx, ecx
.pdw6_pad:
        test ebx, ebx
        jle .pdw6_do
        push ebx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop ebx
        dec ebx
        jmp .pdw6_pad
.pdw6_do:
        mov eax, [tmp_val]
        call .print_dec
        pop eax
        ret

;--- Data ---
hdr_str:     db " Mellivora OS  top  -  System Process Monitor                                  ",0
lbl_uptime:  db "  Uptime: ",0
lbl_date:    db "Date: ",0
lbl_mem:     db "  Mem: ",0
lbl_mb_slash: db " MB / ",0
lbl_mb_used: db " MB   [",0
proc_hdr:    db " Slot    PID  State    Prio  Name                            ",0
lbl_procs:   db "  Procs: ",0
lbl_running: db "  Running: ",0
lbl_running2: db "                                                    ",0
status_bar:  db "  q=quit                                                                        ",0
ps_free:     db "FREE   ",0
ps_ready:    db "READY  ",0
ps_run:      db "RUN    ",0
ps_blk:      db "BLOCK  ",0
ps_stp:      db "STOP   ",0
ps_zom:      db "ZOMBIE ",0
ps_unk:      db "?      ",0
pp_high:     db "HIGH",0
pp_norm:     db "NORM",0
pp_low:      db "LOW ",0
pp_idle:     db "IDLE",0
dec_buf:     times 20 db 0

;--- BSS ---
proc_buf:       times PROC_BUF_SIZE db 0
date_buf:       times 32 db 0
start_ticks:    dd 0
mem_free:       dd 0
mem_total:      dd 0
mem_bar_used:   dd 0
show_row:       dd 0
proc_count:     dd 0
running_count:  dd 0
tmp_val:        dd 0

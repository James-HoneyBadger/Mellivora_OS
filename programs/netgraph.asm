; netgraph.asm - Live network latency graph
;
; Pings a configurable target IP every second and draws a scrolling
; bar graph of round-trip times in VGA text mode.  Demonstrates:
;   - SYS_SOCKET / SYS_PING
;   - SYS_GETTIME for elapsed-time measurement
;   - VGA double-buffer-style character screen writes
;   - SYS_GETARGS for the optional target IP argument
;
; Usage:  netgraph [ip-address]    (default: 8.8.8.8)
; Q or ESC to quit.
%include "syscalls.inc"

SCREEN_W        equ 80
SCREEN_H        equ 25
GRAPH_X         equ 2
GRAPH_Y         equ 4
GRAPH_W         equ 76           ; columns of history
GRAPH_H         equ 18           ; rows of bar height
MAX_RTT_MS      equ 500          ; RTT clamped to this for display
POLL_TICKS      equ 100          ; ~1 s between pings (100 ticks @ 100 Hz)
BAR_CHAR        equ 0xDB         ; █ full block
TICK_CHAR       equ 0xC4         ; ─ axis
SIDE_CHAR       equ 0xB3         ; │ side border

COLOR_TITLE     equ 0x0F
COLOR_AXIS      equ 0x07
COLOR_BAR_OK    equ 0x0A         ; green  (< 50 ms)
COLOR_BAR_WARN  equ 0x0E         ; yellow (50-150 ms)
COLOR_BAR_SLOW  equ 0x0C         ; red    (> 150 ms)
COLOR_TIMEOUT   equ 0x04         ; dark red  (timeout)
COLOR_LABEL     equ 0x0B
COLOR_KEY       equ 0x0E
COLOR_DIM       equ 0x07

DEFAULT_IP      equ (8 | (8 << 8) | (8 << 16) | (8 << 24))  ; 8.8.8.8 little-endian

start:
        ; Parse optional IP argument
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .use_default_ip

        ; Very simple dotted-decimal parser for the first word in arg_buf
        mov esi, arg_buf
        call parse_ip           ; returns EAX=ip or 0 on failure
        test eax, eax
        jz .use_default_ip
        mov [target_ip], eax
        jmp .draw_init

.use_default_ip:
        mov dword [target_ip], DEFAULT_IP

.draw_init:
        call draw_frame

.main_loop:
        ; Check for quit key (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .do_ping
        cmp eax, 'q'
        je .exit
        cmp eax, 'Q'
        je .exit
        cmp eax, 0x1B           ; ESC
        je .exit

.do_ping:
        ; Ping the target
        mov eax, SYS_PING
        mov ebx, [target_ip]
        int 0x80                ; EAX = rtt ticks or -1 (timeout)

        ; Convert ticks -> ms  (ticks * 10 at 100 Hz)
        cmp eax, -1
        je .ping_timeout
        imul eax, 10
        cmp eax, MAX_RTT_MS
        jle .rtt_ok
        mov eax, MAX_RTT_MS
.rtt_ok:
        mov [last_rtt], eax
        jmp .record_sample

.ping_timeout:
        mov dword [last_rtt], -1

.record_sample:
        call scroll_history
        call draw_graph
        call draw_status

        ; Wait ~1 s before next ping
        mov eax, SYS_SLEEP
        mov ebx, POLL_TICKS
        int 0x80
        jmp .main_loop

.exit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DIM
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;============================================================
; draw_frame — draw static border, axis, labels
;============================================================
draw_frame:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; Title bar
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_TITLE
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, 0
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        ; Target line
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_LABEL
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, 1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_target_lbl
        int 0x80
        ; Print IP in dotted form
        call print_ip

        ; Key hint
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_KEY
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_keys
        int 0x80

        ; X axis (bottom of graph)
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_AXIS
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, GRAPH_X
        mov ecx, GRAPH_Y + GRAPH_H
        int 0x80
        mov ecx, GRAPH_W
.axis_loop:
        mov eax, SYS_PUTCHAR
        mov ebx, TICK_CHAR
        int 0x80
        dec ecx
        jnz .axis_loop

        ; Y axis labels
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_AXIS
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, GRAPH_Y
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_500ms
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, GRAPH_Y + GRAPH_H / 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_250ms
        int 0x80

        popad
        ret

;============================================================
; scroll_history — shift history[] left, insert last_rtt
;============================================================
scroll_history:
        pushad
        mov esi, history + 4
        mov edi, history
        mov ecx, GRAPH_W - 1
        rep movsd
        mov eax, [last_rtt]
        mov [history + (GRAPH_W - 1) * 4], eax
        popad
        ret

;============================================================
; draw_graph — render bar chart from history[]
;============================================================
draw_graph:
        pushad
        xor ebp, ebp            ; column index
.col_loop:
        cmp ebp, GRAPH_W
        jge .done

        mov eax, [history + ebp * 4]
        cmp eax, -1
        je .draw_timeout

        ; bar_height = rtt * GRAPH_H / MAX_RTT_MS
        imul eax, GRAPH_H
        xor edx, edx
        mov ecx, MAX_RTT_MS
        div ecx
        cmp eax, GRAPH_H
        jle .clamp_ok
        mov eax, GRAPH_H
.clamp_ok:
        mov [bar_h], eax

        ; Choose colour
        mov ecx, [history + ebp * 4]
        mov edx, COLOR_BAR_OK
        cmp ecx, 50
        jl .color_done
        mov edx, COLOR_BAR_WARN
        cmp ecx, 150
        jl .color_done
        mov edx, COLOR_BAR_SLOW
.color_done:

        ; Draw bar (filled from bottom up)
        mov eax, SYS_SETCOLOR
        mov ebx, edx
        int 0x80

        mov esi, GRAPH_H        ; row from top of graph area
.row_loop:
        dec esi
        cmp esi, 0
        jl .row_done
        ; plot row = GRAPH_Y + (GRAPH_H - 1 - esi)
        mov ecx, GRAPH_H
        dec ecx
        sub ecx, esi
        add ecx, GRAPH_Y
        mov eax, SYS_SETCURSOR
        mov ebx, ebp
        add ebx, GRAPH_X
        int 0x80

        ; is this row within bar height?
        mov edx, GRAPH_H
        dec edx
        sub edx, esi
        cmp edx, [bar_h]
        jl .plot_bar

        ; empty above bar — erase
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DIM
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Restore bar colour for next iteration
        mov eax, SYS_SETCOLOR
        push dword [history + ebp * 4]
        mov ecx, [esp]
        pop ecx
        mov edx, COLOR_BAR_OK
        cmp ecx, 50
        jl .rc_done
        mov edx, COLOR_BAR_WARN
        cmp ecx, 150
        jl .rc_done
        mov edx, COLOR_BAR_SLOW
.rc_done:
        mov ebx, edx
        int 0x80
        jmp .row_loop

.plot_bar:
        mov eax, SYS_PUTCHAR
        mov ebx, BAR_CHAR
        int 0x80
        jmp .row_loop

.row_done:
        jmp .next_col

.draw_timeout:
        ; Timeout column: draw a small X marker at bottom of graph area
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_TIMEOUT
        int 0x80
        ; Clear whole column first
        mov esi, 0
.to_clear:
        cmp esi, GRAPH_H
        jge .to_done
        mov eax, SYS_SETCURSOR
        mov ebx, ebp
        add ebx, GRAPH_X
        mov ecx, GRAPH_Y
        add ecx, esi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc esi
        jmp .to_clear
.to_done:
        mov eax, SYS_SETCURSOR
        mov ebx, ebp
        add ebx, GRAPH_X
        mov ecx, GRAPH_Y + GRAPH_H - 1
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'x'
        int 0x80

.next_col:
        inc ebp
        jmp .col_loop
.done:
        popad
        ret

;============================================================
; draw_status — bottom status bar with latest RTT
;============================================================
draw_status:
        pushad
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, GRAPH_Y + GRAPH_H + 1
        int 0x80

        mov eax, [last_rtt]
        cmp eax, -1
        je .timeout_status

        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_LABEL
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_rtt_lbl
        int 0x80

        mov eax, [last_rtt]
        mov edi, num_buf
        call int2str
        mov eax, SYS_PRINT
        mov ebx, num_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_ms
        int 0x80
        jmp .status_done

.timeout_status:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_TIMEOUT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_timeout
        int 0x80

.status_done:
        ; pad rest of line
        mov ecx, 20
.pad:   mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jnz .pad

        popad
        ret

;============================================================
; int2str — EAX = value, EDI = dest buffer (null-terminated)
;============================================================
int2str:
        pushad
        test eax, eax
        jnz .nonzero
        mov byte [edi], '0'
        mov byte [edi+1], 0
        popad
        ret
.nonzero:
        ; reverse order trick
        mov esi, num_tmp
        xor ecx, ecx
.div_loop:
        xor edx, edx
        mov ebx, 10
        div ebx
        add dl, '0'
        mov [esi + ecx], dl
        inc ecx
        test eax, eax
        jnz .div_loop
        ; reverse into edi
        dec ecx
.rev_loop:
        mov al, [esi + ecx]
        stosb
        dec ecx
        jns .rev_loop
        mov byte [edi], 0
        popad
        ret

;============================================================
; parse_ip — ESI = string -> EAX = IP dword or 0
;============================================================
parse_ip:
        push ebx
        push ecx
        push edx
        push esi
        xor eax, eax
        xor ecx, ecx   ; octet index
.pi_octet:
        xor ebx, ebx   ; accumulator for current octet
.pi_digit:
        xor edx, edx
        lodsb
        test al, al
        jz .pi_end
        cmp al, '.'
        je .pi_sep
        cmp al, '0'
        jl .pi_fail
        cmp al, '9'
        jg .pi_fail
        sub al, '0'
        imul ebx, 10
        add ebx, eax
        cmp ebx, 255
        jg .pi_fail
        jmp .pi_digit
.pi_sep:
        ; shift EAX left 8, OR in octet
        shl eax, 8
        or al, bl
        inc ecx
        cmp ecx, 3
        jl .pi_octet
        ; This is the last octet
        jmp .pi_digit
.pi_end:
        cmp ecx, 3
        jne .pi_fail
        shl eax, 8
        or al, bl
        ; swap to little-endian dword order
        bswap eax
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.pi_fail:
        xor eax, eax
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;============================================================
; print_ip — print [target_ip] in dotted decimal
;============================================================
print_ip:
        pushad
        mov eax, [target_ip]
        mov edi, ip_print_buf
        ; Extract 4 octets from little-endian dword
        mov ecx, 4
.pi_loop:
        push eax
        and eax, 0xFF
        push edi
        call int2str
        pop edi
        ; advance past written chars
.pi_adv: inc edi
        cmp byte [edi], 0
        jnz .pi_adv
        dec ecx
        jz .pi_done
        mov byte [edi], '.'
        inc edi
        pop eax
        shr eax, 8
        jmp .pi_loop
.pi_done:
        pop eax
        mov byte [edi], 0
        mov eax, SYS_PRINT
        mov ebx, ip_print_buf
        int 0x80
        popad
        ret

; ---------------------------------------------------------------------------
msg_title:      db "  Network Latency Graph - Mellivora OS", 0
msg_target_lbl: db "  Target: ", 0
msg_keys:       db "  Q=quit", 0
msg_rtt_lbl:    db "  Last RTT: ", 0
msg_ms:         db " ms  ", 0
msg_timeout:    db "  Request timed out  ", 0
lbl_500ms:      db " 500", 0
lbl_250ms:      db " 250", 0

target_ip:      dd DEFAULT_IP
last_rtt:       dd -1
bar_h:          dd 0

; GRAPH_W history slots, -1 = timeout
history:        times GRAPH_W dd -1

num_buf:        times 16 db 0
num_tmp:        times 16 db 0
arg_buf:        times 256 db 0
ip_print_buf:   times 20 db 0

; debug.asm - System Debug & Memory Inspector
; Usage: debug [address]
; Provides interactive memory hex dump, register display, and search.
; Commands: d=dump, g=goto, s=search, r=registers, n=next page, p=prev, q=quit

%include "syscalls.inc"

BYTES_PER_LINE  equ 16
LINES_PER_PAGE  equ 20

start:
        ; Parse optional start address
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp byte [arg_buf], 0
        je .use_default
        mov esi, arg_buf
        call parse_hex
        mov [view_addr], eax
        jmp .main
.use_default:
        mov dword [view_addr], 0x00200000  ; default: kernel start

.main:
        ; Print banner
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, banner_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

.prompt:
        ; Show current address and prompt
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Read command line
        mov esi, cmd_buf
        xor ecx, ecx
.read_cmd:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 0
        je .read_cmd
        cmp al, 13
        je .exec_cmd
        cmp al, 8
        je .cmd_bs
        cmp ecx, 62
        jge .read_cmd
        mov [esi + ecx], al
        inc ecx
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .read_cmd
.cmd_bs:
        test ecx, ecx
        jz .read_cmd
        dec ecx
        mov byte [esi + ecx], 0
        mov eax, SYS_PUTCHAR
        mov ebx, 8
        int 0x80
        mov ebx, ' '
        int 0x80
        mov ebx, 8
        int 0x80
        jmp .read_cmd
.exec_cmd:
        mov byte [esi + ecx], 0
        ; Print newline
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        ; Parse command
        cmp byte [cmd_buf], 'q'
        je .quit
        cmp byte [cmd_buf], 'd'
        je .cmd_dump
        cmp byte [cmd_buf], 'n'
        je .cmd_next
        cmp byte [cmd_buf], 'p'
        je .cmd_prev
        cmp byte [cmd_buf], 'g'
        je .cmd_goto
        cmp byte [cmd_buf], 'r'
        je .cmd_regs
        cmp byte [cmd_buf], 's'
        je .cmd_search
        cmp byte [cmd_buf], 'w'
        je .cmd_write
        cmp byte [cmd_buf], 'h'
        je .cmd_help
        cmp byte [cmd_buf], '?'
        je .cmd_help
        cmp byte [cmd_buf], 0
        je .cmd_dump           ; Enter = dump current page

        mov eax, SYS_PRINT
        mov ebx, msg_unknown
        int 0x80
        jmp .prompt

.quit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; d - Dump memory at current address
;---------------------------------------
.cmd_dump:
        ; Check for optional address: "d XXXX"
        cmp byte [cmd_buf + 1], ' '
        jne .dump_current
        lea esi, [cmd_buf + 2]
        call parse_hex
        mov [view_addr], eax
.dump_current:
        call dump_page
        jmp .prompt

;---------------------------------------
; n - Next page
;---------------------------------------
.cmd_next:
        mov eax, BYTES_PER_LINE * LINES_PER_PAGE
        add [view_addr], eax
        call dump_page
        jmp .prompt

;---------------------------------------
; p - Previous page
;---------------------------------------
.cmd_prev:
        mov eax, BYTES_PER_LINE * LINES_PER_PAGE
        sub [view_addr], eax
        call dump_page
        jmp .prompt

;---------------------------------------
; g XXXXXXXX - Go to address
;---------------------------------------
.cmd_goto:
        cmp byte [cmd_buf + 1], ' '
        jne .prompt
        lea esi, [cmd_buf + 2]
        call parse_hex
        mov [view_addr], eax
        call dump_page
        jmp .prompt

;---------------------------------------
; r - Display registers (captured at entry)
;---------------------------------------
.cmd_regs:
        call show_registers
        jmp .prompt

;---------------------------------------
; s XX [XX ...] - Search for byte pattern
;---------------------------------------
.cmd_search:
        cmp byte [cmd_buf + 1], ' '
        jne .prompt
        call do_search
        jmp .prompt

;---------------------------------------
; w ADDR XX [XX ...] - Write bytes to memory
;---------------------------------------
.cmd_write:
        cmp byte [cmd_buf + 1], ' '
        jne .prompt
        call do_write
        jmp .prompt

;---------------------------------------
; h - Help
;---------------------------------------
.cmd_help:
        mov eax, SYS_PRINT
        mov ebx, help_str
        int 0x80
        jmp .prompt

;---------------------------------------
; dump_page - Display LINES_PER_PAGE lines of hex dump
;---------------------------------------
dump_page:
        pushad
        mov esi, [view_addr]
        mov ecx, LINES_PER_PAGE

.dp_line:
        push ecx
        ; Print address
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09           ; blue
        int 0x80
        mov eax, esi
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, colon_space
        int 0x80

        ; Print hex bytes
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; white
        int 0x80
        xor edx, edx
.dp_byte:
        cmp edx, BYTES_PER_LINE
        jge .dp_ascii
        movzx eax, byte [esi + edx]
        call print_hex8
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        ; Extra space at midpoint
        cmp edx, 7
        jne .dp_byte_next
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.dp_byte_next:
        inc edx
        jmp .dp_byte

.dp_ascii:
        ; Print ASCII representation
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; green
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        xor edx, edx
.dp_asc_byte:
        cmp edx, BYTES_PER_LINE
        jge .dp_asc_done
        movzx eax, byte [esi + edx]
        cmp al, 32
        jl .dp_dot
        cmp al, 126
        jg .dp_dot
        mov ebx, eax
        jmp .dp_asc_put
.dp_dot:
        mov ebx, '.'
.dp_asc_put:
        mov eax, SYS_PUTCHAR
        int 0x80
        inc edx
        jmp .dp_asc_byte
.dp_asc_done:
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        add esi, BYTES_PER_LINE
        pop ecx
        dec ecx
        jnz .dp_line

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        popad
        ret

;---------------------------------------
; show_registers - Capture and display CPU registers
;---------------------------------------
show_registers:
        ; Capture registers at this point
        pushad
        pushfd
        mov [.sr_eflags], eax
        ; The pushad saved: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX
        ; We'll display them
        mov ebp, esp
        add ebp, 4              ; skip eflags push

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80

        ; EAX
        mov eax, SYS_PRINT
        mov ebx, reg_eax_str
        int 0x80
        mov eax, [ebp + 28]     ; EAX from pushad
        call print_hex32

        ; EBX
        mov eax, SYS_PRINT
        mov ebx, reg_ebx_str
        int 0x80
        mov eax, [ebp + 16]     ; EBX
        call print_hex32

        ; ECX
        mov eax, SYS_PRINT
        mov ebx, reg_ecx_str
        int 0x80
        mov eax, [ebp + 24]     ; ECX
        call print_hex32

        ; EDX
        mov eax, SYS_PRINT
        mov ebx, reg_edx_str
        int 0x80
        mov eax, [ebp + 20]     ; EDX
        call print_hex32

        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        ; ESI
        mov eax, SYS_PRINT
        mov ebx, reg_esi_str
        int 0x80
        mov eax, [ebp + 8]
        call print_hex32

        ; EDI
        mov eax, SYS_PRINT
        mov ebx, reg_edi_str
        int 0x80
        mov eax, [ebp + 4]
        call print_hex32

        ; EBP
        mov eax, SYS_PRINT
        mov ebx, reg_ebp_str
        int 0x80
        mov eax, [ebp + 12]
        call print_hex32

        ; ESP
        mov eax, SYS_PRINT
        mov ebx, reg_esp_str
        int 0x80
        mov eax, [ebp + 0]
        call print_hex32

        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        ; EFLAGS
        mov eax, SYS_PRINT
        mov ebx, reg_efl_str
        int 0x80
        mov eax, [.sr_eflags]
        call print_hex32

        ; PID
        mov eax, SYS_PRINT
        mov ebx, reg_pid_str
        int 0x80
        mov eax, SYS_GETPID
        int 0x80
        call print_hex32

        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        popfd
        popad
        ret

.sr_eflags: dd 0

;---------------------------------------
; do_search - Search memory for byte pattern
;---------------------------------------
do_search:
        pushad
        ; Parse pattern from "s XX XX XX"
        lea esi, [cmd_buf + 2]
        mov edi, search_pat
        xor ecx, ecx           ; pattern length
.ds_parse:
        call skip_spaces_esi
        cmp byte [esi], 0
        je .ds_search
        call parse_hex
        mov [edi + ecx], al
        inc ecx
        cmp ecx, 16
        jl .ds_parse

.ds_search:
        test ecx, ecx
        jz .ds_done
        mov [search_pat_len], ecx

        ; Search from view_addr, 64KB range
        mov esi, [view_addr]
        mov edx, 65536
.ds_loop:
        push ecx
        push esi
        mov edi, search_pat
        mov ecx, [search_pat_len]
        repe cmpsb
        pop esi
        pop ecx
        je .ds_found
        inc esi
        dec edx
        jnz .ds_loop

        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        jmp .ds_done

.ds_found:
        mov eax, SYS_PRINT
        mov ebx, msg_found_at
        int 0x80
        mov eax, esi
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        mov [view_addr], esi

.ds_done:
        popad
        ret

;---------------------------------------
; do_write - Write bytes to memory
;---------------------------------------
do_write:
        pushad
        ; Parse "w ADDR XX XX XX"
        lea esi, [cmd_buf + 2]
        call skip_spaces_esi
        call parse_hex
        mov edi, eax            ; destination address

.dw_loop:
        call skip_spaces_esi
        cmp byte [esi], 0
        je .dw_done
        call parse_hex
        mov [edi], al
        inc edi
        jmp .dw_loop

.dw_done:
        mov eax, SYS_PRINT
        mov ebx, msg_written
        int 0x80
        popad
        ret

;---------------------------------------
; Helper functions
;---------------------------------------

; print_hex32 - Print EAX as 8-digit hex
print_hex32:
        push eax
        push ecx
        push edx
        mov ecx, 8
        mov edx, eax
.ph32_loop:
        rol edx, 4
        mov al, dl
        and al, 0x0F
        cmp al, 10
        jl .ph32_digit
        add al, 'A' - 10
        jmp .ph32_put
.ph32_digit:
        add al, '0'
.ph32_put:
        push edx
        push ecx
        mov ebx, eax
        movzx ebx, bl
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ecx
        pop edx
        dec ecx
        jnz .ph32_loop
        pop edx
        pop ecx
        pop eax
        ret

; print_hex8 - Print AL as 2-digit hex
print_hex8:
        push eax
        push ebx
        push ecx
        mov cl, al
        shr al, 4
        and al, 0x0F
        cmp al, 10
        jl .ph8_d1
        add al, 'A' - 10
        jmp .ph8_p1
.ph8_d1:
        add al, '0'
.ph8_p1:
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        mov al, cl
        and al, 0x0F
        cmp al, 10
        jl .ph8_d2
        add al, 'A' - 10
        jmp .ph8_p2
.ph8_d2:
        add al, '0'
.ph8_p2:
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ecx
        pop ebx
        pop eax
        ret

; parse_hex - Parse hex number from ESI, advances ESI
; Returns: EAX = value
parse_hex:
        push ebx
        push ecx
        xor eax, eax
        ; Skip "0x" prefix
        cmp byte [esi], '0'
        jne .phx_loop
        cmp byte [esi + 1], 'x'
        jne .phx_loop
        add esi, 2
.phx_loop:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .phx_done
        cmp cl, '9'
        jbe .phx_digit
        or cl, 0x20
        cmp cl, 'a'
        jb .phx_done
        cmp cl, 'f'
        ja .phx_done
        sub cl, 'a' - 10
        jmp .phx_add
.phx_digit:
        sub cl, '0'
.phx_add:
        shl eax, 4
        add eax, ecx
        inc esi
        jmp .phx_loop
.phx_done:
        pop ecx
        pop ebx
        ret

; skip_spaces_esi - Advance ESI past spaces
skip_spaces_esi:
        cmp byte [esi], ' '
        jne .sse_done
        inc esi
        jmp skip_spaces_esi
.sse_done:
        ret

;=======================================
; Data
;=======================================

banner_str:     db "Mellivora Debug v1.0 - Memory Inspector", 0x0D, 0x0A
                db "Type 'h' for help", 0x0D, 0x0A, 0
prompt_str:     db "dbg> ", 0
newline:        db 0x0D, 0x0A, 0
colon_space:    db ": ", 0

help_str:       db "Commands:", 0x0D, 0x0A
                db "  d [addr]        Dump memory (hex + ASCII)", 0x0D, 0x0A
                db "  n               Next page", 0x0D, 0x0A
                db "  p               Previous page", 0x0D, 0x0A
                db "  g <addr>        Go to address", 0x0D, 0x0A
                db "  s <XX XX ..>    Search for hex bytes", 0x0D, 0x0A
                db "  w <addr> <XX>   Write bytes to memory", 0x0D, 0x0A
                db "  r               Show CPU registers", 0x0D, 0x0A
                db "  q               Quit", 0x0D, 0x0A
                db "Addresses are hexadecimal (0x prefix optional)", 0x0D, 0x0A, 0

msg_unknown:    db "Unknown command. Type 'h' for help.", 0x0D, 0x0A, 0
msg_not_found:  db "Pattern not found in 64KB range", 0x0D, 0x0A, 0
msg_found_at:   db "Found at: ", 0
msg_written:    db "Bytes written", 0x0D, 0x0A, 0

reg_eax_str:    db "  EAX=", 0
reg_ebx_str:    db "  EBX=", 0
reg_ecx_str:    db "  ECX=", 0
reg_edx_str:    db "  EDX=", 0
reg_esi_str:    db 0x0D, 0x0A, "  ESI=", 0
reg_edi_str:    db "  EDI=", 0
reg_ebp_str:    db "  EBP=", 0
reg_esp_str:    db "  ESP=", 0
reg_efl_str:    db 0x0D, 0x0A, "  EFL=", 0
reg_pid_str:    db "  PID=", 0

; BSS
view_addr:      dd 0
search_pat_len: dd 0
arg_buf:        times 64 db 0
cmd_buf:        times 64 db 0
search_pat:     times 16 db 0

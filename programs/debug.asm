; debug.asm - System Debug & Memory Inspector v2.0
; Usage: debug [address]
; Provides interactive memory hex dump, register display, search,
; breakpoints (INT3 patching), disassemble, fill, compare, stack trace.
; Commands: d=dump, g=goto, s=search, r=registers, n=next, p=prev,
;   w=write, b=breakpoint, bl=list bp, bc=clear bp, u=unassemble,
;   f=fill, c=compare, t=stack trace, e=eval, h=help, q=quit

%include "syscalls.inc"

BYTES_PER_LINE  equ 16
LINES_PER_PAGE  equ 20

MAX_BP          equ 8

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
        cmp byte [cmd_buf], 'b'
        je .cmd_bp_dispatch
        cmp byte [cmd_buf], 'u'
        je .cmd_unasm
        cmp byte [cmd_buf], 'f'
        je .cmd_fill
        cmp byte [cmd_buf], 'c'
        je .cmd_compare
        cmp byte [cmd_buf], 't'
        je .cmd_stack
        cmp byte [cmd_buf], 'e'
        je .cmd_eval
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
; b ADDR - Set breakpoint (patch INT3 = 0xCC)
;---------------------------------------
.cmd_bp_dispatch:
        cmp byte [cmd_buf + 1], 'l'
        je .cmd_bp_list
        cmp byte [cmd_buf + 1], 'c'
        je .cmd_bp_clear
        cmp byte [cmd_buf + 1], ' '
        jne .prompt
        ; b ADDR — set breakpoint
        lea esi, [cmd_buf + 2]
        call parse_hex
        mov edx, eax            ; EDX = address
        ; Find free breakpoint slot
        mov ecx, 0
.bp_find:
        cmp ecx, MAX_BP
        jge .bp_full
        cmp byte [bp_active + ecx], 0
        je .bp_set
        inc ecx
        jmp .bp_find
.bp_set:
        mov byte [bp_active + ecx], 1
        mov [bp_addr + rcx * 8], rdx
        movzx eax, byte [edx]
        mov [bp_orig + ecx], al
        mov byte [edx], 0xCC   ; INT3
        mov eax, SYS_PRINT
        mov ebx, msg_bp_set
        int 0x80
        mov eax, edx
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        jmp .prompt
.bp_full:
        mov eax, SYS_PRINT
        mov ebx, msg_bp_full
        int 0x80
        jmp .prompt

;---------------------------------------
; bl - List breakpoints
;---------------------------------------
.cmd_bp_list:
        mov ecx, 0
        xor edx, edx           ; count displayed
.bpl_loop:
        cmp ecx, MAX_BP
        jge .bpl_done
        cmp byte [bp_active + ecx], 0
        je .bpl_next
        push rcx
        push rdx
        ; Print "  #N  ADDR  (orig: XX)"
        mov eax, SYS_PRINT
        mov ebx, msg_bp_num
        int 0x80
        mov eax, ecx
        add eax, '0'
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_bp_at
        int 0x80
        pop rdx
        pop rcx
        push rcx
        push rdx
        mov rax, [bp_addr + rcx * 8]
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, msg_bp_orig
        int 0x80
        pop rdx
        pop rcx
        push rcx
        push rdx
        movzx eax, byte [bp_orig + ecx]
        call print_hex8
        mov eax, SYS_PRINT
        mov ebx, msg_bp_end
        int 0x80
        pop rdx
        pop rcx
        inc edx
.bpl_next:
        inc ecx
        jmp .bpl_loop
.bpl_done:
        test edx, edx
        jnz .prompt
        mov eax, SYS_PRINT
        mov ebx, msg_no_bp
        int 0x80
        jmp .prompt

;---------------------------------------
; bc ADDR - Clear breakpoint
;---------------------------------------
.cmd_bp_clear:
        cmp byte [cmd_buf + 2], ' '
        jne .prompt
        lea esi, [cmd_buf + 3]
        call parse_hex
        mov edx, eax
        ; Find breakpoint at this address
        mov ecx, 0
.bpc_loop:
        cmp ecx, MAX_BP
        jge .bpc_notfound
        cmp byte [bp_active + ecx], 0
        je .bpc_next
        cmp [bp_addr + rcx * 8], rdx
        je .bpc_found
.bpc_next:
        inc ecx
        jmp .bpc_loop
.bpc_found:
        movzx eax, byte [bp_orig + ecx]
        mov [edx], al           ; Restore original byte
        mov byte [bp_active + ecx], 0
        mov eax, SYS_PRINT
        mov ebx, msg_bp_cleared
        int 0x80
        jmp .prompt
.bpc_notfound:
        mov eax, SYS_PRINT
        mov ebx, msg_bp_nf
        int 0x80
        jmp .prompt

;---------------------------------------
; u [ADDR] - Unassemble / hex opcode dump
; Shows 16 bytes at address as raw opcodes + ASCII
;---------------------------------------
.cmd_unasm:
        cmp byte [cmd_buf + 1], ' '
        jne .unasm_current
        lea esi, [cmd_buf + 2]
        call parse_hex
        mov [unasm_addr], eax
        jmp .unasm_go
.unasm_current:
        mov eax, [view_addr]
        mov [unasm_addr], eax
.unasm_go:
        call do_unassemble
        jmp .prompt

;---------------------------------------
; f ADDR COUNT BYTE - Fill memory region
;---------------------------------------
.cmd_fill:
        cmp byte [cmd_buf + 1], ' '
        jne .prompt
        lea esi, [cmd_buf + 2]
        call skip_spaces_esi
        call parse_hex
        mov edi, eax            ; dest address
        call skip_spaces_esi
        call parse_hex
        mov ecx, eax            ; count
        call skip_spaces_esi
        call parse_hex
        ; AL = fill byte
        test ecx, ecx
        jz .prompt
        rep stosb
        mov eax, SYS_PRINT
        mov ebx, msg_filled
        int 0x80
        jmp .prompt

;---------------------------------------
; c ADDR1 ADDR2 LEN - Compare memory regions
;---------------------------------------
.cmd_compare:
        cmp byte [cmd_buf + 1], ' '
        jne .prompt
        lea esi, [cmd_buf + 2]
        call skip_spaces_esi
        call parse_hex
        push rax                ; addr1
        call skip_spaces_esi
        call parse_hex
        push rax                ; addr2
        call skip_spaces_esi
        call parse_hex
        mov ecx, eax            ; length
        pop rdi                 ; addr2
        pop rsi                 ; addr1
        xor edx, edx           ; diff count
.cmp_loop:
        test ecx, ecx
        jz .cmp_done
        cmpsb
        je .cmp_next
        ; Mismatch at ESI-1 vs EDI-1
        push rcx
        push rdx
        push rsi
        push rdi
        mov eax, esi
        dec eax
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, msg_cmp_vs
        int 0x80
        movzx eax, byte [esi - 1]
        call print_hex8
        mov eax, SYS_PRINT
        mov ebx, msg_cmp_ne
        int 0x80
        movzx eax, byte [edi - 1]
        call print_hex8
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        inc edx
        cmp edx, 16            ; max 16 diffs shown
        jge .cmp_done
.cmp_next:
        dec ecx
        jmp .cmp_loop
.cmp_done:
        test edx, edx
        jnz .prompt
        mov eax, SYS_PRINT
        mov ebx, msg_cmp_eq
        int 0x80
        jmp .prompt

;---------------------------------------
; t - Stack trace (walk EBP chain)
;---------------------------------------
.cmd_stack:
        PUSHALL
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_stack_hdr
        int 0x80
        mov ebp, [rsp + 64]    ; EBP from PUSHALL
        xor ecx, ecx
.st_loop:
        cmp ecx, 16            ; max 16 frames
        jge .st_end
        test ebp, ebp
        jz .st_end
        cmp ebp, 0x00100000    ; sanity: below kernel
        jb .st_end
        cmp ebp, 0x08000000    ; sanity: above 128MB
        ja .st_end
        ; Return address is at [EBP+4]
        push rcx
        mov eax, SYS_PRINT
        mov ebx, msg_st_frame
        int 0x80
        pop rcx
        push rcx
        mov eax, ecx
        add eax, '0'
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_st_ebp
        int 0x80
        mov eax, ebp
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, msg_st_ret
        int 0x80
        mov eax, [ebp + 4]
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        pop rcx
        mov ebp, [ebp]         ; Walk to parent frame
        inc ecx
        jmp .st_loop
.st_end:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        POPALL
        jmp .prompt

;---------------------------------------
; e EXPR - Evaluate hex expression (supports +, -, &, |)
;---------------------------------------
.cmd_eval:
        cmp byte [cmd_buf + 1], ' '
        jne .prompt
        lea esi, [cmd_buf + 2]
        call skip_spaces_esi
        call parse_hex
        mov edx, eax            ; first operand
.eval_op:
        call skip_spaces_esi
        movzx ecx, byte [esi]
        test cl, cl
        jz .eval_show
        inc esi
        call skip_spaces_esi
        push rdx
        push rcx
        call parse_hex
        pop rcx
        pop rdx
        cmp cl, '+'
        je .eval_add
        cmp cl, '-'
        je .eval_sub
        cmp cl, '&'
        je .eval_and
        cmp cl, '|'
        je .eval_or
        jmp .eval_show
.eval_add:
        add edx, eax
        jmp .eval_op
.eval_sub:
        sub edx, eax
        jmp .eval_op
.eval_and:
        and edx, eax
        jmp .eval_op
.eval_or:
        or edx, eax
        jmp .eval_op
.eval_show:
        mov eax, SYS_PRINT
        mov ebx, msg_eval_eq
        int 0x80
        mov eax, edx
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        jmp .prompt

;---------------------------------------
; dump_page - Display LINES_PER_PAGE lines of hex dump
;---------------------------------------
dump_page:
        PUSHALL
        mov esi, [view_addr]
        mov ecx, LINES_PER_PAGE

.dp_line:
        push rcx
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
        pop rcx
        dec ecx
        jnz .dp_line

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        POPALL
        ret

;---------------------------------------
; show_registers - Capture and display CPU registers
;---------------------------------------
show_registers:
        ; Capture registers at this point
        PUSHALL
        pushfq
        mov [.sr_eflags], eax
        ; The PUSHALL saved: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX
        ; We'll display them
        mov rbp, rsp
        add rbp, 8              ; skip pushfq push

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80

        ; EAX
        mov eax, SYS_PRINT
        mov ebx, reg_eax_str
        int 0x80
        mov eax, [ebp + 28]     ; EAX from PUSHALL
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

        popfq
        POPALL
        ret

.sr_eflags: dd 0

;---------------------------------------
; do_search - Search memory for byte pattern
;---------------------------------------
do_search:
        PUSHALL
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
        push rcx
        push rsi
        mov edi, search_pat
        mov ecx, [search_pat_len]
        repe cmpsb
        pop rsi
        pop rcx
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
        POPALL
        ret

;---------------------------------------
; do_write - Write bytes to memory
;---------------------------------------
do_write:
        PUSHALL
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
        POPALL
        ret

;---------------------------------------
; do_unassemble - Show 16 lines of raw opcode bytes
; Each line: ADDR: XX XX XX XX XX XX  ; decoded approx instruction
;---------------------------------------
do_unassemble:
        PUSHALL
        mov esi, [unasm_addr]
        mov ecx, 16            ; lines
.ua_line:
        push rcx
        ; Print address
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09
        int 0x80
        mov eax, esi
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, colon_space
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        ; Decode common instruction lengths
        movzx eax, byte [esi]
        ; Common x86 opcode recognition for display
        ; NOP = 0x90
        cmp al, 0x90
        je .ua_1byte
        ; RET = 0xC3
        cmp al, 0xC3
        je .ua_1byte
        ; INT3 = 0xCC
        cmp al, 0xCC
        je .ua_1byte
        ; PUSHAD = 0x60, POPAD = 0x61
        cmp al, 0x60
        je .ua_1byte
        cmp al, 0x61
        je .ua_1byte
        ; PUSH reg (0x50-0x57), POP reg (0x58-0x5F)
        cmp al, 0x50
        jb .ua_check2
        cmp al, 0x5F
        jbe .ua_1byte
.ua_check2:
        ; INT imm8 = 0xCD xx
        cmp al, 0xCD
        je .ua_2byte
        ; JMP/JCC short (0x70-0x7F, 0xEB)
        cmp al, 0x70
        jb .ua_check_eb
        cmp al, 0x7F
        jbe .ua_2byte
.ua_check_eb:
        cmp al, 0xEB
        je .ua_2byte
        ; CALL rel32 = 0xE8, JMP rel32 = 0xE9
        cmp al, 0xE8
        je .ua_5byte
        cmp al, 0xE9
        je .ua_5byte
        ; MOV reg,imm32 (0xB8-0xBF)
        cmp al, 0xB8
        jb .ua_default
        cmp al, 0xBF
        jbe .ua_5byte

.ua_default:
        ; Default: show 4 bytes
        mov edx, 4
        jmp .ua_show
.ua_1byte:
        mov edx, 1
        jmp .ua_show
.ua_2byte:
        mov edx, 2
        jmp .ua_show
.ua_5byte:
        mov edx, 5

.ua_show:
        ; Print EDX bytes in hex
        push rdx
        push rsi
        xor ebx, ebx
.ua_byte:
        cmp ebx, edx
        jge .ua_pad
        movzx eax, byte [esi + ebx]
        call print_hex8
        mov eax, SYS_PUTCHAR
        push rbx
        mov ebx, ' '
        int 0x80
        pop rbx
        inc ebx
        jmp .ua_byte
.ua_pad:
        ; Pad to 18 chars (6 bytes max × 3)
        cmp ebx, 6
        jge .ua_mnemonic
        mov eax, SYS_PUTCHAR
        push rbx
        mov ebx, ' '
        int 0x80
        int 0x80
        int 0x80
        pop rbx
        inc ebx
        jmp .ua_pad

.ua_mnemonic:
        ; Print semicolon + approximate mnemonic
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_semi
        int 0x80
        pop rsi
        movzx eax, byte [esi]
        ; Quick decode
        cmp al, 0x90
        je .ua_m_nop
        cmp al, 0xC3
        je .ua_m_ret
        cmp al, 0xCC
        je .ua_m_int3
        cmp al, 0xCD
        je .ua_m_int
        cmp al, 0x60
        je .ua_m_pushad
        cmp al, 0x61
        je .ua_m_popad
        cmp al, 0xE8
        je .ua_m_call
        cmp al, 0xE9
        je .ua_m_jmp
        cmp al, 0xEB
        je .ua_m_jmps
        cmp al, 0x50
        jb .ua_m_unknown
        cmp al, 0x57
        jbe .ua_m_push
        cmp al, 0x58
        jb .ua_m_unknown
        cmp al, 0x5F
        jbe .ua_m_pop
        cmp al, 0xB8
        jb .ua_m_unknown
        cmp al, 0xBF
        jbe .ua_m_mov
        jmp .ua_m_unknown

.ua_m_nop:
        mov ebx, mn_nop
        jmp .ua_m_print
.ua_m_ret:
        mov ebx, mn_ret
        jmp .ua_m_print
.ua_m_int3:
        mov ebx, mn_int3
        jmp .ua_m_print
.ua_m_int:
        mov ebx, mn_int
        jmp .ua_m_print
.ua_m_pushad:
        mov ebx, mn_pushad
        jmp .ua_m_print
.ua_m_popad:
        mov ebx, mn_popad
        jmp .ua_m_print
.ua_m_call:
        mov ebx, mn_call
        jmp .ua_m_print
.ua_m_jmp:
        mov ebx, mn_jmp
        jmp .ua_m_print
.ua_m_jmps:
        mov ebx, mn_jmps
        jmp .ua_m_print
.ua_m_push:
        mov ebx, mn_push
        jmp .ua_m_print
.ua_m_pop:
        mov ebx, mn_pop
        jmp .ua_m_print
.ua_m_mov:
        mov ebx, mn_mov
        jmp .ua_m_print
.ua_m_unknown:
        mov ebx, mn_db
.ua_m_print:
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        pop rdx
        add esi, edx
        pop rcx
        dec ecx
        jnz .ua_line

        mov [unasm_addr], esi
        POPALL
        ret

;---------------------------------------
; Helper functions
;---------------------------------------

; print_hex32 - Print EAX as 8-digit hex
print_hex32:
        push rax
        push rcx
        push rdx
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
        push rdx
        push rcx
        mov ebx, eax
        movzx ebx, bl
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        pop rdx
        dec ecx
        jnz .ph32_loop
        pop rdx
        pop rcx
        pop rax
        ret

; print_hex8 - Print AL as 2-digit hex
print_hex8:
        push rax
        push rbx
        push rcx
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
        pop rcx
        pop rbx
        pop rax
        ret

; parse_hex - Parse hex number from ESI, advances ESI
; Returns: EAX = value
parse_hex:
        push rbx
        push rcx
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
        pop rcx
        pop rbx
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

banner_str:     db "Mellivora Debug v2.0 - Memory Inspector", 0x0D, 0x0A
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
                db "  u [addr]        Unassemble (opcode dump)", 0x0D, 0x0A
                db "  b <addr>        Set breakpoint (INT3)", 0x0D, 0x0A
                db "  bc <addr>       Clear breakpoint", 0x0D, 0x0A
                db "  bl              List breakpoints", 0x0D, 0x0A
                db "  f <addr> <n> <b> Fill n bytes with b", 0x0D, 0x0A
                db "  c <a1> <a2> <n> Compare n bytes", 0x0D, 0x0A
                db "  t               Stack trace (EBP chain)", 0x0D, 0x0A
                db "  e <expr>        Evaluate hex (+,-,&,|)", 0x0D, 0x0A
                db "  q               Quit", 0x0D, 0x0A
                db "Addresses are hexadecimal (0x prefix optional)", 0x0D, 0x0A, 0

msg_unknown:    db "Unknown command. Type 'h' for help.", 0x0D, 0x0A, 0
msg_not_found:  db "Pattern not found in 64KB range", 0x0D, 0x0A, 0
msg_found_at:   db "Found at: ", 0
msg_written:    db "Bytes written", 0x0D, 0x0A, 0

; Breakpoint messages
msg_bp_set:     db "Breakpoint set at ", 0
msg_bp_full:    db "All breakpoint slots full (max 8)", 0x0D, 0x0A, 0
msg_bp_cleared: db "Breakpoint cleared", 0x0D, 0x0A, 0
msg_bp_nf:      db "No breakpoint at that address", 0x0D, 0x0A, 0
msg_bp_num:     db "  #", 0
msg_bp_at:      db "  ", 0
msg_bp_orig:    db "  (was ", 0
msg_bp_end:     db ")", 0x0D, 0x0A, 0
msg_no_bp:      db "No breakpoints set", 0x0D, 0x0A, 0

; Compare/fill messages
msg_filled:     db "Memory filled", 0x0D, 0x0A, 0
msg_cmp_vs:     db ": ", 0
msg_cmp_ne:     db " != ", 0
msg_cmp_eq:     db "Regions are identical", 0x0D, 0x0A, 0

; Stack trace messages
msg_stack_hdr:  db "Stack Trace (EBP chain):", 0x0D, 0x0A, 0
msg_st_frame:   db "  #", 0
msg_st_ebp:     db "  EBP=", 0
msg_st_ret:     db "  RET=", 0

; Eval messages
msg_eval_eq:    db "= ", 0

; Unassemble
msg_semi:       db "; ", 0
mn_nop:         db "NOP", 0
mn_ret:         db "RET", 0
mn_int3:        db "INT3", 0
mn_int:         db "INT", 0
mn_pushad:      db "PUSHAD", 0
mn_popad:       db "POPAD", 0
mn_call:        db "CALL", 0
mn_jmp:         db "JMP", 0
mn_jmps:        db "JMP SHORT", 0
mn_push:        db "PUSH", 0
mn_pop:         db "POP", 0
mn_mov:         db "MOV reg,imm32", 0
mn_db:          db "DB", 0

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
unasm_addr:     dd 0
arg_buf:        times 64 db 0
cmd_buf:        times 64 db 0
search_pat:     times 16 db 0
; Breakpoint table
bp_active:      times MAX_BP db 0
bp_addr:        times MAX_BP dq 0
bp_orig:        times MAX_BP db 0

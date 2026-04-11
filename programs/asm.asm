; asm.asm - Interactive x86 Assembler / Disassembler for Mellivora OS
; Assembles basic x86-32 instructions and shows machine code bytes.
; Great educational tool for learning CPU instruction encoding.
;
; Usage: asm              (interactive mode)
;        asm <instruction> (assemble one instruction)
;
; Supports: NOP, HLT, RET, INT n, MOV reg,imm, MOV reg,reg,
;           ADD/SUB/AND/OR/XOR/CMP reg,reg, PUSH/POP reg,
;           INC/DEC reg, JMP/CALL/JZ/JNZ rel, XCHG reg,reg,
;           SHL/SHR reg,imm
;
; Press 'quit' or ESC to exit
%include "syscalls.inc"
%include "lib/io.inc"
%include "lib/string.inc"
%include "lib/math.inc"

MAX_BYTES       equ 16

start:
        ; Check for command-line argument
        mov eax, SYS_GETARGS
        mov ebx, input_buf
        mov ecx, 128
        int 0x80
        test eax, eax
        jz .interactive

        ; Single-shot mode
        call assemble_line
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.interactive:
        ; Print banner
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, banner_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

.loop:
        ; Prompt
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

        call io_read_line
        mov esi, eax            ; ESI = input string

        ; Copy to input_buf
        mov edi, input_buf
        mov ecx, 128
.copy:
        lodsb
        stosb
        test al, al
        jz .copied
        dec ecx
        jnz .copy
.copied:

        ; Check for quit
        mov eax, input_buf
        mov ebx, quit_str
        call str_icmp
        test eax, eax
        jz .exit

        mov eax, input_buf
        mov ebx, exit_str
        call str_icmp
        test eax, eax
        jz .exit

        ; Check for help
        mov eax, input_buf
        mov ebx, help_cmd
        call str_icmp
        test eax, eax
        jz .show_help

        ; Check empty line
        cmp byte [input_buf], 0
        je .loop

        call assemble_line
        jmp .loop

.show_help:
        mov eax, SYS_PRINT
        mov ebx, help_text
        int 0x80
        jmp .loop

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; assemble_line - parse input_buf and emit bytes
;=======================================
assemble_line:
        pushad

        ; Reset output
        mov dword [out_len], 0

        ; Tokenize: get mnemonic
        mov esi, input_buf
        call skip_spaces
        mov edi, mnemonic_buf
        call copy_token         ; copies word to EDI, ESI advances

        ; Uppercase the mnemonic
        mov esi, mnemonic_buf
.upper:
        lodsb
        test al, al
        jz .upper_done
        cmp al, 'a'
        jb .upper_next
        cmp al, 'z'
        ja .upper_next
        sub al, 32
        mov [esi-1], al
.upper_next:
        jmp .upper
.upper_done:

        ; Parse operands
        mov esi, input_buf
        call skip_spaces
        call skip_token         ; skip mnemonic
        call skip_spaces
        ; ESI now points to operands

        ; Save operand start
        mov [operand_ptr], esi

        ; Try to match mnemonic
        ; --- NOP ---
        mov eax, mnemonic_buf
        mov ebx, mn_nop
        call str_icmp
        test eax, eax
        jnz .try_hlt
        mov byte [out_bytes], 0x90
        mov dword [out_len], 1
        jmp .emit

.try_hlt:
        mov eax, mnemonic_buf
        mov ebx, mn_hlt
        call str_icmp
        test eax, eax
        jnz .try_ret
        mov byte [out_bytes], 0xF4
        mov dword [out_len], 1
        jmp .emit

.try_ret:
        mov eax, mnemonic_buf
        mov ebx, mn_ret
        call str_icmp
        test eax, eax
        jnz .try_clc
        mov byte [out_bytes], 0xC3
        mov dword [out_len], 1
        jmp .emit

.try_clc:
        mov eax, mnemonic_buf
        mov ebx, mn_clc
        call str_icmp
        test eax, eax
        jnz .try_stc
        mov byte [out_bytes], 0xF8
        mov dword [out_len], 1
        jmp .emit

.try_stc:
        mov eax, mnemonic_buf
        mov ebx, mn_stc
        call str_icmp
        test eax, eax
        jnz .try_cli
        mov byte [out_bytes], 0xF9
        mov dword [out_len], 1
        jmp .emit

.try_cli:
        mov eax, mnemonic_buf
        mov ebx, mn_cli
        call str_icmp
        test eax, eax
        jnz .try_sti
        mov byte [out_bytes], 0xFA
        mov dword [out_len], 1
        jmp .emit

.try_sti:
        mov eax, mnemonic_buf
        mov ebx, mn_sti
        call str_icmp
        test eax, eax
        jnz .try_int
        mov byte [out_bytes], 0xFB
        mov dword [out_len], 1
        jmp .emit

        ; --- INT imm8 ---
.try_int:
        mov eax, mnemonic_buf
        mov ebx, mn_int
        call str_icmp
        test eax, eax
        jnz .try_push
        mov esi, [operand_ptr]
        call parse_number       ; EAX=number
        jc .error
        mov byte [out_bytes], 0xCD
        mov byte [out_bytes+1], al
        mov dword [out_len], 2
        jmp .emit

        ; --- PUSH reg ---
.try_push:
        mov eax, mnemonic_buf
        mov ebx, mn_push
        call str_icmp
        test eax, eax
        jnz .try_pop
        mov esi, [operand_ptr]
        call parse_reg32
        jc .error
        add al, 0x50            ; PUSH r32 = 50+rd
        mov byte [out_bytes], al
        mov dword [out_len], 1
        jmp .emit

        ; --- POP reg ---
.try_pop:
        mov eax, mnemonic_buf
        mov ebx, mn_pop
        call str_icmp
        test eax, eax
        jnz .try_inc
        mov esi, [operand_ptr]
        call parse_reg32
        jc .error
        add al, 0x58            ; POP r32 = 58+rd
        mov byte [out_bytes], al
        mov dword [out_len], 1
        jmp .emit

        ; --- INC reg ---
.try_inc:
        mov eax, mnemonic_buf
        mov ebx, mn_inc
        call str_icmp
        test eax, eax
        jnz .try_dec
        mov esi, [operand_ptr]
        call parse_reg32
        jc .error
        add al, 0x40            ; INC r32 = 40+rd
        mov byte [out_bytes], al
        mov dword [out_len], 1
        jmp .emit

        ; --- DEC reg ---
.try_dec:
        mov eax, mnemonic_buf
        mov ebx, mn_dec
        call str_icmp
        test eax, eax
        jnz .try_mov
        mov esi, [operand_ptr]
        call parse_reg32
        jc .error
        add al, 0x48            ; DEC r32 = 48+rd
        mov byte [out_bytes], al
        mov dword [out_len], 1
        jmp .emit

        ; --- MOV reg, imm32 / MOV reg, reg ---
.try_mov:
        mov eax, mnemonic_buf
        mov ebx, mn_mov
        call str_icmp
        test eax, eax
        jnz .try_add
        mov esi, [operand_ptr]
        call parse_reg32        ; destination
        jc .error
        mov [dest_reg], al
        call skip_comma
        ; Try register second operand
        push esi
        call parse_reg32
        jc .mov_imm
        ; MOV reg, reg: opcode 89 /r (MOV r/m32, r32)
        add esp, 4              ; discard saved esi
        mov cl, al              ; source reg
        mov byte [out_bytes], 0x89
        ; ModR/M: mod=11, reg=src, r/m=dest
        shl cl, 3
        or cl, [dest_reg]
        or cl, 0xC0
        mov byte [out_bytes+1], cl
        mov dword [out_len], 2
        jmp .emit

.mov_imm:
        pop esi                 ; restore before comma parsed position
        call skip_comma
        call parse_number
        jc .error
        ; MOV r32, imm32 = B8+rd
        mov ecx, eax            ; imm32
        mov al, [dest_reg]
        add al, 0xB8
        mov byte [out_bytes], al
        mov [out_bytes+1], ecx
        mov dword [out_len], 5
        jmp .emit

        ; --- ALU reg, reg: ADD SUB AND OR XOR CMP ---
.try_add:
        ; Table of ALU ops
        mov ebp, 0              ; index
.alu_loop:
        cmp ebp, 6
        jge .try_xchg

        mov eax, mnemonic_buf
        mov ebx, [alu_names + ebp*4]
        call str_icmp
        test eax, eax
        jnz .alu_next

        ; Matched ALU op
        mov esi, [operand_ptr]
        call parse_reg32
        jc .error
        mov [dest_reg], al
        call skip_comma

        ; Try reg
        push esi
        call parse_reg32
        jc .alu_imm
        add esp, 4
        ; ALU r/m32, r32: opcode = alu_opcodes[ebp] + 01
        mov cl, [alu_opcodes + ebp]
        add cl, 1               ; r/m32, r32 form
        mov byte [out_bytes], cl
        ; ModR/M
        mov cl, al              ; src
        shl cl, 3
        or cl, [dest_reg]
        or cl, 0xC0
        mov byte [out_bytes+1], cl
        mov dword [out_len], 2
        jmp .emit

.alu_imm:
        pop esi
        call skip_comma
        call parse_number
        jc .error
        ; ALU r/m32, imm32: 81 /digit r/m imm32
        mov byte [out_bytes], 0x81
        mov cl, byte [alu_digits + ebp]
        shl cl, 3
        or cl, [dest_reg]
        or cl, 0xC0
        mov byte [out_bytes+1], cl
        mov [out_bytes+2], eax
        mov dword [out_len], 6
        jmp .emit

.alu_next:
        inc ebp
        jmp .alu_loop

        ; --- XCHG reg, reg ---
.try_xchg:
        mov eax, mnemonic_buf
        mov ebx, mn_xchg
        call str_icmp
        test eax, eax
        jnz .try_shl
        mov esi, [operand_ptr]
        call parse_reg32
        jc .error
        mov [dest_reg], al
        call skip_comma
        call parse_reg32
        jc .error
        ; If one is EAX, use short form (90+rd)
        cmp byte [dest_reg], 0
        jne .xchg_full
        add al, 0x90
        mov byte [out_bytes], al
        mov dword [out_len], 1
        jmp .emit
.xchg_full:
        test al, al
        jnz .xchg_long
        mov cl, [dest_reg]
        add cl, 0x90
        mov byte [out_bytes], cl
        mov dword [out_len], 1
        jmp .emit
.xchg_long:
        mov byte [out_bytes], 0x87
        mov cl, al
        shl cl, 3
        or cl, [dest_reg]
        or cl, 0xC0
        mov byte [out_bytes+1], cl
        mov dword [out_len], 2
        jmp .emit

        ; --- SHL/SHR reg, imm8 ---
.try_shl:
        mov eax, mnemonic_buf
        mov ebx, mn_shl
        call str_icmp
        test eax, eax
        jnz .try_shr
        mov byte [shift_dir], 4     ; SHL /4
        jmp .do_shift
.try_shr:
        mov eax, mnemonic_buf
        mov ebx, mn_shr
        call str_icmp
        test eax, eax
        jnz .try_jmp
        mov byte [shift_dir], 5     ; SHR /5
.do_shift:
        mov esi, [operand_ptr]
        call parse_reg32
        jc .error
        mov [dest_reg], al
        call skip_comma
        call parse_number
        jc .error
        cmp eax, 1
        jne .shift_imm8
        ; SHL/SHR r32, 1: D1 /digit
        mov byte [out_bytes], 0xD1
        mov cl, [shift_dir]
        shl cl, 3
        or cl, [dest_reg]
        or cl, 0xC0
        mov byte [out_bytes+1], cl
        mov dword [out_len], 2
        jmp .emit
.shift_imm8:
        mov byte [out_bytes], 0xC1
        mov cl, [shift_dir]
        shl cl, 3
        or cl, [dest_reg]
        or cl, 0xC0
        mov byte [out_bytes+1], cl
        mov byte [out_bytes+2], al
        mov dword [out_len], 3
        jmp .emit

        ; --- JMP/CALL/Jcc rel ---
.try_jmp:
        mov eax, mnemonic_buf
        mov ebx, mn_jmp
        call str_icmp
        test eax, eax
        jnz .try_call
        mov esi, [operand_ptr]
        call parse_number
        jc .error
        mov byte [out_bytes], 0xEB    ; JMP rel8
        mov byte [out_bytes+1], al
        mov dword [out_len], 2
        jmp .emit

.try_call:
        mov eax, mnemonic_buf
        mov ebx, mn_call
        call str_icmp
        test eax, eax
        jnz .try_jz
        mov esi, [operand_ptr]
        call parse_number
        jc .error
        mov byte [out_bytes], 0xE8    ; CALL rel32
        mov [out_bytes+1], eax
        mov dword [out_len], 5
        jmp .emit

.try_jz:
        mov eax, mnemonic_buf
        mov ebx, mn_jz
        call str_icmp
        test eax, eax
        jnz .try_jnz
        mov esi, [operand_ptr]
        call parse_number
        jc .error
        mov byte [out_bytes], 0x74
        mov byte [out_bytes+1], al
        mov dword [out_len], 2
        jmp .emit

.try_jnz:
        mov eax, mnemonic_buf
        mov ebx, mn_jnz
        call str_icmp
        test eax, eax
        jnz .try_syscall
        mov esi, [operand_ptr]
        call parse_number
        jc .error
        mov byte [out_bytes], 0x75
        mov byte [out_bytes+1], al
        mov dword [out_len], 2
        jmp .emit

        ; --- SYSCALL (INT 0x80 shorthand) ---
.try_syscall:
        mov eax, mnemonic_buf
        mov ebx, mn_syscall
        call str_icmp
        test eax, eax
        jnz .unknown
        mov byte [out_bytes], 0xCD
        mov byte [out_bytes+1], 0x80
        mov dword [out_len], 2
        jmp .emit

.unknown:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_unknown
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, mnemonic_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline_str
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        jmp .done

.error:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_parse
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        jmp .done

.emit:
        ; Print machine code bytes
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A            ; green for hex
        int 0x80

        ; Print " => "
        mov eax, SYS_PRINT
        mov ebx, arrow_str
        int 0x80

        mov ecx, 0
.print_bytes:
        cmp ecx, [out_len]
        jge .print_done
        movzx eax, byte [out_bytes + ecx]
        call print_hex_byte
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .print_bytes
.print_done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08            ; gray annotation
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, size_prefix
        int 0x80
        mov eax, [out_len]
        call math_int_to_str
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, size_suffix
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

.done:
        popad
        ret

;---------------------------------------
; parse_reg32 - parse 32-bit register name from [ESI]
; Returns: AL=register number (0-7), ESI advanced
; CF set on failure
;---------------------------------------
parse_reg32:
        push ebx
        push ecx
        push edx

        call skip_spaces

        ; Copy register name to temp buffer (up to 3 chars)
        mov edi, reg_buf
        mov ecx, 3
.pr_copy:
        mov al, [esi]
        cmp al, ','
        je .pr_copy_done
        cmp al, ' '
        je .pr_copy_done
        cmp al, 0
        je .pr_copy_done
        ; Uppercase
        cmp al, 'a'
        jb .pr_no_up
        cmp al, 'z'
        ja .pr_no_up
        sub al, 32
.pr_no_up:
        stosb
        inc esi
        dec ecx
        jnz .pr_copy
.pr_copy_done:
        mov byte [edi], 0

        ; Match against register names
        mov ecx, 0
.pr_match:
        cmp ecx, 8
        jge .pr_fail

        push ecx
        mov eax, reg_buf
        mov ebx, [reg_names + ecx*4]
        call str_icmp
        pop ecx
        test eax, eax
        jz .pr_found
        inc ecx
        jmp .pr_match

.pr_found:
        mov eax, ecx
        clc
        pop edx
        pop ecx
        pop ebx
        ret

.pr_fail:
        stc
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; parse_number - parse decimal or hex number from [ESI]
; Hex if starts with 0x. Returns EAX. CF set on failure.
;---------------------------------------
parse_number:
        push ebx
        push ecx
        push edx

        call skip_spaces

        ; Check for 0x prefix
        cmp byte [esi], '0'
        jne .pn_decimal
        cmp byte [esi+1], 'x'
        je .pn_hex
        cmp byte [esi+1], 'X'
        je .pn_hex

.pn_decimal:
        ; Check for negative
        xor ecx, ecx           ; sign flag
        cmp byte [esi], '-'
        jne .pn_dec_parse
        mov ecx, 1
        inc esi
.pn_dec_parse:
        xor eax, eax
        xor edx, edx
.pn_dec_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pn_dec_done
        cmp dl, '9'
        ja .pn_dec_done
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .pn_dec_loop
.pn_dec_done:
        test ecx, ecx
        jz .pn_ok
        neg eax
        jmp .pn_ok

.pn_hex:
        add esi, 2              ; skip 0x
        xor eax, eax
.pn_hex_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pn_ok
        cmp dl, '9'
        jbe .pn_hex_digit
        cmp dl, 'A'
        jb .pn_hex_try_lower
        cmp dl, 'F'
        jbe .pn_hex_upper
.pn_hex_try_lower:
        cmp dl, 'a'
        jb .pn_ok
        cmp dl, 'f'
        ja .pn_ok
        sub dl, 'a' - 10
        jmp .pn_hex_add
.pn_hex_upper:
        sub dl, 'A' - 10
        jmp .pn_hex_add
.pn_hex_digit:
        sub dl, '0'
.pn_hex_add:
        shl eax, 4
        add eax, edx
        inc esi
        jmp .pn_hex_loop

.pn_ok:
        clc
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; print_hex_byte - print AL as 2 hex digits
;---------------------------------------
print_hex_byte:
        pushad
        mov cl, al
        shr al, 4
        call .hex_digit
        mov al, cl
        and al, 0x0F
        call .hex_digit
        popad
        ret
.hex_digit:
        cmp al, 10
        jl .hd_dec
        add al, 'A' - 10
        jmp .hd_print
.hd_dec:
        add al, '0'
.hd_print:
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        ret

;---------------------------------------
; Helpers
;---------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_ret
        inc esi
        jmp skip_spaces
.ss_ret:
        ret

copy_token:
        ; Copy word from [ESI] to [EDI], stop at space/comma/null
        mov al, [esi]
        cmp al, ' '
        je .ct_done
        cmp al, ','
        je .ct_done
        cmp al, 0
        je .ct_done
        movsb
        jmp copy_token
.ct_done:
        mov byte [edi], 0
        ret

skip_token:
        mov al, [esi]
        cmp al, ' '
        je .st_done
        cmp al, ','
        je .st_done
        cmp al, 0
        je .st_done
        inc esi
        jmp skip_token
.st_done:
        ret

skip_comma:
        call skip_spaces
        cmp byte [esi], ','
        jne .sc_ret
        inc esi
        call skip_spaces
.sc_ret:
        ret

; === Data ===
banner_str:     db "== Mellivora x86 Assembler ==", 10
                db "Type instructions to see machine code.", 10
                db "Type 'help' for supported instructions.", 10, 0

prompt_str:     db "asm> ", 0
arrow_str:      db " => ", 0
err_unknown:    db "Unknown instruction: ", 0
err_parse:      db "Parse error - check operands", 10, 0
size_prefix:    db " (", 0
size_suffix:    db " bytes)", 10, 0
newline_str:    db 10, 0
quit_str:       db "quit", 0
exit_str:       db "exit", 0
help_cmd:       db "help", 0

help_text:      db "Supported instructions:", 10
                db "  NOP, HLT, RET, CLC, STC, CLI, STI", 10
                db "  INT <imm8>           - software interrupt", 10
                db "  SYSCALL              - shorthand for INT 0x80", 10
                db "  MOV <reg>, <reg|imm> - move data", 10
                db "  ADD/SUB/AND/OR/XOR/CMP <reg>, <reg|imm>", 10
                db "  PUSH/POP <reg>       - stack operations", 10
                db "  INC/DEC <reg>        - increment/decrement", 10
                db "  SHL/SHR <reg>, <imm> - shift operations", 10
                db "  XCHG <reg>, <reg>    - exchange", 10
                db "  JMP/JZ/JNZ <offset>  - relative jump", 10
                db "  CALL <offset>        - relative call", 10
                db "Registers: EAX ECX EDX EBX ESP EBP ESI EDI", 10
                db "Numbers: decimal or 0x hex", 10, 0

; Mnemonics
mn_nop:         db "NOP", 0
mn_hlt:         db "HLT", 0
mn_ret:         db "RET", 0
mn_clc:         db "CLC", 0
mn_stc:         db "STC", 0
mn_cli:         db "CLI", 0
mn_sti:         db "STI", 0
mn_int:         db "INT", 0
mn_push:        db "PUSH", 0
mn_pop:         db "POP", 0
mn_inc:         db "INC", 0
mn_dec:         db "DEC", 0
mn_mov:         db "MOV", 0
mn_add:         db "ADD", 0
mn_sub:         db "SUB", 0
mn_and:         db "AND", 0
mn_or:          db "OR", 0
mn_xor:         db "XOR", 0
mn_cmp:         db "CMP", 0
mn_xchg:        db "XCHG", 0
mn_shl:         db "SHL", 0
mn_shr:         db "SHR", 0
mn_jmp:         db "JMP", 0
mn_call:        db "CALL", 0
mn_jz:          db "JZ", 0
mn_jnz:         db "JNZ", 0
mn_syscall:     db "SYSCALL", 0

; ALU operation table
alu_names:      dd mn_add, mn_sub, mn_and, mn_or, mn_xor, mn_cmp
alu_opcodes:    db 0x00, 0x28, 0x20, 0x08, 0x30, 0x38  ; base opcodes
alu_digits:     db 0, 5, 4, 1, 6, 7                      ; /digit for 81 form

; Register names (index = encoding)
rn_eax: db "EAX", 0
rn_ecx: db "ECX", 0
rn_edx: db "EDX", 0
rn_ebx: db "EBX", 0
rn_esp: db "ESP", 0
rn_ebp: db "EBP", 0
rn_esi: db "ESI", 0
rn_edi: db "EDI", 0
reg_names:      dd rn_eax, rn_ecx, rn_edx, rn_ebx, rn_esp, rn_ebp, rn_esi, rn_edi

; BSS
input_buf:      times 128 db 0
mnemonic_buf:   times 16 db 0
reg_buf:        times 4 db 0
operand_ptr:    dd 0
dest_reg:       db 0
shift_dir:      db 0
out_bytes:      times MAX_BYTES db 0
out_len:        dd 0

; dis.asm - Simple x86 32-bit disassembler
; Usage: dis <file>
; Reads binary file and decodes common x86 opcodes

%include "syscalls.inc"

BUF_SIZE    equ 65536

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Copy filename
        mov edi, filename
        xor ecx, ecx
.copy_fn:
        mov al, [esi]
        cmp al, ' '
        je .fn_done
        cmp al, 0
        je .fn_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_fn
.fn_done:
        mov byte [edi + ecx], 0

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .read_fail
        mov [file_len], eax

        ; Disassemble
        xor esi, esi    ; offset
.dis_loop:
        cmp esi, [file_len]
        jge .dis_done

        ; Print address
        mov eax, esi
        call print_hex32
        mov eax, SYS_PRINT
        mov ebx, msg_colon
        int 0x80

        ; Print raw byte(s) — first opcode byte
        movzx eax, byte [file_buf + esi]
        mov [opcode], eax
        call print_hex8
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Try to decode opcode
        call decode_opcode

        inc esi
        jmp .dis_loop

.dis_done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .exit
.read_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; decode_opcode: ESI=current offset, [opcode]=byte at offset
; Prints mnemonic and operands, updates ESI
decode_opcode:
        movzx eax, byte [file_buf + esi]
        mov [opcode], eax

        ; NOP
        cmp eax, 0x90
        jne .not_nop
        mov eax, SYS_PRINT
        mov ebx, str_nop
        int 0x80
        ret

.not_nop:
        ; RET near
        cmp eax, 0xC3
        jne .not_ret
        mov eax, SYS_PRINT
        mov ebx, str_ret
        int 0x80
        ret

.not_ret:
        ; RET far
        cmp eax, 0xCB
        jne .not_retf
        mov eax, SYS_PRINT
        mov ebx, str_retf
        int 0x80
        ret

.not_retf:
        ; PUSH reg (0x50-0x57)
        cmp eax, 0x50
        jb .not_push
        cmp eax, 0x57
        ja .not_push
        sub eax, 0x50
        mov eax, SYS_PRINT
        mov ebx, str_push
        int 0x80
        movzx eax, byte [opcode]
        sub eax, 0x50
        call print_reg32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_push:
        ; POP reg (0x58-0x5F)
        cmp eax, 0x58
        jb .not_pop
        cmp eax, 0x5F
        ja .not_pop
        mov eax, SYS_PRINT
        mov ebx, str_pop
        int 0x80
        movzx eax, byte [opcode]
        sub eax, 0x58
        call print_reg32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_pop:
        ; JMP short (0xEB rel8)
        cmp eax, 0xEB
        jne .not_jmpsh
        inc esi
        cmp esi, [file_len]
        jge .not_jmpsh
        movzx ecx, byte [file_buf + esi]
        ; Print second byte
        mov eax, ecx
        call print_hex8
        mov eax, SYS_PRINT
        mov ebx, str_jmps
        int 0x80
        ; Target = current esi + 1 + (int8)ecx
        movsx ecx, cl
        mov eax, esi
        inc eax
        add eax, ecx
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_jmpsh:
        ; JMP near (0xE9 rel32)
        cmp eax, 0xE9
        jne .not_jmpnr
        add esi, 4
        cmp esi, [file_len]
        jge .not_jmpnr
        ; Print 4 bytes
        mov ecx, 1
.jmp_pb:
        cmp ecx, 5
        jge .jmp_pb_done
        movzx eax, byte [file_buf + esi - 4 + ecx]
        call print_hex8
        inc ecx
        jmp .jmp_pb
.jmp_pb_done:
        mov eax, SYS_PRINT
        mov ebx, str_jmp
        int 0x80
        mov eax, [file_buf + esi - 3]
        mov ecx, esi
        inc ecx
        add eax, ecx
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_jmpnr:
        ; CALL near (0xE8 rel32)
        cmp eax, 0xE8
        jne .not_call
        add esi, 4
        cmp esi, [file_len]
        jge .not_call
        mov ecx, 1
.call_pb:
        cmp ecx, 5
        jge .call_pb_done
        movzx eax, byte [file_buf + esi - 4 + ecx]
        call print_hex8
        inc ecx
        jmp .call_pb
.call_pb_done:
        mov eax, SYS_PRINT
        mov ebx, str_call
        int 0x80
        mov eax, [file_buf + esi - 3]
        mov ecx, esi
        inc ecx
        add eax, ecx
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_call:
        ; Jcc short (0x70-0x7F)
        cmp eax, 0x70
        jb .not_jcc
        cmp eax, 0x7F
        ja .not_jcc
        mov [jcc_op], eax
        inc esi
        cmp esi, [file_len]
        jge .not_jcc
        movzx ecx, byte [file_buf + esi]
        mov eax, ecx
        call print_hex8
        mov eax, SYS_PRINT
        mov ebx, str_jcc
        int 0x80
        movzx eax, byte [jcc_op]
        sub eax, 0x70
        call print_jcc_name
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        movsx ecx, cl
        mov eax, esi
        inc eax
        add eax, ecx
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_jcc:
        ; INT imm8 (0xCD)
        cmp eax, 0xCD
        jne .not_int
        inc esi
        cmp esi, [file_len]
        jge .not_int
        movzx ecx, byte [file_buf + esi]
        mov eax, ecx
        call print_hex8
        mov eax, SYS_PRINT
        mov ebx, str_int
        int 0x80
        mov eax, ecx
        call print_hex8
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_int:
        ; HLT (0xF4)
        cmp eax, 0xF4
        jne .not_hlt
        mov eax, SYS_PRINT
        mov ebx, str_hlt
        int 0x80
        ret

.not_hlt:
        ; CLI (0xFA)
        cmp eax, 0xFA
        jne .not_cli
        mov eax, SYS_PRINT
        mov ebx, str_cli
        int 0x80
        ret

.not_cli:
        ; STI (0xFB)
        cmp eax, 0xFB
        jne .not_sti
        mov eax, SYS_PRINT
        mov ebx, str_sti
        int 0x80
        ret

.not_sti:
        ; PUSHA (0x60)
        cmp eax, 0x60
        jne .not_pusha
        mov eax, SYS_PRINT
        mov ebx, str_pusha
        int 0x80
        ret

.not_pusha:
        ; POPA (0x61)
        cmp eax, 0x61
        jne .not_popa
        mov eax, SYS_PRINT
        mov ebx, str_popa
        int 0x80
        ret

.not_popa:
        ; MOV EAX, imm32 (0xB8)
        cmp eax, 0xB8
        jb .not_mov_imm
        cmp eax, 0xBF
        ja .not_mov_imm
        sub eax, 0xB8
        mov [mov_reg], eax
        ; Read 4-byte immediate
        add esi, 4
        cmp esi, [file_len]
        jge .not_mov_imm
        mov ecx, 1
.mi_pb:
        cmp ecx, 5
        jge .mi_pb_done
        movzx eax, byte [file_buf + esi - 4 + ecx]
        call print_hex8
        inc ecx
        jmp .mi_pb
.mi_pb_done:
        mov eax, SYS_PRINT
        mov ebx, str_mov
        int 0x80
        movzx eax, byte [mov_reg]
        call print_reg32
        mov eax, SYS_PRINT
        mov ebx, msg_comma
        int 0x80
        mov eax, [file_buf + esi - 3]
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.not_mov_imm:
        ; 0x0F prefix (2-byte opcodes)
        cmp eax, 0x0F
        jne .unknown

        inc esi
        cmp esi, [file_len]
        jge .unknown
        movzx eax, byte [file_buf + esi]
        call print_hex8
        ; Near Jcc (0x0F 0x80-0x8F)
        cmp eax, 0x80
        jb .unknown
        cmp eax, 0x8F
        ja .unknown
        sub eax, 0x80
        mov [jcc_op], eax
        add esi, 4
        cmp esi, [file_len]
        jge .unknown
        mov ecx, 1
.nj_pb:
        cmp ecx, 5
        jge .nj_pb_done
        movzx eax, byte [file_buf + esi - 4 + ecx]
        call print_hex8
        inc ecx
        jmp .nj_pb
.nj_pb_done:
        mov eax, SYS_PRINT
        mov ebx, str_jcc
        int 0x80
        movzx eax, byte [jcc_op]
        call print_jcc_name
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, [file_buf + esi - 3]
        mov ecx, esi
        inc ecx
        add eax, ecx
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

.unknown:
        mov eax, SYS_PRINT
        mov ebx, str_db
        int 0x80
        movzx eax, byte [opcode]
        call print_hex8
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        ret

print_jcc_name:
        ; EAX = 0-15 for jo,jno,jb,jae,je,jne,jbe,ja,js,jns,jp,jnp,jl,jge,jle,jg
        cmp eax, 15
        ja .pj_unk
        imul eax, 4
        add eax, jcc_names
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.pj_unk:
        mov eax, SYS_PRINT
        mov ebx, str_j_unk
        int 0x80
        ret

print_reg32:
        ; EAX = 0-7
        cmp eax, 7
        ja .pr_unk
        imul eax, 4
        add eax, reg_names
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.pr_unk:
        mov eax, SYS_PRINT
        mov ebx, str_unk_reg
        int 0x80
        ret

print_hex32:
        pushad
        mov edi, 8
.ph32:
        rol eax, 4
        push eax
        and eax, 0x0F
        cmp eax, 10
        jl .ph32_d
        add eax, 'A' - 10
        jmp .ph32_p
.ph32_d:
        add eax, '0'
.ph32_p:
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        pop eax
        dec edi
        jnz .ph32
        popad
        ret

print_hex8:
        pushad
        ; High nibble
        mov ecx, eax
        shr ecx, 4
        and ecx, 0x0F
        cmp ecx, 10
        jl .ph8_dn
        add ecx, 'A' - 10
        jmp .ph8_pn
.ph8_dn:
        add ecx, '0'
.ph8_pn:
        push ecx
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        ; Low nibble
        mov ecx, [esp + 28]
        and ecx, 0x0F
        cmp ecx, 10
        jl .ph8_dl
        add ecx, 'A' - 10
        jmp .ph8_pl
.ph8_dl:
        add ecx, '0'
.ph8_pl:
        push ecx
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        popad
        ret

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

; Strings
msg_usage:      db "Usage: dis <file>", 10, 0
msg_err:        db "dis: cannot read file", 10, 0
msg_colon:      db ":  ", 0
msg_comma:      db ", ", 0

str_nop:        db "        nop", 10, 0
str_ret:        db "        ret", 10, 0
str_retf:       db "        retf", 10, 0
str_hlt:        db "        hlt", 10, 0
str_cli:        db "        cli", 10, 0
str_sti:        db "        sti", 10, 0
str_pusha:      db "        pusha", 10, 0
str_popa:       db "        popa", 10, 0
str_push:       db "        push ", 0
str_pop:        db "        pop  ", 0
str_jmps:       db "        jmp short ", 0
str_jmp:        db "        jmp  ", 0
str_call:       db "        call ", 0
str_jcc:        db "        j", 0
str_int:        db "        int  0x", 0
str_mov:        db "        mov  ", 0
str_db:         db "        db   0x", 0
str_j_unk:      db "??", 0
str_unk_reg:    db "r?", 0

; 32-bit register names (4 bytes each, null-padded)
reg_names:      db "eax",0, "ecx",0, "edx",0, "ebx",0
                db "esp",0, "ebp",0, "esi",0, "edi",0

; Jcc condition names (4 bytes each)
jcc_names:      db "o   ", "no  ", "b   ", "ae  "
                db "e   ", "ne  ", "be  ", "a   "
                db "s   ", "ns  ", "p   ", "np  "
                db "l   ", "ge  ", "le  ", "g   "

filename:       times 256 db 0
arg_buf:        times 256 db 0
opcode:         dd 0
jcc_op:         dd 0
mov_reg:        dd 0
file_len:       dd 0
file_buf:       times BUF_SIZE db 0

; dis.asm — x86 32-bit disassembler for Mellivora OS
;
; Full ModRM/SIB decoder. Covers most common 32-bit protected-mode opcodes.
; Usage: dis <file> [hex_start_offset]

%include "syscalls.inc"

BUF_SIZE    equ 65536

; ── Entry ────────────────────────────────────────────────────────────────────
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

        ; Optional hex offset
        call skip_spaces
        xor eax, eax
.parse_off:
        movzx ecx, byte [esi]
        cmp cl, 0
        je .off_done
        cmp cl, '0'
        jb .off_done
        cmp cl, '9'
        jle .off_dig
        cmp cl, 'a'
        jb .off_nc
        cmp cl, 'f'
        jle .off_lc
.off_nc:
        cmp cl, 'A'
        jb .off_done
        cmp cl, 'F'
        ja .off_done
        sub cl, 'A' - 10
        jmp .off_add
.off_lc:
        sub cl, 'a' - 10
        jmp .off_add
.off_dig:
        sub cl, '0'
.off_add:
        shl eax, 4
        or eax, ecx
        inc esi
        jmp .parse_off
.off_done:
        mov [start_offset], eax

        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .read_fail
        mov [file_len], eax

        mov esi, [start_offset]
.dis_loop:
        cmp esi, [file_len]
        jge .dis_done
        mov eax, esi
        call print_hex32
        mov al, ':'
        call putchar
        mov al, ' '
        call putchar
        call decode_insn
        mov al, 10
        call putchar
        jmp .dis_loop
.dis_done:
        ret
.usage:
        call prt
        dd msg_usage
        ret
.read_fail:
        call prt
        dd msg_err
        ret

; ── decode_insn ──────────────────────────────────────────────────────────────
; Decodes one instruction at file_buf+esi, advances esi.
decode_insn:
        pushad

        ; Prefix scan
        mov byte [pfx_rep], 0
        mov byte [pfx_lock], 0
        mov byte [pfx_opsz], 0
.pfx:
        cmp esi, [file_len]
        jge .eof
        movzx eax, byte [file_buf + esi]
        cmp al, 0xF3
        je .pfx_rep
        cmp al, 0xF2
        je .pfx_repne
        cmp al, 0xF0
        je .pfx_lock
        cmp al, 0x66
        je .pfx_opsz
        cmp al, 0x67
        je .pfx_skip   ; addr-size, skip
        jmp .pfx_done
.pfx_rep:   mov byte [pfx_rep], 1
        jmp .pfx_skip
.pfx_repne: mov byte [pfx_rep], 2
        jmp .pfx_skip
.pfx_lock:  mov byte [pfx_lock], 1
        jmp .pfx_skip
.pfx_opsz:  mov byte [pfx_opsz], 1
.pfx_skip:  inc esi
        jmp .pfx
.pfx_done:

        cmp esi, [file_len]
        jge .eof
        movzx ebx, byte [file_buf + esi]
        mov [cur_op], bl

        ; ── 0x0F two-byte escape ───────────────────────────────────────────
        cmp bl, 0x0F
        je .op_0f

        ; ── NOP 0x90 ──────────────────────────────────────────────────────
        cmp bl, 0x90
        je .op_nop

        ; ── PUSH reg  0x50-0x57 ────────────────────────────────────────────
        cmp bl, 0x50
        jb .no_push_r
        cmp bl, 0x57
        ja .no_push_r
        movzx eax, bl
        sub eax, 0x50
        inc esi
        call prt
        dd str_push
        call print_reg32
        jmp .done
.no_push_r:

        ; ── POP reg  0x58-0x5F ─────────────────────────────────────────────
        cmp bl, 0x58
        jb .no_pop_r
        cmp bl, 0x5F
        ja .no_pop_r
        movzx eax, bl
        sub eax, 0x58
        inc esi
        call prt
        dd str_pop
        call print_reg32
        jmp .done
.no_pop_r:

        ; ── INC reg  0x40-0x47 ─────────────────────────────────────────────
        cmp bl, 0x40
        jb .no_inc_r
        cmp bl, 0x47
        ja .no_inc_r
        movzx eax, bl
        sub eax, 0x40
        inc esi
        call prt
        dd str_inc
        call print_reg32
        jmp .done
.no_inc_r:

        ; ── DEC reg  0x48-0x4F ─────────────────────────────────────────────
        cmp bl, 0x48
        jb .no_dec_r
        cmp bl, 0x4F
        ja .no_dec_r
        movzx eax, bl
        sub eax, 0x48
        inc esi
        call prt
        dd str_dec
        call print_reg32
        jmp .done
.no_dec_r:

        ; ── MOV reg32, imm32  0xB8-0xBF ────────────────────────────────────
        cmp bl, 0xB8
        jb .no_movri32
        cmp bl, 0xBF
        ja .no_movri32
        movzx eax, bl
        sub eax, 0xB8
        mov [tmp_r], al
        inc esi
        call read_imm32
        mov [tmp_imm], eax
        call prt
        dd str_mov
        movzx eax, byte [tmp_r]
        call print_reg32
        call print_comma
        mov eax, [tmp_imm]
        call print_hex32_0x
        jmp .done
.no_movri32:

        ; ── MOV reg8, imm8  0xB0-0xB7 ──────────────────────────────────────
        cmp bl, 0xB0
        jb .no_movri8
        cmp bl, 0xB7
        ja .no_movri8
        movzx eax, bl
        sub eax, 0xB0
        mov [tmp_r], al
        inc esi
        call read_imm8
        mov [tmp_imm], eax
        call prt
        dd str_mov
        movzx eax, byte [tmp_r]
        call print_reg8
        call print_comma
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        movzx eax, byte [tmp_imm]
        call print_hex8
        jmp .done
.no_movri8:

        ; ── PUSH imm32  0x68 ────────────────────────────────────────────────
        cmp bl, 0x68
        jne .no_pushimm32
        inc esi
        call read_imm32
        call prt
        dd str_push
        call print_hex32_0x
        jmp .done
.no_pushimm32:

        ; ── PUSH imm8 sign-ext  0x6A ────────────────────────────────────────
        cmp bl, 0x6A
        jne .no_pushimm8
        inc esi
        call read_imm8
        movsx eax, al
        call prt
        dd str_push
        call print_hex32_0x
        jmp .done
.no_pushimm8:

        ; ── Simple single-byte no-operand ───────────────────────────────────
        cmp bl, 0xC3
        jne .no_ret
        inc esi
        call prt
        dd str_ret
        jmp .done
.no_ret:
        cmp bl, 0xCB
        jne .no_retf
        inc esi
        call prt
        dd str_retf
        jmp .done
.no_retf:
        cmp bl, 0xF4
        jne .no_hlt
        inc esi
        call prt
        dd str_hlt
        jmp .done
.no_hlt:
        cmp bl, 0xFA
        jne .no_cli
        inc esi
        call prt
        dd str_cli
        jmp .done
.no_cli:
        cmp bl, 0xFB
        jne .no_sti
        inc esi
        call prt
        dd str_sti
        jmp .done
.no_sti:
        cmp bl, 0xFC
        jne .no_cld
        inc esi
        call prt
        dd str_cld
        jmp .done
.no_cld:
        cmp bl, 0xFD
        jne .no_std
        inc esi
        call prt
        dd str_std
        jmp .done
.no_std:
        cmp bl, 0xF5
        jne .no_cmc
        inc esi
        call prt
        dd str_cmc
        jmp .done
.no_cmc:
        cmp bl, 0x60
        jne .no_pusha
        inc esi
        call prt
        dd str_pusha
        jmp .done
.no_pusha:
        cmp bl, 0x61
        jne .no_popa
        inc esi
        call prt
        dd str_popa
        jmp .done
.no_popa:
        cmp bl, 0x9C
        jne .no_pushf
        inc esi
        call prt
        dd str_pushf
        jmp .done
.no_pushf:
        cmp bl, 0x9D
        jne .no_popf
        inc esi
        call prt
        dd str_popf
        jmp .done
.no_popf:
        cmp bl, 0x9E
        jne .no_sahf
        inc esi
        call prt
        dd str_sahf
        jmp .done
.no_sahf:
        cmp bl, 0x9F
        jne .no_lahf
        inc esi
        call prt
        dd str_lahf
        jmp .done
.no_lahf:
        cmp bl, 0x98
        jne .no_cwde
        inc esi
        call prt
        dd str_cwde
        jmp .done
.no_cwde:
        cmp bl, 0x99
        jne .no_cdq
        inc esi
        call prt
        dd str_cdq
        jmp .done
.no_cdq:
        cmp bl, 0xCC
        jne .no_int3
        inc esi
        call prt
        dd str_int3
        jmp .done
.no_int3:
        cmp bl, 0xCE
        jne .no_into
        inc esi
        call prt
        dd str_into
        jmp .done
.no_into:
        cmp bl, 0xCF
        jne .no_iret
        inc esi
        call prt
        dd str_iret
        jmp .done
.no_iret:
        cmp bl, 0xC9
        jne .no_leave
        inc esi
        call prt
        dd str_leave
        jmp .done
.no_leave:
        cmp bl, 0xD7
        jne .no_xlat
        inc esi
        call prt
        dd str_xlat
        jmp .done
.no_xlat:

        ; ── INT imm8  0xCD ──────────────────────────────────────────────────
        cmp bl, 0xCD
        jne .no_int
        inc esi
        call read_imm8
        mov [tmp_imm], eax
        call prt
        dd str_int
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        movzx eax, byte [tmp_imm]
        call print_hex8
        jmp .done
.no_int:

        ; ── CALL near  0xE8 ─────────────────────────────────────────────────
        cmp bl, 0xE8
        jne .no_call
        inc esi
        call read_imm32
        call prt
        dd str_call
        add eax, esi
        call print_hex32_0x
        jmp .done
.no_call:

        ; ── JMP near  0xE9 ──────────────────────────────────────────────────
        cmp bl, 0xE9
        jne .no_jmp32
        inc esi
        call read_imm32
        call prt
        dd str_jmp
        add eax, esi
        call print_hex32_0x
        jmp .done
.no_jmp32:

        ; ── JMP short  0xEB ─────────────────────────────────────────────────
        cmp bl, 0xEB
        jne .no_jmps
        inc esi
        call read_imm8
        call prt
        dd str_jmp
        movsx ecx, al
        lea eax, [esi + ecx]
        call print_hex32_0x
        jmp .done
.no_jmps:

        ; ── Jcc short  0x70-0x7F ────────────────────────────────────────────
        cmp bl, 0x70
        jb .no_jcc8
        cmp bl, 0x7F
        ja .no_jcc8
        movzx eax, bl
        sub eax, 0x70
        mov [tmp_r], al
        inc esi
        call read_imm8
        call prt
        dd str_j
        movzx eax, byte [tmp_r]
        call print_cond
        mov al, ' '
        call putchar
        movsx ecx, al
        lea eax, [esi + ecx]
        call print_hex32_0x
        jmp .done
.no_jcc8:

        ; ── XCHG EAX, reg  0x91-0x97 ────────────────────────────────────────
        cmp bl, 0x91
        jb .no_xchg_r
        cmp bl, 0x97
        ja .no_xchg_r
        movzx eax, bl
        sub eax, 0x90
        mov [tmp_r], al
        inc esi
        call prt
        dd str_xchg
        xor eax, eax
        call print_reg32
        call print_comma
        movzx eax, byte [tmp_r]
        call print_reg32
        jmp .done
.no_xchg_r:

        ; ── LOOP/LOOPcc/JCXZ  0xE0-0xE3 ────────────────────────────────────
        cmp bl, 0xE0
        jb .no_loop
        cmp bl, 0xE3
        ja .no_loop
        movzx eax, bl
        sub eax, 0xE0
        mov [tmp_r], al
        inc esi
        call read_imm8
        mov [tmp_imm], eax
        movzx eax, byte [tmp_r]
        cmp eax, 0
        je .loop_ne
        cmp eax, 1
        je .loop_e
        cmp eax, 2
        je .loop_plain
        ; 3 = JCXZ
        call prt
        dd str_jcxz
        jmp .loop_target
.loop_ne:
        call prt
        dd str_loopne
        jmp .loop_target
.loop_e:
        call prt
        dd str_loope
        jmp .loop_target
.loop_plain:
        call prt
        dd str_loop
.loop_target:
        movzx eax, byte [tmp_imm]
        movsx ecx, al
        lea eax, [esi + ecx]
        call print_hex32_0x
        jmp .done
.no_loop:

        ; ── ENTER  0xC8 ─────────────────────────────────────────────────────
        cmp bl, 0xC8
        jne .no_enter
        inc esi
        call read_imm16
        push eax
        call read_imm8
        mov [tmp_imm], eax
        call prt
        dd str_enter
        pop eax
        call print_hex16_0x
        call print_comma
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        movzx eax, byte [tmp_imm]
        call print_hex8
        jmp .done
.no_enter:

        ; ── Arithmetic group  0x00-0x3D ─────────────────────────────────────
        cmp bl, 0x3D
        ja .no_arith
        call decode_arith
        jmp .done
.no_arith:

        ; ── GRP1  0x80-0x83 ─────────────────────────────────────────────────
        cmp bl, 0x80
        jb .no_grp1
        cmp bl, 0x83
        ja .no_grp1
        call decode_grp1
        jmp .done
.no_grp1:

        ; ── TEST  0x84/0x85 ─────────────────────────────────────────────────
        cmp bl, 0x84
        je .op_test
        cmp bl, 0x85
        jne .no_test
.op_test:
        call decode_test
        jmp .done
.no_test:

        ; ── TEST AL/EAX,imm  0xA8/0xA9 ──────────────────────────────────────
        cmp bl, 0xA8
        je .op_testimm
        cmp bl, 0xA9
        jne .no_testimm
.op_testimm:
        call decode_test_imm
        jmp .done
.no_testimm:

        ; ── XCHG r/m  0x86/0x87 ────────────────────────────────────────────
        cmp bl, 0x86
        je .op_xchg_rm
        cmp bl, 0x87
        jne .no_xchg_rm
.op_xchg_rm:
        call decode_xchg_rm
        jmp .done
.no_xchg_rm:

        ; ── MOV r/m  0x88-0x8B ─────────────────────────────────────────────
        cmp bl, 0x88
        jb .no_movrm
        cmp bl, 0x8B
        ja .no_movrm
        call decode_mov_rm
        jmp .done
.no_movrm:

        ; ── LEA  0x8D ───────────────────────────────────────────────────────
        cmp bl, 0x8D
        jne .no_lea
        call decode_lea
        jmp .done
.no_lea:

        ; ── POP r/m  0x8F ───────────────────────────────────────────────────
        cmp bl, 0x8F
        jne .no_pop_rm
        inc esi
        call parse_modrm
        call prt
        dd str_pop
        call print_rm32
        jmp .done
.no_pop_rm:

        ; ── MOV moffs  0xA0-0xA3 ────────────────────────────────────────────
        cmp bl, 0xA0
        jb .no_moffs
        cmp bl, 0xA3
        ja .no_moffs
        call decode_moffs
        jmp .done
.no_moffs:

        ; ── String ops  0xA4-0xAF ───────────────────────────────────────────
        cmp bl, 0xA4
        jb .no_strop
        cmp bl, 0xAF
        ja .no_strop
        call decode_strop
        jmp .done
.no_strop:

        ; ── Shift/rot imm  0xC0/0xC1 ────────────────────────────────────────
        cmp bl, 0xC0
        je .op_shrimm
        cmp bl, 0xC1
        jne .no_shrimm
.op_shrimm:
        call decode_shift_imm
        jmp .done
.no_shrimm:

        ; ── MOV r/m,imm  0xC6/0xC7 ──────────────────────────────────────────
        cmp bl, 0xC6
        je .op_movmi
        cmp bl, 0xC7
        jne .no_movmi
.op_movmi:
        call decode_mov_mi
        jmp .done
.no_movmi:

        ; ── Shift/rot 1/CL  0xD0-0xD3 ──────────────────────────────────────
        cmp bl, 0xD0
        jb .no_shift
        cmp bl, 0xD3
        ja .no_shift
        call decode_shift
        jmp .done
.no_shift:

        ; ── IMUL reg,r/m,imm  0x69/0x6B ────────────────────────────────────
        cmp bl, 0x69
        je .op_imul3
        cmp bl, 0x6B
        jne .no_imul3
.op_imul3:
        mov [cur_op], bl
        inc esi
        call parse_modrm
        call prt
        dd str_imul
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        call print_rm32
        call print_comma
        cmp byte [cur_op], 0x6B
        je .imul3_imm8
        call read_imm32
        call print_hex32_0x
        jmp .done
.imul3_imm8:
        call read_imm8
        movsx eax, al
        call print_hex32_0x
        jmp .done
.no_imul3:

        ; ── IN/OUT  0xE4-0xE7,0xEC-0xEF ─────────────────────────────────────
        cmp bl, 0xE4
        jb .no_io
        cmp bl, 0xEF
        ja .no_io
        call decode_io
        jmp .done
.no_io:

        ; ── GRP3  0xF6/0xF7 ─────────────────────────────────────────────────
        cmp bl, 0xF6
        je .op_grp3
        cmp bl, 0xF7
        jne .no_grp3
.op_grp3:
        call decode_grp3
        jmp .done
.no_grp3:

        ; ── GRP4  0xFE ──────────────────────────────────────────────────────
        cmp bl, 0xFE
        jne .no_grp4
        inc esi
        call parse_modrm
        movzx eax, byte [modrm_reg]
        test eax, eax
        jz .grp4_inc
        call prt
        dd str_dec
        call print_rm8
        jmp .done
.grp4_inc:
        call prt
        dd str_inc
        call print_rm8
        jmp .done
.no_grp4:

        ; ── GRP5  0xFF ──────────────────────────────────────────────────────
        cmp bl, 0xFF
        jne .no_grp5
        call decode_grp5
        jmp .done
.no_grp5:

        ; ── RET+imm16  0xC2 ─────────────────────────────────────────────────
        cmp bl, 0xC2
        jne .no_ret_imm
        inc esi
        call read_imm16
        call prt
        dd str_ret
        call print_comma
        call print_hex16_0x
        jmp .done
.no_ret_imm:

        ; ── CALL far / JMP far  0x9A / 0xEA ────────────────────────────────
        ; (just show raw for now — fall through to db)

        ; ── Unknown ──────────────────────────────────────────────────────────
        call prt
        dd str_db
        movzx eax, byte [file_buf + esi]
        inc esi
        call print_hex8
        jmp .done

.op_nop:
        inc esi
        call prt
        dd str_nop
        jmp .done

.op_0f:
        inc esi
        cmp esi, [file_len]
        jge .eof
        movzx ebx, byte [file_buf + esi]
        ; Jcc near  0x80-0x8F
        cmp bl, 0x80
        jb .0f_not_jcc
        cmp bl, 0x8F
        ja .0f_not_jcc
        movzx eax, bl
        sub eax, 0x80
        mov [tmp_r], al
        inc esi
        call read_imm32
        call prt
        dd str_j
        movzx eax, byte [tmp_r]
        call print_cond
        mov al, ' '
        call putchar
        add eax, esi
        call print_hex32_0x
        jmp .done
.0f_not_jcc:
        ; SETCC  0x90-0x9F
        cmp bl, 0x90
        jb .0f_not_set
        cmp bl, 0x9F
        ja .0f_not_set
        movzx eax, bl
        sub eax, 0x90
        mov [tmp_r], al
        inc esi
        call parse_modrm
        call prt
        dd str_set
        movzx eax, byte [tmp_r]
        call print_cond
        mov al, ' '
        call putchar
        call print_rm8
        jmp .done
.0f_not_set:
        ; MOVZX  0xB6/0xB7
        cmp bl, 0xB6
        je .0f_movzx
        cmp bl, 0xB7
        jne .0f_not_movzx
.0f_movzx:
        mov [tmp_r], bl
        inc esi
        call parse_modrm
        call prt
        dd str_movzx
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        cmp byte [tmp_r], 0xB6
        je .movzx_b
        call print_rm16
        jmp .done
.movzx_b: call print_rm8
        jmp .done
.0f_not_movzx:
        ; MOVSX  0xBE/0xBF
        cmp bl, 0xBE
        je .0f_movsx
        cmp bl, 0xBF
        jne .0f_not_movsx
.0f_movsx:
        mov [tmp_r], bl
        inc esi
        call parse_modrm
        call prt
        dd str_movsx
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        cmp byte [tmp_r], 0xBE
        je .movsx_b
        call print_rm16
        jmp .done
.movsx_b: call print_rm8
        jmp .done
.0f_not_movsx:
        ; IMUL r,r/m  0xAF
        cmp bl, 0xAF
        jne .0f_not_imul
        inc esi
        call parse_modrm
        call prt
        dd str_imul
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        call print_rm32
        jmp .done
.0f_not_imul:
        ; BSF/BSR  0xBC/0xBD
        cmp bl, 0xBC
        je .0f_bsf
        cmp bl, 0xBD
        jne .0f_not_bs
.0f_bsf:
        mov [tmp_r], bl
        inc esi
        call parse_modrm
        cmp byte [tmp_r], 0xBC
        je .bsf_op
        call prt
        dd str_bsr
        jmp .bsxop
.bsf_op:
        call prt
        dd str_bsf
.bsxop:
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        call print_rm32
        jmp .done
.0f_not_bs:
        ; BSWAP  0xC8-0xCF
        cmp bl, 0xC8
        jb .0f_not_bswap
        cmp bl, 0xCF
        ja .0f_not_bswap
        movzx eax, bl
        sub eax, 0xC8
        inc esi
        call prt
        dd str_bswap
        call print_reg32
        jmp .done
.0f_not_bswap:
        ; CPUID 0xA2 / RDTSC 0x31
        cmp bl, 0xA2
        jne .0f_not_cpuid
        inc esi
        call prt
        dd str_cpuid
        jmp .done
.0f_not_cpuid:
        cmp bl, 0x31
        jne .0f_not_rdtsc
        inc esi
        call prt
        dd str_rdtsc
        jmp .done
.0f_not_rdtsc:
        ; BT/BTS/BTR/BTC  0xA3/0xAB/0xB3/0xBB
        cmp bl, 0xA3
        je .0f_bt
        cmp bl, 0xAB
        je .0f_bts
        cmp bl, 0xB3
        je .0f_btr
        cmp bl, 0xBB
        je .0f_btc
        jmp .0f_unk
.0f_bt:  mov [tmp_r], dword bt_mnem
        jmp .0f_bt_common
.0f_bts: mov [tmp_r], dword bts_mnem
        jmp .0f_bt_common
.0f_btr: mov [tmp_r], dword btr_mnem
        jmp .0f_bt_common
.0f_btc: mov [tmp_r], dword btc_mnem
.0f_bt_common:
        inc esi
        call parse_modrm
        mov eax, SYS_PRINT
        mov ebx, [tmp_r]
        int 0x80
        call print_rm32
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg32
        jmp .done
.0f_unk:
        ; Unknown 0F XX — back up and emit db
        dec esi
        call prt
        dd str_db
        movzx eax, byte [file_buf + esi]
        inc esi
        call print_hex8
        jmp .done

.eof:
.done:
        mov [esp + 20], esi     ; write esi back through pushad frame
        popad
        ret

; ── Decoder helpers ───────────────────────────────────────────────────────────

; Arithmetic group 0x00-0x3D
decode_arith:
        movzx eax, byte [cur_op]
        mov ecx, eax
        shr ecx, 3
        and ecx, 7
        mov [arith_op], cl  ; 0-7
        and eax, 7
        mov [arith_enc], al               ; encoding 0-5
        inc esi

        movzx eax, byte [arith_op]
        cmp eax, 4
        je .al_imm   ; enc must also be 4
        ; use arith_enc to distinguish
        movzx eax, byte [arith_enc]
        cmp eax, 4
        je .al_imm
        cmp eax, 5
        je .eax_imm
        cmp eax, 6
        jge .ag_unk
        cmp eax, 7
        je .ag_unk

        call parse_modrm
        movzx eax, byte [arith_op]
        call print_arith_mnem

        movzx eax, byte [arith_enc]
        cmp eax, 0
        je .ag_rm8_r8
        cmp eax, 1
        je .ag_rmv_rv
        cmp eax, 2
        je .ag_r8_rm8
        ; 3: r32, r/m32
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        call print_rm32
        ret

.ag_rm8_r8:
        call print_rm8
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg8
        ret
.ag_rmv_rv:
        call print_rm32
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg32
        ret
.ag_r8_rm8:
        movzx eax, byte [modrm_reg]
        call print_reg8
        call print_comma
        call print_rm8
        ret

.al_imm:
        call read_imm8
        mov [tmp_imm], eax
        movzx eax, byte [arith_op]
        call print_arith_mnem
        call prt
        dd str_al
        call print_comma
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        movzx eax, byte [tmp_imm]
        call print_hex8
        ret

.eax_imm:
        call read_imm32
        mov [tmp_imm], eax
        movzx eax, byte [arith_op]
        call print_arith_mnem
        xor eax, eax
        call print_reg32
        call print_comma
        mov eax, [tmp_imm]
        call print_hex32_0x
        ret

.ag_unk:
        dec esi
        jmp decode_insn.eof

; GRP1 0x80-0x83
decode_grp1:
        movzx ecx, byte [cur_op]
        mov [grp1_sz], cl
        inc esi
        call parse_modrm
        movzx eax, byte [modrm_reg]
        call print_arith_mnem
        cmp byte [grp1_sz], 0x80
        je .g1_rm8
        cmp byte [grp1_sz], 0x82
        je .g1_rm8
        cmp byte [grp1_sz], 0x81
        jne .g1_rmv_imm8
        ; 0x81: Ev, imm32
        call print_rm32
        call print_comma
        call read_imm32
        call print_hex32_0x
        ret
.g1_rmv_imm8:
        ; 0x83: Ev, imm8 sign-ext
        call print_rm32
        call print_comma
        call read_imm8
        movsx eax, al
        call print_hex32_0x
        ret
.g1_rm8:
        call print_rm8
        call print_comma
        call read_imm8
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        call print_hex8
        ret

; TEST 0x84/0x85
decode_test:
        movzx ecx, byte [cur_op]
        inc esi
        call parse_modrm
        call prt
        dd str_test
        cmp ecx, 0x84
        je .t8
        call print_rm32
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg32
        ret
.t8:
        call print_rm8
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg8
        ret

; TEST AL/EAX, imm
decode_test_imm:
        movzx ecx, byte [cur_op]
        inc esi
        call prt
        dd str_test
        cmp ecx, 0xA8
        jne .ti_eax
        call prt
        dd str_al
        call print_comma
        call read_imm8
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        call print_hex8
        ret
.ti_eax:
        xor eax, eax
        call print_reg32
        call print_comma
        call read_imm32
        call print_hex32_0x
        ret

; XCHG r/m
decode_xchg_rm:
        movzx ecx, byte [cur_op]
        inc esi
        call parse_modrm
        call prt
        dd str_xchg
        cmp ecx, 0x86
        je .xr8
        call print_rm32
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg32
        ret
.xr8:
        call print_rm8
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg8
        ret

; MOV r/m 0x88-0x8B
decode_mov_rm:
        movzx ecx, byte [cur_op]
        inc esi
        call parse_modrm
        call prt
        dd str_mov
        cmp ecx, 0x88
        je .mv_rm8_r8
        cmp ecx, 0x89
        je .mv_rmv_rv
        cmp ecx, 0x8A
        je .mv_r8_rm8
        ; 0x8B: r32, r/m32
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        call print_rm32
        ret
.mv_rm8_r8:
        call print_rm8
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg8
        ret
.mv_rmv_rv:
        call print_rm32
        call print_comma
        movzx eax, byte [modrm_reg]
        call print_reg32
        ret
.mv_r8_rm8:
        movzx eax, byte [modrm_reg]
        call print_reg8
        call print_comma
        call print_rm8
        ret

; LEA 0x8D
decode_lea:
        inc esi
        call parse_modrm
        call prt
        dd str_lea
        movzx eax, byte [modrm_reg]
        call print_reg32
        call print_comma
        call print_rm_mem
        ret

; MOV moffs 0xA0-0xA3
decode_moffs:
        movzx ecx, byte [cur_op]
        inc esi
        call read_imm32
        mov [tmp_imm], eax
        call prt
        dd str_mov
        cmp ecx, 0xA0
        je .mof_al_m
        cmp ecx, 0xA1
        je .mof_eax_m
        cmp ecx, 0xA2
        je .mof_m_al
        ; A3: [moffs], eax
        mov al, '['
        call putchar
        mov eax, [tmp_imm]
        call print_hex32_0x
        mov al, ']'
        call putchar
        call print_comma
        xor eax, eax
        call print_reg32
        ret
.mof_al_m:
        call prt
        dd str_al
        call print_comma
        mov al, '['
        call putchar
        mov eax, [tmp_imm]
        call print_hex32_0x
        mov al, ']'
        call putchar
        ret
.mof_eax_m:
        xor eax, eax
        call print_reg32
        call print_comma
        mov al, '['
        call putchar
        mov eax, [tmp_imm]
        call print_hex32_0x
        mov al, ']'
        call putchar
        ret
.mof_m_al:
        mov al, '['
        call putchar
        mov eax, [tmp_imm]
        call print_hex32_0x
        mov al, ']'
        call putchar
        call print_comma
        call prt
        dd str_al
        ret

; String ops 0xA4-0xAF
decode_strop:
        movzx ecx, byte [cur_op]
        inc esi
        ; REP/REPNE prefix
        movzx eax, byte [pfx_rep]
        cmp eax, 1
        je .ds_rep
        cmp eax, 2
        je .ds_repne
        jmp .ds_op
.ds_rep:   call prt
        dd str_rep
        jmp .ds_op
.ds_repne: call prt
        dd str_repne
.ds_op:
        ; mnemonic by opcode
        movzx eax, byte [cur_op]
        cmp eax, 0xA4
        je .so_movsb
        cmp eax, 0xA5
        je .so_movsd
        cmp eax, 0xA6
        je .so_cmpsb
        cmp eax, 0xA7
        je .so_cmpsd
        cmp eax, 0xAA
        je .so_stosb
        cmp eax, 0xAB
        je .so_stosd
        cmp eax, 0xAC
        je .so_lodsb
        cmp eax, 0xAD
        je .so_lodsd
        cmp eax, 0xAE
        je .so_scasb
        cmp eax, 0xAF
        je .so_scasd
        ret
.so_movsb: call prt
        dd str_movsb
        ret
.so_movsd: call prt
        dd str_movsd
        ret
.so_cmpsb: call prt
        dd str_cmpsb
        ret
.so_cmpsd: call prt
        dd str_cmpsd
        ret
.so_stosb: call prt
        dd str_stosb
        ret
.so_stosd: call prt
        dd str_stosd
        ret
.so_lodsb: call prt
        dd str_lodsb
        ret
.so_lodsd: call prt
        dd str_lodsd
        ret
.so_scasb: call prt
        dd str_scasb
        ret
.so_scasd: call prt
        dd str_scasd
        ret

; Shift/rot imm 0xC0/0xC1
decode_shift_imm:
        movzx ecx, byte [cur_op]
        inc esi
        call parse_modrm
        movzx eax, byte [modrm_reg]
        call print_shift_mnem
        cmp ecx, 0xC0
        je .si_rm8
        call print_rm32
        call print_comma
        call read_imm8
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        call print_hex8
        ret
.si_rm8:
        call print_rm8
        call print_comma
        call read_imm8
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        call print_hex8
        ret

; Shift/rot 0xD0-0xD3
decode_shift:
        movzx ecx, byte [cur_op]
        inc esi
        call parse_modrm
        movzx eax, byte [modrm_reg]
        call print_shift_mnem
        cmp ecx, 0xD0
        je .sh_rm8_1
        cmp ecx, 0xD1
        je .sh_rmv_1
        cmp ecx, 0xD2
        je .sh_rm8_cl
        ; D3: r/m32, cl
        call print_rm32
        call print_comma
        call prt
        dd str_cl
        ret
.sh_rm8_1:
        call print_rm8
        call print_comma
        mov al, '1'
        call putchar
        ret
.sh_rmv_1:
        call print_rm32
        call print_comma
        mov al, '1'
        call putchar
        ret
.sh_rm8_cl:
        call print_rm8
        call print_comma
        call prt
        dd str_cl
        ret

; MOV r/m, imm 0xC6/0xC7
decode_mov_mi:
        movzx ecx, byte [cur_op]
        inc esi
        call parse_modrm
        call prt
        dd str_mov
        cmp ecx, 0xC6
        je .mmi8
        call print_rm32
        call print_comma
        call read_imm32
        call print_hex32_0x
        ret
.mmi8:
        call print_rm8
        call print_comma
        call read_imm8
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        call print_hex8
        ret

; GRP3 0xF6/0xF7
decode_grp3:
        movzx ecx, byte [cur_op]
        inc esi
        call parse_modrm
        movzx eax, byte [modrm_reg]
        cmp eax, 0
        je .g3_test
        cmp eax, 1
        je .g3_test
        cmp eax, 2
        je .g3_not
        cmp eax, 3
        je .g3_neg
        cmp eax, 4
        je .g3_mul
        cmp eax, 5
        je .g3_imul
        cmp eax, 6
        je .g3_div
        ; 7: idiv
        call prt
        dd str_idiv
        jmp .g3_operand
.g3_test:
        call prt
        dd str_test
        cmp ecx, 0xF6
        je .g3_t8
        call print_rm32
        call print_comma
        call read_imm32
        call print_hex32_0x
        ret
.g3_t8:
        call print_rm8
        call print_comma
        call read_imm8
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        call print_hex8
        ret
.g3_not:  call prt
        dd str_not
        jmp .g3_operand
.g3_neg:  call prt
        dd str_neg
        jmp .g3_operand
.g3_mul:  call prt
        dd str_mul
        jmp .g3_operand
.g3_imul: call prt
        dd str_imul
        jmp .g3_operand
.g3_div:  call prt
        dd str_div
.g3_operand:
        cmp ecx, 0xF6
        je .g3_op8
        call print_rm32
        ret
.g3_op8:
        call print_rm8
        ret

; GRP5 0xFF
decode_grp5:
        inc esi
        call parse_modrm
        movzx eax, byte [modrm_reg]
        cmp eax, 0
        je .g5_inc
        cmp eax, 1
        je .g5_dec
        cmp eax, 2
        je .g5_call
        cmp eax, 4
        je .g5_jmp
        cmp eax, 6
        je .g5_push
        ; 3=CALLF 5=JMPF
        call prt
        dd str_db
        movzx eax, byte [file_buf + esi - 1]
        call print_hex8
        ret
.g5_inc:  call prt
        dd str_inc
        call print_rm32
        ret
.g5_dec:  call prt
        dd str_dec
        call print_rm32
        ret
.g5_call: call prt
        dd str_call
        call print_rm32
        ret
.g5_jmp:  call prt
        dd str_jmp
        call print_rm32
        ret
.g5_push: call prt
        dd str_push
        call print_rm32
        ret

; IN/OUT 0xE4-0xE7, 0xEC-0xEF
decode_io:
        movzx ecx, byte [cur_op]
        inc esi
        cmp ecx, 0xE4
        je .in_imm
        cmp ecx, 0xE5
        je .in_imm
        cmp ecx, 0xE6
        je .out_imm
        cmp ecx, 0xE7
        je .out_imm
        cmp ecx, 0xEC
        je .in_dx
        cmp ecx, 0xED
        je .in_dx
        cmp ecx, 0xEE
        je .out_dx
        cmp ecx, 0xEF
        je .out_dx
        ret
.in_imm:
        call read_imm8
        push eax
        call prt
        dd str_in
        movzx eax, cl
        and eax, 1
        call print_accum
        call print_comma
        pop eax
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        pop eax
        call print_hex8
        ret
.out_imm:
        call read_imm8
        push eax
        call prt
        dd str_out
        pop eax
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        call print_hex8
        call print_comma
        movzx eax, cl
        and eax, 1
        call print_accum
        ret
.in_dx:
        call prt
        dd str_in
        movzx eax, cl
        and eax, 1
        call print_accum
        call print_comma
        call prt
        dd str_dx
        ret
.out_dx:
        call prt
        dd str_out
        call prt
        dd str_dx
        call print_comma
        movzx eax, cl
        and eax, 1
        call print_accum
        ret

; ── parse_modrm ──────────────────────────────────────────────────────────────
; Parses ModRM byte (+ optional SIB + displacement) at file_buf+esi.
; Sets modrm_mod / modrm_reg / modrm_rm / has_sib / sib_* / disp32.
; Advances esi past all consumed bytes.
parse_modrm:
        push eax
        push ecx
        cmp esi, [file_len]
        jge .pm_done
        movzx eax, byte [file_buf + esi]
        inc esi

        mov ecx, eax
        shr ecx, 6
        and ecx, 3
        mov [modrm_mod], cl
        mov ecx, eax
        shr ecx, 3
        and ecx, 7
        mov [modrm_reg], cl
        and eax, 7
        mov [modrm_rm], al
        mov dword [disp32], 0
        mov byte [has_sib], 0

        cmp byte [modrm_mod], 3
        je .pm_done  ; register

        cmp byte [modrm_rm], 4
        jne .pm_no_sib
        ; SIB byte
        cmp esi, [file_len]
        jge .pm_done
        movzx eax, byte [file_buf + esi]
        inc esi
        mov byte [has_sib], 1
        mov ecx, eax
        shr ecx, 6
        and ecx, 3
        mov [sib_scale], cl
        mov ecx, eax
        shr ecx, 3
        and ecx, 7
        mov [sib_idx], cl
        and eax, 7
        mov [sib_base], al
.pm_no_sib:
        cmp byte [modrm_mod], 0
        jne .pm_not0
        cmp byte [modrm_rm], 5
        jne .pm_done  ; mod=0,rm=5 → disp32
        call rd_imm32_raw
        mov [disp32], eax
        jmp .pm_done
.pm_not0:
        cmp byte [modrm_mod], 1
        jne .pm_mod2
        ; mod=1: disp8
        cmp esi, [file_len]
        jge .pm_done
        movzx eax, byte [file_buf + esi]
        inc esi
        movsx eax, al
        mov [disp32], eax
        jmp .pm_done
.pm_mod2:
        call rd_imm32_raw
        mov [disp32], eax
.pm_done:
        pop ecx
        pop eax
        ret

; ── Print r/m operand ─────────────────────────────────────────────────────────
print_rm32:
        cmp byte [modrm_mod], 3
        jne print_rm_mem
        movzx eax, byte [modrm_rm]
        call print_reg32
        ret

print_rm16:
        cmp byte [modrm_mod], 3
        jne print_rm_mem
        movzx eax, byte [modrm_rm]
        call print_reg16
        ret

print_rm8:
        cmp byte [modrm_mod], 3
        jne print_rm_mem
        movzx eax, byte [modrm_rm]
        call print_reg8
        ret

; print_rm_mem — always print as memory reference [...]
print_rm_mem:
        mov al, '['
        call putchar

        cmp byte [has_sib], 1
        je .prmm_sib

        ; mod=0,rm=5 → [disp32]
        cmp byte [modrm_mod], 0
        jne .prmm_base
        cmp byte [modrm_rm], 5
        jne .prmm_base
        mov eax, [disp32]
        call print_hex32_0x
        mov al, ']'
        call putchar
        ret

.prmm_base:
        movzx eax, byte [modrm_rm]
        call print_reg32
        jmp .prmm_disp

.prmm_sib:
        ; base (unless base=5 and mod=0)
        movzx eax, byte [sib_base]
        cmp eax, 5
        jne .prmm_sib_base
        cmp byte [modrm_mod], 0
        je .prmm_sib_idx_first
.prmm_sib_base:
        movzx eax, byte [sib_base]
        call print_reg32
        mov byte [sib_printed], 1
        jmp .prmm_sib_idx
.prmm_sib_idx_first:
        mov byte [sib_printed], 0
.prmm_sib_idx:
        movzx ecx, byte [sib_idx]
        cmp ecx, 4
        je .prmm_sib_post  ; no index
        cmp byte [sib_printed], 0
        je .prmm_sib_idx_bare
        mov al, '+'
        call putchar
.prmm_sib_idx_bare:
        movzx eax, byte [sib_idx]
        call print_reg32
        movzx ecx, byte [sib_scale]
        test ecx, ecx
        jz .prmm_sib_post
        mov al, '*'
        call putchar
        mov eax, 1
        shl eax, cl
        add al, '0'
        call putchar
.prmm_sib_post:
        ; No base and no index: need to show disp32 inside
        cmp byte [sib_printed], 0
        jne .prmm_disp
        cmp byte [sib_idx], 4
        jne .prmm_disp
        mov eax, [disp32]
        call print_hex32_0x
        mov al, ']'
        call putchar
        ret

.prmm_disp:
        mov eax, [disp32]
        test eax, eax
        jz .prmm_close
        js .prmm_neg
        mov al, '+'
        call putchar
        mov eax, [disp32]
        call print_hex32_0x
        jmp .prmm_close
.prmm_neg:
        mov al, '-'
        call putchar
        neg eax
        call print_hex32_0x
.prmm_close:
        mov al, ']'
        call putchar
        ret

; ── Register tables ───────────────────────────────────────────────────────────
print_reg32:
        cmp eax, 7
        ja .pr32u
        imul eax, 4
        add eax, reg32_tab
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.pr32u: call prt
        dd str_unk_r
        ret

print_reg16:
        cmp eax, 7
        ja .pr16u
        imul eax, 4
        add eax, reg16_tab
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.pr16u: call prt
        dd str_unk_r
        ret

print_reg8:
        cmp eax, 7
        ja .pr8u
        imul eax, 3
        add eax, reg8_tab
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.pr8u:  call prt
        dd str_unk_r
        ret

print_accum:
        test eax, eax
        jnz .pa32
        call prt
        dd str_al
        ret
.pa32:  xor eax, eax
        call print_reg32
        ret

; Arithmetic mnemonic (0-7)
print_arith_mnem:
        cmp eax, 7
        ja .pau
        imul eax, 8
        add eax, arith_tab
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.pau:   call prt
        dd str_unk
        ret

; Shift mnemonic (0-7)
print_shift_mnem:
        cmp eax, 7
        ja .psu
        imul eax, 8
        add eax, shift_tab
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.psu:   call prt
        dd str_unk
        ret

; Condition code name (0-15)
print_cond:
        cmp eax, 15
        ja .pcu
        imul eax, 4
        add eax, cond_tab
        mov ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        ret
.pcu:   call prt
        dd str_unk
        ret

; ── Immediate readers ─────────────────────────────────────────────────────────
read_imm32:
        cmp esi, [file_len]
        jge .ri32_eof
        mov eax, [file_buf + esi]
        add esi, 4
        ret
.ri32_eof: xor eax, eax
        ret

rd_imm32_raw:
        cmp esi, [file_len]
        jge .r32r_eof
        mov eax, [file_buf + esi]
        add esi, 4
        ret
.r32r_eof: xor eax, eax
        ret

read_imm16:
        cmp esi, [file_len]
        jge .ri16_eof
        movzx eax, word [file_buf + esi]
        add esi, 2
        ret
.ri16_eof: xor eax, eax
        ret

read_imm8:
        cmp esi, [file_len]
        jge .ri8_eof
        movzx eax, byte [file_buf + esi]
        inc esi
        ret
.ri8_eof: xor eax, eax
        ret

; ── Output utilities ─────────────────────────────────────────────────────────

; putchar — print AL
putchar:
        push eax
        push ebx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ebx
        pop eax
        ret

; prt — print null-terminated string whose address is the dd after the call
;   call prt
;   dd   str_something
; Equivalent to: call [esp] then skip 4 bytes
prt:
        push eax
        push ebx
        push ecx
        mov ecx, [esp + 12]     ; return address = address of dd
        mov ebx, [ecx]          ; dereference the pointer
        add dword [esp + 12], 4 ; skip over the dd
        mov eax, SYS_PRINT
        int 0x80
        pop ecx
        pop ebx
        pop eax
        ret

print_comma:
        push eax
        push ebx
        mov eax, SYS_PRINT
        mov ebx, str_comma
        int 0x80
        pop ebx
        pop eax
        ret

print_hex32_0x:
        push eax
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        pop eax
        ; fall through
print_hex32:
        push eax
        push ecx
        mov ecx, 8
.ph32:
        rol eax, 4
        push eax
        and eax, 0x0F
        cmp eax, 10
        jl .ph32d
        add eax, 'A'-10
        jmp .ph32p
.ph32d: add eax, '0'
.ph32p: call putchar
        pop eax
        dec ecx
        jnz .ph32
        pop ecx
        pop eax
        ret

print_hex16_0x:
        push eax
        mov al, '0'
        call putchar
        mov al, 'x'
        call putchar
        pop eax
        and eax, 0xFFFF
        push ecx
        mov ecx, 4
.ph16:
        rol eax, 4
        push eax
        and eax, 0x0F
        cmp eax, 10
        jl .ph16d
        add eax, 'A'-10
        jmp .ph16p
.ph16d: add eax, '0'
.ph16p: call putchar
        pop eax
        dec ecx
        jnz .ph16
        pop ecx
        pop eax
        ret

print_hex8:
        push eax
        push ecx
        and eax, 0xFF
        mov ecx, eax
        shr ecx, 4
        cmp ecx, 10
        jl .ph8h
        add ecx, 'A'-10
        jmp .ph8hp
.ph8h:  add ecx, '0'
.ph8hp: push eax
        mov eax, ecx
        call putchar
        pop eax
        and eax, 0xF
        cmp eax, 10
        jl .ph8l
        add eax, 'A'-10
        jmp .ph8lp
.ph8l:  add eax, '0'
.ph8lp: call putchar
        pop ecx
        pop eax
        ret

skip_spaces:
        cmp byte [esi], ' '
        je .ss
        cmp byte [esi], 0x09
        je .ss
        ret
.ss:    inc esi
        jmp skip_spaces

; ── Data ─────────────────────────────────────────────────────────────────────

str_nop:    db "nop", 0
str_ret:    db "ret", 0
str_retf:   db "retf", 0
str_hlt:    db "hlt", 0
str_cli:    db "cli", 0
str_sti:    db "sti", 0
str_cld:    db "cld", 0
str_std:    db "std", 0
str_cmc:    db "cmc", 0
str_pusha:  db "pusha", 0
str_popa:   db "popa", 0
str_pushf:  db "pushf", 0
str_popf:   db "popf", 0
str_sahf:   db "sahf", 0
str_lahf:   db "lahf", 0
str_cwde:   db "cwde", 0
str_cdq:    db "cdq", 0
str_xlat:   db "xlat", 0
str_int3:   db "int3", 0
str_into:   db "into", 0
str_iret:   db "iret", 0
str_leave:  db "leave", 0
str_cpuid:  db "cpuid", 0
str_rdtsc:  db "rdtsc", 0
str_loopne: db "loopne ", 0
str_loope:  db "loope ", 0
str_loop:   db "loop ", 0
str_jcxz:   db "jcxz ", 0
str_enter:  db "enter ", 0

str_push:   db "push  ", 0
str_pop:    db "pop   ", 0
str_call:   db "call  ", 0
str_jmp:    db "jmp   ", 0
str_j:      db "j", 0
str_int:    db "int   ", 0
str_mov:    db "mov   ", 0
str_lea:    db "lea   ", 0
str_xchg:   db "xchg  ", 0
str_test:   db "test  ", 0
str_inc:    db "inc   ", 0
str_dec:    db "dec   ", 0
str_not:    db "not   ", 0
str_neg:    db "neg   ", 0
str_mul:    db "mul   ", 0
str_imul:   db "imul  ", 0
str_div:    db "div   ", 0
str_idiv:   db "idiv  ", 0
str_in:     db "in    ", 0
str_out:    db "out   ", 0
str_rep:    db "rep ", 0
str_repne:  db "repne ", 0
str_lock:   db "lock ", 0
str_set:    db "set", 0
str_movzx:  db "movzx ", 0
str_movsx:  db "movsx ", 0
str_bsf:    db "bsf   ", 0
str_bsr:    db "bsr   ", 0
str_bswap:  db "bswap ", 0
str_movsb:  db "movsb", 0
str_movsd:  db "movsd", 0
str_cmpsb:  db "cmpsb", 0
str_cmpsd:  db "cmpsd", 0
str_stosb:  db "stosb", 0
str_stosd:  db "stosd", 0
str_lodsb:  db "lodsb", 0
str_lodsd:  db "lodsd", 0
str_scasb:  db "scasb", 0
str_scasd:  db "scasd", 0
str_db:     db "db    0x", 0
str_unk:    db "???", 0
str_unk_r:  db "r?", 0
str_comma:  db ", ", 0
str_al:     db "al", 0
str_cl:     db "cl", 0
str_dx:     db "dx", 0

bt_mnem:  db "bt    ", 0
bts_mnem: db "bts   ", 0
btr_mnem: db "btr   ", 0
btc_mnem: db "btc   ", 0

msg_usage:  db "Usage: dis <file> [hex_offset]", 10, 0
msg_err:    db "dis: cannot read file", 10, 0

; Tables (8 bytes each, null-term, padded with spaces)
arith_tab:
        db "add   ", 0, 0
        db "or    ", 0, 0
        db "adc   ", 0, 0
        db "sbb   ", 0, 0
        db "and   ", 0, 0
        db "sub   ", 0, 0
        db "xor   ", 0, 0
        db "cmp   ", 0, 0

shift_tab:
        db "rol   ", 0, 0
        db "ror   ", 0, 0
        db "rcl   ", 0, 0
        db "rcr   ", 0, 0
        db "shl   ", 0, 0
        db "shr   ", 0, 0
        db "sal   ", 0, 0
        db "sar   ", 0, 0

; Condition code names (4 bytes each)
cond_tab:
        db "o",   0, 0, 0
        db "no",  0, 0
        db "b",   0, 0, 0
        db "ae",  0, 0
        db "e",   0, 0, 0
        db "ne",  0, 0
        db "be",  0, 0
        db "a",   0, 0, 0
        db "s",   0, 0, 0
        db "ns",  0, 0
        db "p",   0, 0, 0
        db "np",  0, 0
        db "l",   0, 0, 0
        db "ge",  0, 0
        db "le",  0, 0
        db "g",   0, 0, 0

; Register tables (4 bytes each, null-terminated)
reg32_tab:
        db "eax", 0
        db "ecx", 0
        db "edx", 0
        db "ebx", 0
        db "esp", 0
        db "ebp", 0
        db "esi", 0
        db "edi", 0

reg16_tab:
        db "ax",  0, 0
        db "cx",  0, 0
        db "dx",  0, 0
        db "bx",  0, 0
        db "sp",  0, 0
        db "bp",  0, 0
        db "si",  0, 0
        db "di",  0, 0

reg8_tab:
        db "al", 0
        db "cl", 0
        db "dl", 0
        db "bl", 0
        db "ah", 0
        db "ch", 0
        db "dh", 0
        db "bh", 0

; Variables
filename:    times 256 db 0
arg_buf:     times 256 db 0
start_offset: dd 0
file_len:    dd 0
cur_op:      db 0
tmp_r:       db 0
tmp_imm:     dd 0

modrm_mod:   db 0
modrm_reg:   db 0
modrm_rm:    db 0
has_sib:     db 0
sib_scale:   db 0
sib_idx:     db 0
sib_base:    db 0
sib_printed: db 0
disp32:      dd 0

pfx_rep:     db 0
pfx_lock:    db 0
pfx_opsz:    db 0

arith_op:    db 0
arith_enc:   db 0
grp1_sz:     db 0

file_buf:    times BUF_SIZE db 0

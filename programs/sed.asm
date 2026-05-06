; sed.asm - Stream editor for Mellivora OS
; Usage: sed [-n] 's/FIND/REPL/[g]' [file]
;   Also: sed [-n] [-e 'script'] [file]
; Commands: s/P/R/[g]  p  d  q  =  y/src/dst/
; Addresses: linenum  $  /regex/  addr1,addr2
%include "syscalls.inc"

MAX_SCRIPT   equ 4096
MAX_LINE     equ 2048
MAX_FILE     equ 131072

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        je .usage

        mov esi, arg_buf
        mov byte [silent_flag], 0
        mov byte [subst_delim], 0

.parse_args:
        call .skip_sp
        cmp byte [esi], 0
        je .args_done
        cmp byte [esi], '-'
        jne .not_flag
        inc esi
        mov al, [esi]
        inc esi
        cmp al, 'n'
        jne .chk_e
        mov byte [silent_flag], 1
        jmp .parse_args
.chk_e:
        cmp al, 'e'
        jne .parse_args
        call .skip_sp
        call .append_script
        jmp .parse_args

.not_flag:
        cmp dword [script_len], 0
        jne .is_filename
        call .append_script
        jmp .parse_args
.is_filename:
        mov edi, filename
.copy_word_loop:
        mov al, [esi]
        cmp al, 0
        je .cw_end
        cmp al, ' '
        je .cw_end
        stosb
        inc esi
        jmp .copy_word_loop
.cw_end:
        mov byte [edi], 0
        jmp .parse_args

.args_done:
        cmp dword [script_len], 0
        je .usage

        cmp byte [filename], 0
        je .read_stdin
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .not_found
        mov [file_size], eax
        jmp .run

.read_stdin:
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        mov ecx, MAX_FILE - 1
        int 0x80
        cmp eax, 0
        jle .run
        mov [file_size], eax

.run:
        mov eax, [file_size]
        mov byte [file_buf + eax], 0
        mov dword [line_num], 0
        mov dword [file_pos], 0

.next_line:
        mov eax, [file_pos]
        cmp byte [file_buf + eax], 0
        je .done

        inc dword [line_num]
        ; Extract line
        mov esi, file_buf
        add esi, eax
        mov edi, line_buf
        xor ecx, ecx
.lc:
        mov al, [esi]
        cmp al, 0
        je .le_eof
        cmp al, 0x0A
        je .le_nl
        stosb
        inc ecx
        inc esi
        jmp .lc
.le_nl:
        inc esi
.le_eof:
        mov byte [edi], 0
        ; Update file_pos
        mov eax, esi
        sub eax, file_buf
        mov [file_pos], eax

        mov byte [delete_flag], 0
        mov byte [quit_flag],   0
        mov byte [subst_delim], 0

        ; Run script
        mov esi, script_buf
.run_cmd:
        call .skip_sp_nl
        cmp byte [esi], 0
        je .cmd_done

        ; Save script pointer for after addr check
        mov [saved_script_esi], esi

        call .parse_addr
        call .skip_sp

        call .addr_match
        jnc .skip_this_cmd

        mov al, [esi]
        inc esi
        cmp al, 's'
        je .do_s
        cmp al, 'p'
        je .do_p
        cmp al, 'd'
        je .do_d
        cmp al, 'q'
        je .do_q
        cmp al, '='
        je .do_eq
        cmp al, 'y'
        je .do_y
        call .skip_to_eol_sub
        jmp .run_cmd

.skip_this_cmd:
        call .skip_to_eol_sub
        jmp .run_cmd

.do_s:
        call .subst_run
        jmp .run_cmd
.do_p:
        call .print_line
        jmp .run_cmd
.do_d:
        mov byte [delete_flag], 1
        jmp .cmd_done
.do_q:
        mov byte [quit_flag], 1
        jmp .cmd_done
.do_eq:
        mov eax, [line_num]
        call .print_decimal
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        jmp .run_cmd
.do_y:
        call .y_run
        jmp .run_cmd

.cmd_done:
        cmp byte [silent_flag], 1
        je .no_auto
        cmp byte [delete_flag], 1
        je .no_auto
        call .print_line
.no_auto:
        cmp byte [quit_flag], 1
        je .done
        jmp .next_line

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ---- Helpers ----
.skip_sp:
        cmp byte [esi], ' '
        jne .ssp_r
        inc esi
        jmp .skip_sp
.ssp_r: ret

.skip_sp_nl:
        mov al, [esi]
        cmp al, ' '
        je .sspn_s
        cmp al, 0x0A
        je .sspn_s
        ret
.sspn_s: inc esi
jmp .skip_sp_nl

.skip_to_eol_sub:
        mov al, [esi]
        cmp al, 0
        je .ste_r
        cmp al, 0x0A
        je .ste_nl
        inc esi
        jmp .skip_to_eol_sub
.ste_nl: inc esi
.ste_r:  ret

.append_script:
        ; Append ESI-pointed word/token (space-delimited) to script_buf
        mov edi, script_buf
        mov eax, [script_len]
        add edi, eax
.as_lp:
        mov al, [esi]
        cmp al, 0
        je .as_done
        cmp al, ' '
        je .as_done
        stosb
        inc esi
        inc dword [script_len]
        jmp .as_lp
.as_done:
        ; Append newline
        mov eax, [script_len]
        mov byte [script_buf + eax], 0x0A
        inc dword [script_len]
        mov byte [edi], 0
        ret

; Parse address: sets addr_type, addr1, addr2
.parse_addr:
        mov byte [addr_type], 0
        mov al, [esi]
        cmp al, '$'
        je .pa_last
        cmp al, '/'
        je .pa_regex
        cmp al, '0'
        jb .pa_done
        cmp al, '9'
        ja .pa_done
        call .parse_dec
        mov [addr1], eax
        mov byte [addr_type], 1
        cmp byte [esi], ','
        jne .pa_done
        inc esi
        mov al, [esi]
        cmp al, '$'
        je .pa_rl
        cmp al, '0'
        jb .pa_done
        cmp al, '9'
        ja .pa_done
        call .parse_dec
        mov [addr2], eax
        mov byte [addr_type], 4
        ret
.pa_rl: inc esi
        mov dword [addr2], 0x7FFFFFFF
        mov byte [addr_type], 4
        ret
.pa_last:
        inc esi
        mov byte [addr_type], 2
        ret
.pa_regex:
        inc esi
        mov edi, addr_regex
.par_lp:
        mov al, [esi]
        cmp al, '/'
        je .par_done
        cmp al, 0
        je .par_done
        cmp al, 0x0A
        je .par_done
        stosb
        inc esi
        jmp .par_lp
.par_done:
        mov byte [edi], 0
        cmp byte [esi], '/'
        jne .pa_done
        inc esi
        mov byte [addr_type], 3
.pa_done: ret

; Addr match: CF=1 if current line matches address
.addr_match:
        movzx eax, byte [addr_type]
        cmp al, 0
        je .am_y           ; no addr → always match
        cmp al, 1
        je .am_line
        cmp al, 2
        je .am_last
        cmp al, 3
        je .am_regex
        ; range
        mov eax, [line_num]
        cmp eax, [addr1]
        jb .am_n
        cmp eax, [addr2]
        ja .am_n
.am_y:  stc
ret
.am_n:  clc
ret
.am_line:
        mov eax, [line_num]
        cmp eax, [addr1]
        je .am_y
        jmp .am_n
.am_last:
        mov eax, [file_pos]
        cmp byte [file_buf + eax], 0
        je .am_y
        jmp .am_n
.am_regex:
        mov esi, line_buf
        mov edi, addr_regex
        call .strstr
        test eax, eax
        mov esi, [saved_script_esi]
        ; restore esi (parse_addr may have advanced it past the /)
        jnz .am_y
        jmp .am_n

; s/find/repl/[g] — ESI points to delimiter
.subst_run:
        ; read delimiter
        mov al, [esi]
        mov [subst_delim], al
        inc esi
        ; read find pattern
        mov edi, subst_find
        mov dword [subst_find_len], 0
.sr_find:
        mov al, [esi]
        cmp al, [subst_delim]
        je .sr_find_done
        cmp al, 0
        je .sr_find_done
        cmp al, 0x0A
        je .sr_find_done
        stosb
        inc esi
        inc dword [subst_find_len]
        jmp .sr_find
.sr_find_done:
        mov byte [edi], 0
        cmp al, [subst_delim]
        jne .sr_no_repl
        inc esi
.sr_no_repl:
        ; read replace string
        mov edi, subst_repl
.sr_repl:
        mov al, [esi]
        cmp al, [subst_delim]
        je .sr_repl_done
        cmp al, 0
        je .sr_repl_done
        cmp al, 0x0A
        je .sr_repl_done
        stosb
        inc esi
        jmp .sr_repl
.sr_repl_done:
        mov byte [edi], 0
        cmp al, [subst_delim]
        jne .sr_no_flags
        inc esi
.sr_no_flags:
        ; read flags (g)
        mov byte [subst_global], 0
        cmp byte [esi], 'g'
        jne .sr_do
        mov byte [subst_global], 1
        inc esi
.sr_do:
        ; skip rest of command line
        call .skip_to_eol_sub
        dec esi   ; we'll let run_cmd re-advance past newline naturally

        ; Now perform substitution on line_buf → subst_tmp
        push esi
        mov esi, line_buf
        mov edi, subst_tmp
.srd_lp:
        cmp byte [esi], 0
        je .srd_end_src
        push esi
        push edi
        mov edi, subst_find
        call .match_here
        pop edi
        jc .srd_matched
        pop esi
        movsb
        jmp .srd_lp
.srd_matched:
        pop esi
        ; advance esi past match
        add esi, [subst_find_len]
        ; copy replacement
        push esi
        mov esi, subst_repl
.srd_cr:
        lodsb
        test al, al
        jz .srd_cr_done
        stosb
        jmp .srd_cr
.srd_cr_done:
        pop esi
        cmp byte [subst_global], 1
        je .srd_lp
        ; copy rest
.srd_rest:
        lodsb
        stosb
        test al, al
        jnz .srd_rest
        jmp .srd_done
.srd_end_src:
        mov byte [edi], 0
.srd_done:
        ; copy subst_tmp → line_buf
        mov esi, subst_tmp
        mov edi, line_buf
.srd_cpback:
        lodsb
        stosb
        test al, al
        jnz .srd_cpback
        pop esi
        ret

; y/src/dst/ transliteration on line_buf
.y_run:
        mov al, [esi]
        mov [y_delim], al
        inc esi
        mov edi, y_src
.yr_s:
        mov al, [esi]
        cmp al, [y_delim]
        je .yr_sd
        cmp al, 0
        je .yr_sd
        stosb
        inc esi
        jmp .yr_s
.yr_sd:
        mov byte [edi], 0
        cmp al, [y_delim]
        jne .yr_no_dst
        inc esi
.yr_no_dst:
        mov edi, y_dst
.yr_d:
        mov al, [esi]
        cmp al, [y_delim]
        je .yr_dd
        cmp al, 0
        je .yr_dd
        stosb
        inc esi
        jmp .yr_d
.yr_dd:
        mov byte [edi], 0
        cmp al, [y_delim]
        jne .yr_do
        inc esi
.yr_do:
        call .skip_to_eol_sub
        dec esi

        push esi
        mov esi, line_buf
        mov edi, subst_tmp
.ytr:
        lodsb
        test al, al
        jz .ytr_done
        ; search al in y_src
        push esi
        push edi
        push eax
        mov esi, y_src
        xor ecx, ecx
.yts:
        mov dl, [esi + ecx]
        test dl, dl
        jz .yts_no
        cmp al, dl
        je .yts_found
        inc ecx
        jmp .yts
.yts_found:
        mov dl, [y_dst + ecx]
        test dl, dl
        jz .yts_no
        mov al, dl
.yts_no:
        pop eax
        pop edi
        pop esi
        stosb
        jmp .ytr
.ytr_done:
        mov byte [edi], 0
        mov esi, subst_tmp
        mov edi, line_buf
.ycp:   lodsb
stosb
test al, al
jnz .ycp
        pop esi
        ret

; print line_buf + newline
.print_line:
        mov eax, SYS_PRINT
        mov ebx, line_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        ret

; print EAX as decimal
.print_decimal:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        mov edi, dec_buf + 15
        mov byte [edi], 0
        mov ecx, 10
.pdi:   xor edx, edx
div ecx
        add dl, '0'
        dec edi
        mov [edi], dl
        test eax, eax
        jnz .pdi
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; parse decimal from ESI → EAX
.parse_dec:
        xor eax, eax
.pdd:   movzx ecx, byte [esi]
        cmp cl, '0'
        jb .pdd_r
        cmp cl, '9'
        ja .pdd_r
        imul eax, 10
        sub cl, '0'
        add eax, ecx
        inc esi
        jmp .pdd
.pdd_r: ret

; strstr: ESI=haystack EDI=needle → EAX=1 found 0 not
.strstr:
        push esi
        push edi
        push ecx
        push ebx
.sso:
        mov al, [esi]
        test al, al
        jz .ssn
        push esi
        push edi
.ssm:
        mov cl, [edi]
        test cl, cl
        jz .ssf
        mov bl, [esi]
        test bl, bl
        jz .ssno
        cmp bl, cl
        jne .ssno
        inc esi
        inc edi
        jmp .ssm
.ssf:   pop edi
pop esi
        pop ebx
        pop ecx
        pop edi
        pop esi
        mov eax, 1
        ret
.ssno:  pop edi
pop esi
        inc esi
        jmp .sso
.ssn:   pop ebx
pop ecx
pop edi
pop esi
        xor eax, eax
        ret

; match_here: needle EDI at position ESI; CF=1=match
.match_here:
        push esi
        push edi
.mhl:
        mov al, [edi]
        test al, al
        jz .mhy
        mov cl, [esi]
        cmp al, cl
        jne .mhn
        inc esi
        inc edi
        jmp .mhl
.mhy:   pop edi
pop esi
stc
ret
.mhn:   pop edi
pop esi
clc
ret

;--- Data ---
msg_usage:    db "Usage: sed [-n] 's/find/repl/[g]' [file]",0x0A,0
msg_not_found: db "sed: file not found",0x0A,0
dec_buf:      times 16 db 0

;--- BSS ---
arg_buf:          times 512      db 0
script_buf:       times MAX_SCRIPT db 0
script_len:       dd 0
filename:         times 256      db 0
file_buf:         times MAX_FILE db 0
file_size:        dd 0
file_pos:         dd 0
silent_flag:      db 0
line_buf:         times MAX_LINE db 0
line_num:         dd 0
delete_flag:      db 0
quit_flag:        db 0
addr_type:        db 0
addr1:            dd 0
addr2:            dd 0
addr_regex:       times 256      db 0
subst_delim:      db 0
subst_find:       times 256      db 0
subst_find_len:   dd 0
subst_repl:       times 256      db 0
subst_global:     db 0
subst_tmp:        times MAX_LINE db 0
y_delim:          db 0
y_src:            times 256      db 0
y_dst:            times 256      db 0
saved_script_esi: dd 0

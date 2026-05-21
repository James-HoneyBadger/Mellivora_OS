; access.asm — test file accessibility using SYS_ACCESS
;
; SYS_ACCESS was fixed in v10 to correctly load the root directory before
; searching, read the mode argument from the right pushad stack offset,
; and query the real UID/GID of the calling task instead of hardcoding 0.
;
; Usage: access [-f] [-r] [-w] [-x] <path>
;
;   -f   existence check (default when no flags are given)
;   -r   read permission
;   -w   write permission
;   -x   execute permission
;
; Multiple flags may be combined: access -rw myfile.bin
; Exits 0 if every requested check passes, 1 if any check fails.

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        test eax, eax
        jle .usage

        mov esi, argbuf
        call skip_ws
        cmp byte [esi], 0
        je .usage

        xor ecx, ecx            ; accumulated ACC_* mode bits

; ---- Parse flags and filename -----------------------------------
.next_arg:
        call skip_ws
        cmp byte [esi], 0
        je .args_done
        cmp byte [esi], '-'
        jne .is_path

        inc esi                 ; skip '-'
.flag_chars:
        mov al, [esi]
        cmp al, 0
        je .end_flag
        cmp al, ' '
        je .end_flag
        cmp al, 9
        je .end_flag
        cmp al, 'f'
        je .fl_f
        cmp al, 'r'
        je .fl_r
        cmp al, 'w'
        je .fl_w
        cmp al, 'x'
        je .fl_x
        jmp .usage              ; unknown flag
.fl_f:  mov byte [do_fok], 1
        inc esi
        jmp .flag_chars
.fl_r:  or ecx, ACC_R_OK
        inc esi
        jmp .flag_chars
.fl_w:  or ecx, ACC_W_OK
        inc esi
        jmp .flag_chars
.fl_x:  or ecx, ACC_X_OK
        inc esi
        jmp .flag_chars
.end_flag:
        jmp .next_arg

.is_path:
        ; Everything from here to end-of-string is the path
        mov edi, pathbuf
        xor edx, edx
.cp:
        mov al, [esi]
        cmp al, 0
        je .cp_done
        cmp al, 10
        je .cp_done
        mov [edi + edx], al
        inc edx
        inc esi
        jmp .cp
.cp_done:
        mov byte [edi + edx], 0
        jmp .next_arg

.args_done:
        cmp byte [pathbuf], 0
        je .usage

        mov [mode_bits], ecx

        ; Default to -f if no flag was given at all
        test ecx, ecx
        jnz .have_flags
        cmp byte [do_fok], 1
        je .have_flags
        mov byte [do_fok], 1
.have_flags:

        ; Print header
        mov eax, SYS_PRINT
        mov ebx, msg_checking
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, pathbuf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        xor edi, edi            ; result accumulator (0 = all pass)

; ---- F_OK: existence --------------------------------------------
        cmp byte [do_fok], 1
        jne .skip_fok

        mov eax, SYS_ACCESS
        mov ebx, pathbuf
        xor ecx, ecx            ; ACC_F_OK = 0
        int 0x80
        test eax, eax
        jz .fok_ok

        mov eax, SYS_PRINT
        mov ebx, msg_no_exist
        int 0x80
        mov edi, 1
        jmp .done               ; no point testing permissions of a missing file
.fok_ok:
        mov eax, SYS_PRINT
        mov ebx, msg_exists
        int 0x80
.skip_fok:

; ---- R_OK -------------------------------------------------------
        mov eax, [mode_bits]
        test eax, ACC_R_OK
        jz .skip_rok

        mov eax, SYS_ACCESS
        mov ebx, pathbuf
        mov ecx, ACC_R_OK
        int 0x80
        test eax, eax
        jz .rok_ok
        mov eax, SYS_PRINT
        mov ebx, msg_no_read
        int 0x80
        mov edi, 1
        jmp .skip_rok
.rok_ok:
        mov eax, SYS_PRINT
        mov ebx, msg_readable
        int 0x80
.skip_rok:

; ---- W_OK -------------------------------------------------------
        mov eax, [mode_bits]
        test eax, ACC_W_OK
        jz .skip_wok

        mov eax, SYS_ACCESS
        mov ebx, pathbuf
        mov ecx, ACC_W_OK
        int 0x80
        test eax, eax
        jz .wok_ok
        mov eax, SYS_PRINT
        mov ebx, msg_no_write
        int 0x80
        mov edi, 1
        jmp .skip_wok
.wok_ok:
        mov eax, SYS_PRINT
        mov ebx, msg_writable
        int 0x80
.skip_wok:

; ---- X_OK -------------------------------------------------------
        mov eax, [mode_bits]
        test eax, ACC_X_OK
        jz .skip_xok

        mov eax, SYS_ACCESS
        mov ebx, pathbuf
        mov ecx, ACC_X_OK
        int 0x80
        test eax, eax
        jz .xok_ok
        mov eax, SYS_PRINT
        mov ebx, msg_no_exec
        int 0x80
        mov edi, 1
        jmp .skip_xok
.xok_ok:
        mov eax, SYS_PRINT
        mov ebx, msg_executable
        int 0x80
.skip_xok:

.done:
        mov eax, SYS_EXIT
        mov ebx, edi            ; 0 = all pass, 1 = at least one fail
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ---- Helpers ----------------------------------------------------
skip_ws:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_ws

; ---- Data -------------------------------------------------------
msg_usage:
        db "Usage: access [-f] [-r] [-w] [-x] <path>", 10
        db "  -f  existence   -r  readable   -w  writable   -x  executable", 10, 0
msg_checking:   db "Checking: ", 0
msg_exists:     db "  [OK]   exists", 10, 0
msg_no_exist:   db "  [FAIL] does not exist", 10, 0
msg_readable:   db "  [OK]   readable", 10, 0
msg_no_read:    db "  [FAIL] not readable", 10, 0
msg_writable:   db "  [OK]   writable", 10, 0
msg_no_write:   db "  [FAIL] not writable", 10, 0
msg_executable: db "  [OK]   executable", 10, 0
msg_no_exec:    db "  [FAIL] not executable", 10, 0

mode_bits:      dd 0
do_fok:         db 0

section .bss
argbuf:         resb 512
pathbuf:        resb 256

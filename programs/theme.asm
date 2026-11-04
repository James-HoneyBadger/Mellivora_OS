; theme.asm - Switch the system color theme
; Suggestion #12: System polish. Sets the default text-mode attribute and
; clears the screen. Persists choice in /home/.theme so other apps can read.
;
; Usage:
;   theme              show current theme + list themes
;   theme <name>       apply a named theme

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        test eax, eax
        jle .show

        ; Trim trailing whitespace
        mov esi, argbuf
.tr:
        mov al, [esi]
        test al, al
        jz .lookup
        cmp al, ' '
        je .cut
        cmp al, 9
        je .cut
        cmp al, 10
        je .cut
        cmp al, 13
        je .cut
        inc esi
        jmp .tr
.cut:
        mov byte [esi], 0
.lookup:
        ; Walk theme table; entry = (dd name, dd attr)
        mov ebx, themes
.find:
        mov esi, [ebx]
        test esi, esi
        jz .unknown
        mov edi, argbuf
        call streq_ci
        je .apply
        add ebx, 8
        jmp .find

.apply:
        mov eax, [ebx + 4]
        mov [chosen], eax
        mov eax, SYS_SETCOLOR
        mov ebx, [chosen]
        or ebx, SETCOLOR_PERSIST        ; the point of `theme` is to stay
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        ; Persist
        mov edi, persist_buf
        mov esi, argbuf
.cp:
        lodsb
        test al, al
        jz .cd
        stosb
        jmp .cp
.cd:
        mov al, 10
        stosb
        mov ecx, edi
        sub ecx, persist_buf
        mov eax, SYS_FWRITE
        mov ebx, theme_path
        mov edx, ecx
        mov ecx, persist_buf
        xor esi, esi
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_set
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, argbuf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        xor ebx, ebx
        jmp .exit

.show:
        ; Read currently saved theme (if any)
        mov eax, SYS_FREAD
        mov ebx, theme_path
        mov ecx, persist_buf
        int 0x80
        cmp eax, 0
        jle .no_saved
        mov eax, SYS_PRINT
        mov ebx, msg_cur
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, persist_buf
        int 0x80
        jmp .list
.no_saved:
        mov eax, SYS_PRINT
        mov ebx, msg_none
        int 0x80
.list:
        mov eax, SYS_PRINT
        mov ebx, msg_avail
        int 0x80
        mov ebx, themes
.lloop:
        mov esi, [ebx]
        test esi, esi
        jz .ldone
        push ebx
        mov eax, SYS_PRINT
        mov ebx, indent
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop ebx
        add ebx, 8
        jmp .lloop
.ldone:
        xor ebx, ebx
        jmp .exit

.unknown:
        mov eax, SYS_PRINT
        mov ebx, msg_unk
        int 0x80
        mov ebx, 1
.exit:
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------
streq_ci:
        push esi
        push edi
.l:
        mov al, [esi]
        mov ah, [edi]
        cmp al, 'A'
        jb .a1
        cmp al, 'Z'
        ja .a1
        add al, 32
.a1:
        cmp ah, 'A'
        jb .a2
        cmp ah, 'Z'
        ja .a2
        add ah, 32
.a2:
        cmp al, ah
        jne .ne
        test al, al
        jz .eq
        inc esi
        inc edi
        jmp .l
.eq:
        pop edi
        pop esi
        xor al, al
        ret
.ne:
        pop edi
        pop esi
        or al, 1
        cmp al, 0
        ret

;-----------------------------------
themes:
        dd t_classic, 0x07
        dd t_amber,   0x06
        dd t_green,   0x02
        dd t_cyan,    0x0B
        dd t_pink,    0x0D
        dd t_inverse, 0x70
        dd t_solar,   0x4E
        dd t_matrix,  0x0A
        dd 0, 0

t_classic: db 'classic', 0
t_amber:   db 'amber', 0
t_green:   db 'green', 0
t_cyan:    db 'cyan', 0
t_pink:    db 'pink', 0
t_inverse: db 'inverse', 0
t_solar:   db 'solar', 0
t_matrix:  db 'matrix', 0

theme_path: db '/home/.theme', 0
indent:     db '  ', 0
msg_set:    db 'theme set: ', 0
msg_cur:    db 'current theme: ', 0
msg_none:   db 'no theme saved (default applied).', 10, 0
msg_avail:  db 'available themes:', 10, 0
msg_unk:    db 'theme: unknown name. Try "theme" with no args.', 10, 0

argbuf:      times 64 db 0
chosen:      dd 0
persist_buf: times 80 db 0

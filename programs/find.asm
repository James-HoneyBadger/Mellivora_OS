; find.asm - Simple filename search [HBU]
; Usage: find [-name PATTERN]
; Lists files in current directory, optionally filtered by pattern
; Pattern supports * wildcard (glob-style)
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80

        ; Default: no pattern filter
        mov dword [pattern], 0

        ; Parse -name PATTERN
        mov esi, args_buf
        call skip_spaces
        cmp byte [esi], 0
        je .list_all

        ; Check for -name
        cmp byte [esi], '-'
        jne .usage
        cmp byte [esi+1], 'n'
        jne .usage
        cmp byte [esi+2], 'a'
        jne .usage
        cmp byte [esi+3], 'm'
        jne .usage
        cmp byte [esi+4], 'e'
        jne .usage
        add esi, 5
        call skip_spaces
        cmp byte [esi], 0
        je .usage
        mov [pattern], esi
        ; Null-terminate pattern at next space
.term_pat:
        cmp byte [esi], 0
        je .list_all
        cmp byte [esi], ' '
        je .term_pat_done
        inc esi
        jmp .term_pat
.term_pat_done:
        mov byte [esi], 0

.list_all:
        ; Iterate directory entries starting at index 0
        mov dword [dir_index], 0

.dir_loop:
        mov eax, SYS_READDIR
        mov ebx, name_buf
        mov ecx, [dir_index]
        int 0x80

        ; EAX = type: -1 = end, 0 = free/empty slot
        cmp eax, -1
        je .done

        cmp eax, 0
        je .next_entry          ; skip free slots

        ; We have a valid entry in name_buf
        ; Check pattern if set
        cmp dword [pattern], 0
        je .print_entry

        ; Match name_buf against [pattern]
        push esi
        push edi
        mov esi, [pattern]
        mov edi, name_buf
        call glob_match
        pop edi
        pop esi
        cmp eax, 1
        jne .next_entry

.print_entry:
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.next_entry:
        inc dword [dir_index]
        jmp .dir_loop

.done:
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;-----------------------------------------------
; skip_spaces: advance ESI past spaces and tabs
;-----------------------------------------------
skip_spaces:
.loop:
        cmp byte [esi], ' '
        je .skip
        cmp byte [esi], 9
        je .skip
        ret
.skip:
        inc esi
        jmp .loop

;-----------------------------------------------
; glob_match: match string EDI against pattern ESI
; Supports * wildcard (matches zero or more chars)
; Returns EAX=1 if match, 0 if no match
; Uses iterative backtracking (no recursion)
;-----------------------------------------------
glob_match:
        push ebx
        push ecx
        push edx
        push esi
        push edi

        ; Save backtrack points
        mov dword [gm_star_pat], 0    ; no star seen yet
        mov dword [gm_star_str], 0

.gm_loop:
        mov al, [esi]           ; pattern char
        mov bl, [edi]           ; string char

        cmp al, '*'
        je .gm_star

        cmp al, 0
        jne .gm_literal
        ; Pattern ended
        cmp bl, 0
        je .gm_yes             ; both ended = match
        jmp .gm_backtrack

.gm_literal:
        cmp bl, 0
        je .gm_backtrack       ; string ended but pattern hasn't
        cmp al, bl
        jne .gm_backtrack
        inc esi
        inc edi
        jmp .gm_loop

.gm_star:
        inc esi                 ; consume *
        mov [gm_star_pat], esi ; pattern position after *
        mov [gm_star_str], edi ; string position for retry
        jmp .gm_loop           ; try matching * with zero chars

.gm_backtrack:
        cmp dword [gm_star_pat], 0
        je .gm_no              ; no star to backtrack to

        ; Retry: advance the string position by 1
        mov edi, [gm_star_str]
        cmp byte [edi], 0
        je .gm_no              ; string exhausted
        inc edi
        mov [gm_star_str], edi
        mov esi, [gm_star_pat]
        jmp .gm_loop

.gm_yes:
        mov eax, 1
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.gm_no:
        xor eax, eax
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

msg_usage:      db "Usage: find [-name PATTERN]", 10, 0

section .bss
args_buf:       resb 256
pattern:        resd 1
dir_index:      resd 1
name_buf:       resb 64
gm_star_pat:    resd 1
gm_star_str:    resd 1

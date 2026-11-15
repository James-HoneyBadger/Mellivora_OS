; sett_state_test.asm - say whether a transaction landed whole or not at all
;
; A configuration transaction touches several values and commits once. Killed at
; any point inside it, the store has to come back wholly old or wholly new -
; never with the first two values of the new set and the last two of the old.
;
; This program is the judge, because it is the only party that knows what the
; values mean. It reads four keys back through the service call and prints one
; word:
;
;   SETT_STATE=old     every key still holds the old generation
;   SETT_STATE=new     every key holds the new generation
;   SETT_STATE=mixed   a mixture, which is the failure the case forbids
;   SETT_STATE=missing a key the seed wrote is gone, which is also a failure
;
; The keys are \Registry\Machine\Crash\K1 .. K4, seeded by the test that runs
; this; each holds the text "old" or "new".
;
; Prints SETT_STATE=... and exits 0 for old and new, 1 for the other two.

%include "syscalls.inc"

KEYS        equ 4

start:
        mov dword [index], 0
        mov dword [seen_old], 0
        mov dword [seen_new], 0

.key_loop:
        mov eax, [index]
        cmp eax, KEYS
        jae .judge
        call build_path
        mov eax, SYS_SETT
        xor ebx, ebx                    ; 0 = get
        mov ecx, path_buf
        mov edx, value_buf
        mov esi, 16
        int 0x80
        cmp eax, 0
        jne .missing

        ; "old" or "new", by its first two characters
        mov esi, value_buf
        cmp byte [esi], 'o'
        jne .maybe_new
        cmp byte [esi + 1], 'l'
        jne .mixed
        inc dword [seen_old]
        jmp .next
.maybe_new:
        cmp byte [esi], 'n'
        jne .mixed
        cmp byte [esi + 1], 'e'
        jne .mixed
        inc dword [seen_new]
.next:
        inc dword [index]
        jmp .key_loop

.judge:
        cmp dword [seen_old], KEYS
        je .say_old
        cmp dword [seen_new], KEYS
        je .say_new
        jmp .mixed
.say_old:
        mov esi, msg_old
        jmp .report
.say_new:
        mov esi, msg_new
        jmp .report
.mixed:
        mov esi, msg_mixed
        jmp .report_fail
.missing:
        mov esi, msg_missing
.report_fail:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80
.report:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

; index -> "\Registry\Machine\Crash\K<n>" at path_buf
build_path:
        pushad
        mov esi, path_prefix
        mov edi, path_buf
.copy:
        mov al, [esi]
        test al, al
        jz .digit
        mov [edi], al
        inc esi
        inc edi
        jmp .copy
.digit:
        mov eax, [index]
        add al, '1'
        mov [edi], al
        mov byte [edi + 1], 0
        popad
        ret

path_prefix: db "\Registry\Machine\Crash\K", 0
msg_old:     db "SETT_STATE=old", 10, 0
msg_new:     db "SETT_STATE=new", 10, 0
msg_mixed:   db "SETT_STATE=mixed", 10, 0
msg_missing: db "SETT_STATE=missing", 10, 0
index:       dd 0
seen_old:    dd 0
seen_new:    dd 0
path_buf:    times 64 db 0
value_buf:   times 16 db 0

; osmoke.asm - first contact with the executive
;
; Not a conformance test: the smallest program that proves the object manager
; answers. It creates a manual-reset event by name, opens it again by the same
; name, sets it through one handle and waits on the other, checks that the
; signal survives one wait and that a reset makes the next wait time out, then
; closes both handles.
;
; Prints OSMOKE_OK or OSMOKE_FAIL naming the step. A dot per check shows how
; far it got.

%include "syscalls.inc"
%include "lib/obj.inc"

start:
        ; 1: create
        mov eax, SYS_OBJECT_CREATE
        mov ebx, name_event
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        mov edi, OCF_EVENT_MANUAL
        int 0x80
        cmp eax, -1
        je .fail1
        mov [h1], eax
        call obj_dot

        ; 2: open it again by name
        mov eax, SYS_OBJECT_OPEN
        mov ebx, name_event
        mov ecx, R_ALL
        int 0x80
        cmp eax, -1
        je .fail2
        mov [h2], eax
        cmp eax, [h1]
        je .fail2                       ; two handles, one object
        call obj_dot

        ; 3: set through h1, wait through h2
        mov eax, SYS_EVENT_SET
        mov ebx, [h1]
        int 0x80
        cmp eax, 0
        jne .fail3
        mov eax, SYS_WAIT_ONE
        mov ebx, [h2]
        xor ecx, ecx
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne .fail3
        call obj_dot

        ; 4: manual reset, so a second poll still succeeds
        mov eax, SYS_WAIT_ONE
        mov ebx, [h2]
        xor ecx, ecx
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne .fail4
        call obj_dot

        ; 5: reset, then a poll must time out
        mov eax, SYS_EVENT_RESET
        mov ebx, [h1]
        int 0x80
        cmp eax, 0
        jne .fail5
        mov eax, SYS_WAIT_ONE
        mov ebx, [h2]
        xor ecx, ecx
        xor edx, edx
        int 0x80
        cmp eax, WAIT_TIMEOUT
        jne .fail5
        call obj_dot

        ; 6: close both
        mov eax, SYS_OBJECT_CLOSE
        mov ebx, [h1]
        int 0x80
        cmp eax, 0
        jne .fail6
        mov eax, SYS_OBJECT_CLOSE
        mov ebx, [h2]
        int 0x80
        cmp eax, 0
        jne .fail6
        call obj_dot

        mov ebx, ok_msg
        jmp .report
.fail1: mov ebx, f1
        jmp .report
.fail2: mov ebx, f2
        jmp .report
.fail3: mov ebx, f3
        jmp .report
.fail4: mov ebx, f4
        jmp .report
.fail5: mov ebx, f5
        jmp .report
.fail6: mov ebx, f6
.report:
        call obj_say
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

name_event: db "\Sessions\0\BaseNamedObjects\SmokeEvent", 0
ok_msg:     db " OSMOKE_OK", 10, 0
f1:         db " OSMOKE_FAIL step1", 10, 0
f2:         db " OSMOKE_FAIL step2", 10, 0
f3:         db " OSMOKE_FAIL step3", 10, 0
f4:         db " OSMOKE_FAIL step4", 10, 0
f5:         db " OSMOKE_FAIL step5", 10, 0
f6:         db " OSMOKE_FAIL step6", 10, 0
h1:         dd 0
h2:         dd 0

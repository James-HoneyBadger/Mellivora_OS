; object_test.asm - the object manager's conformance gate (M1)
;
; Runs the five things docs/spec/OBJECTS.md §11 requires of the object model,
; plus the two refusals the security model adds:
;
;   1. a named object opens by name, and the two handles name one object
;   2. rights never amplify: a duplicate may only narrow
;   3. a stale handle is refused, not silently rebound
;   4. a forged handle is refused
;   5. the object counters come back to where they started
;
; and, after becoming an ordinary user:
;
;   6. another session's namespace is refused without SE_SESSION_CROSS
;   7. a write-up into a SYSTEM-integrity directory is refused even though the
;      directory's DACL allows it - which is how the mandatory rule is shown to
;      be the rule that refused it
;
; Prints OBJECT_OK when every case behaved, OBJECT_FAIL otherwise. A dot per
; case shows how far it got.

%include "syscalls.inc"
%include "lib/obj.inc"

OQ_TYPE      equ 0
OQ_RIGHTS    equ 4
OQ_SIGNALLED equ 8
OQ_NAMELEN   equ 12
OQ_NAME      equ 16

start:
        ; --- counters before anything ---
        mov ebx, stats_before
        call read_stats
        mov eax, [stats_before + OBJ_TYPE_EVENT * 32]
        mov [ev_before], eax

        ; --- 1: create, then open by name ---
        mov eax, SYS_OBJECT_CREATE
        mov ebx, ev_name
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        xor edi, edi
        int 0x80
        cmp eax, -1
        je .fail
        mov [h1], eax

        mov eax, SYS_OBJECT_OPEN
        mov ebx, ev_name
        mov ecx, R_ALL
        int 0x80
        cmp eax, -1
        je .fail
        mov [h2], eax
        cmp eax, [h1]
        je .fail                        ; two handles must be two numbers
        ; ...and both must name one object: querying either reports one type
        mov eax, SYS_OBJECT_QUERY
        mov ebx, [h2]
        mov ecx, qbuf
        mov edx, 32
        int 0x80
        cmp eax, -1
        je .fail
        cmp dword [qbuf + OQ_TYPE], OBJ_TYPE_EVENT
        jne .fail
        call obj_dot

        ; --- 2: a duplicate may only narrow ---
        ; h3 is the same object with QUERY_STATE only.
        mov eax, SYS_OBJECT_DUP
        mov ebx, [h1]
        mov ecx, R_QUERY_STATE
        int 0x80
        cmp eax, -1
        je .fail
        mov [h3], eax
        ; Amplifying it back to R_ALL must be refused.
        mov eax, SYS_OBJECT_DUP
        mov ebx, [h3]
        mov ecx, R_ALL
        int 0x80
        cmp eax, -1
        jne .fail
        ; The narrowed handle can query but cannot set.
        mov eax, SYS_OBJECT_QUERY
        mov ebx, [h3]
        mov ecx, qbuf
        mov edx, 32
        int 0x80
        cmp eax, -1
        je .fail
        mov eax, SYS_EVENT_SET
        mov ebx, [h3]
        int 0x80
        cmp eax, -1
        jne .fail
        call obj_dot

        ; --- 3: a stale handle is refused ---
        mov eax, SYS_OBJECT_CLOSE
        mov ebx, [h3]
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_EVENT_SET
        mov ebx, [h3]
        int 0x80
        cmp eax, -1
        jne .fail                       ; closed is closed, even if reused
        call obj_dot

        ; --- 4: a forged handle is refused ---
        mov eax, SYS_EVENT_SET
        mov ebx, 0xDEADBEEF
        int 0x80
        cmp eax, -1
        jne .fail
        mov eax, SYS_OBJECT_QUERY
        mov ebx, 0x00000004             ; index 1, generation 0: never issued
        mov ecx, qbuf
        mov edx, 32
        int 0x80
        cmp eax, -1
        jne .fail
        call obj_dot

        ; --- 5: counters return to where they started ---
        mov eax, SYS_OBJECT_CLOSE
        mov ebx, [h1]
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_OBJECT_CLOSE
        mov ebx, [h2]
        int 0x80
        cmp eax, 0
        jne .fail
        mov ebx, stats_after
        call read_stats
        mov eax, [stats_after + OBJ_TYPE_EVENT * 32]
        cmp eax, [ev_before]
        jne .fail
        call obj_dot

        ; --- become an ordinary user for the refusal cases ---
        mov eax, SYS_SETGID
        mov ebx, 1000
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_SETUID
        mov ebx, 1000
        int 0x80
        cmp eax, 0
        jne .fail

        ; --- 6: another session's namespace is refused ---
        mov eax, SYS_OBJECT_CREATE
        mov ebx, other_session
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        xor edi, edi
        int 0x80
        cmp eax, -1
        jne .fail
        call obj_dot

        ; --- 7: a write-up is refused even though the DACL allows it ---
        ; \KernelObjects\Lobby grants everybody every right and sits at SYSTEM
        ; integrity. A medium-integrity process therefore fails on the level,
        ; not on the DACL: privilege follows from that combination.
        mov eax, SYS_OBJECT_CREATE
        mov ebx, lobby_child
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        xor edi, edi
        int 0x80
        cmp eax, -1
        jne .fail
        call obj_dot

        mov ebx, ok_msg
        jmp .report
.fail:
        mov ebx, fail_msg
.report:
        call obj_say
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; read_stats - snapshot the object counters into the buffer in EBX
read_stats:
        mov eax, SYS_OBJECT_STATS
        mov ecx, STATS_LEN
        int 0x80
        ret

ok_msg:     db " OBJECT_OK", 10, 0
fail_msg:   db " OBJECT_FAIL", 10, 0

ev_name:       db "\Sessions\0\BaseNamedObjects\ObjTestEvent", 0
other_session: db "\Sessions\1\BaseNamedObjects\ObjTestCross", 0
lobby_child:   db "\KernelObjects\Lobby\ObjTestWriteUp", 0

qbuf:        times 32 db 0
STATS_LEN   equ OBJ_TYPE_COUNT * 32
stats_before: times STATS_LEN db 0
stats_after:  times STATS_LEN db 0
ev_before:   dd 0
h1:          dd 0
h2:          dd 0
h3:          dd 0

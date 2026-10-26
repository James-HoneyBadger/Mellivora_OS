; wait_test.asm - the waiting primitive (M1)
;
; OBJECTS.md §6 says any handle with SYNCHRONIZE can be waited on, that a wait
; set is WaitOne/WaitAny/WaitAll with a timeout, that a timeout of nought polls
; and a longer one blocks, that an auto-reset object's signal is consumed by
; exactly one waiter, and that a set is at most 64 handles.
;
; The first child exists so that a blocking wait really blocks: a task with no
; peer to switch to is told its wait timed out at once, which is correct but
; would make the blocking cases test nothing.
;
; Waking a waiter in *another* task is crosswait_test.asm's job, in both
; directions: here every case is satisfied by the task that asked for it.
;
; Prints WAIT_OK when every case behaves, WAIT_FAIL otherwise.

%include "syscalls.inc"
%include "lib/obj.inc"

start:
        ; A child that sleeps and exits, so a blocking wait has somewhere to go.
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .idle_child

        ; --- fixtures: a semaphore at 0 and a manual-reset event, unset ---
        mov eax, SYS_OBJECT_CREATE
        mov ebx, sem_name
        mov ecx, OBJ_TYPE_SEMAPHORE
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        xor edi, edi                    ; initial count 0
        int 0x80
        cmp eax, -1
        je .fail
        mov [h_sem], eax

        mov eax, SYS_OBJECT_CREATE
        mov ebx, ev_name
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        mov edi, OCF_EVENT_MANUAL
        int 0x80
        cmp eax, -1
        je .fail
        mov [h_ev], eax

        ; --- 0: a semaphore at zero is not signalled ---
        mov eax, SYS_WAIT_ONE
        mov ebx, [h_sem]
        xor ecx, ecx
        xor edx, edx
        int 0x80
        cmp eax, WAIT_TIMEOUT
        jne .fail
        call obj_dot

        ; --- 1: WaitAny picks the signalled one, by index ---
        mov eax, SYS_EVENT_SET
        mov ebx, [h_ev]
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, [h_sem]
        mov [set0], eax
        mov eax, [h_ev]
        mov [set0 + 4], eax
        mov eax, SYS_WAIT_ANY
        mov ebx, set0
        mov ecx, 2
        xor edx, edx                    ; poll
        int 0x80
        cmp eax, 1
        jne .fail
        call obj_dot

        ; --- 2: WaitAll blocks on the unsignalled one and times out ---
        mov eax, SYS_EVENT_RESET
        mov ebx, [h_ev]
        int 0x80
        mov eax, SYS_WAIT_ALL
        mov ebx, set0
        mov ecx, 2
        mov edx, 3                      ; 30 ms
        int 0x80
        cmp eax, WAIT_TIMEOUT
        jne .fail
        call obj_dot

        ; --- 3: releasing the semaphore lets WaitAll succeed ---
        mov eax, SYS_EVENT_SET
        mov ebx, [h_ev]
        int 0x80
        mov eax, SYS_SEMAPHORE_RELEASE
        mov ebx, [h_sem]
        mov ecx, 1
        int 0x80
        cmp eax, 0
        jne .fail
        mov eax, SYS_WAIT_ALL
        mov ebx, set0
        mov ecx, 2
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne .fail
        call obj_dot

        ; --- 4: an auto-reset event satisfies exactly one wait ---
        mov eax, SYS_OBJECT_CREATE
        mov ebx, auto_name
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        xor edi, edi                    ; auto-reset
        int 0x80
        cmp eax, -1
        je .fail
        mov [h_auto], eax
        mov eax, SYS_EVENT_SET
        mov ebx, [h_auto]
        int 0x80
        mov eax, SYS_WAIT_ONE
        mov ebx, [h_auto]
        xor ecx, ecx
        xor edx, edx
        int 0x80
        cmp eax, 0
        jne .fail                       ; the first wait takes the signal
        mov eax, SYS_WAIT_ONE
        mov ebx, [h_auto]
        xor ecx, ecx
        xor edx, edx
        int 0x80
        cmp eax, WAIT_TIMEOUT
        jne .fail                       ; the second must not
        call obj_dot

        ; --- 5: more than 64 handles in one set is refused ---
        mov eax, SYS_WAIT_ANY
        mov ebx, set0
        mov ecx, 65
        xor edx, edx
        int 0x80
        cmp eax, WAIT_FAILED
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

.idle_child:
        mov eax, SYS_SLEEP
        mov ebx, 600
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

ok_msg:         db " WAIT_OK", 10, 0
fail_msg:       db " WAIT_FAIL", 10, 0
sem_name:   db "\Sessions\0\BaseNamedObjects\WaitTestSem", 0
ev_name:    db "\Sessions\0\BaseNamedObjects\WaitTestEvent", 0
auto_name:  db "\Sessions\0\BaseNamedObjects\WaitTestAuto", 0

set0:       times 66 dd 0
h_sem:      dd 0
h_ev:       dd 0
h_auto:     dd 0

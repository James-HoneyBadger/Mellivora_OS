; crosswait_test.asm - waking a waiter that lives in another task (M1)
;
; OBJECTS.md §6 promises that a wait set is completed by whoever signals the
; object, not only by the task that is waiting. Everything else in the waiting
; primitive is covered by wait_test.asm inside a single task; this file is the
; other half, and it is the part the wave left unproven.
;
; Both directions are tested, because they exercise different code:
;   A. the waiter is a child and the parent signals it - the child is blocked
;      when its parent runs, so the signal has to move a task that is not
;      executing.
;   B. the waiter is the parent and a child signals it - the parent blocks, so
;      the task that wakes it is the one that was switched in.
;
; A handle table is per process and a fork starts with an empty one, so the
; child finds the event through the namespace with SYS_OBJECT_OPEN. That is not
; incidental: a wake that only works through an inherited handle would pass a
; weaker test and fail the moment a real program spawned a helper.
;
; Prints XWAIT_OK when both directions complete, XWAIT_FAIL otherwise.

%include "syscalls.inc"
%include "lib/obj.inc"

WAIT_FOREVER    equ 0xFFFFFFFF
CHILD_WAIT      equ 300                 ; 3 s: a failed wake fails the case fast
PARENT_WAIT     equ 300
SETTLE_TICKS    equ 20                  ; 200 ms: the waiter is certainly blocked

start:
        ; --- fixture: one named, manual-reset, initially unset event ---
        mov eax, SYS_OBJECT_CREATE
        mov ebx, ev_name
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        mov edi, OCF_EVENT_MANUAL
        int 0x80
        cmp eax, -1
        je .fail_create
        mov [h_ev], eax

        ; ===============================================================
        ; A. child waits, parent signals
        ; ===============================================================
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail_fork_a
        test eax, eax
        jz .child_waiter

        mov [pid_a], eax
        ; Let the child reach its wait before anything is signalled.
        mov eax, SYS_SLEEP
        mov ebx, SETTLE_TICKS
        int 0x80

        mov eax, SYS_EVENT_SET
        mov ebx, [h_ev]
        int 0x80
        test eax, eax
        jnz .fail_set_a

        mov ebx, [pid_a]
        mov eax, SYS_WAITPID
        int 0x80
        mov [child_exit], eax
        test eax, eax                   ; the child exits 0 only if it woke
        jnz .fail_a_exit

        ; ===============================================================
        ; B. parent waits, child signals
        ; ===============================================================
        mov eax, SYS_EVENT_RESET
        mov ebx, [h_ev]
        int 0x80
        test eax, eax
        jnz .fail_reset

        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail_fork_b
        test eax, eax
        jz .child_signaller
        mov [pid_b], eax

        ; The wait blocks here; the child below is what completes it. A timeout
        ; means the wake never arrived, which is the failure this test exists
        ; to catch.
        mov eax, SYS_WAIT_ONE
        mov ebx, [h_ev]
        mov edx, PARENT_WAIT
        int 0x80
        test eax, eax
        jnz .fail_b_timeout

        mov ebx, [pid_b]
        mov eax, SYS_WAITPID
        int 0x80
        test eax, eax
        jnz .fail_b_exit

        mov eax, SYS_OBJECT_CLOSE
        mov ebx, [h_ev]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

        ; --- A: wait in this task, on a handle opened by name ---
.child_waiter:
        mov eax, SYS_OBJECT_OPEN
        mov ebx, ev_name
        mov ecx, R_ALL
        int 0x80
        cmp eax, -1
        je .child_fail_open
        mov [h_child], eax

        mov eax, SYS_WAIT_ONE
        mov ebx, [h_child]
        mov edx, CHILD_WAIT
        int 0x80
        test eax, eax
        jnz .child_fail_wait            ; WAIT_TIMEOUT: the signal never arrived
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

        ; --- B: signal from here, after the parent has blocked ---
.child_signaller:
        mov eax, SYS_SLEEP
        mov ebx, SETTLE_TICKS
        int 0x80

        mov eax, SYS_OBJECT_OPEN
        mov ebx, ev_name
        mov ecx, R_ALL
        int 0x80
        cmp eax, -1
        je .child_fail_open
        mov ebx, eax
        mov eax, SYS_EVENT_SET
        int 0x80
        test eax, eax
        jnz .child_fail_set
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.child_fail_open:
        mov eax, SYS_PRINT
        mov ebx, msg_child_open
        int 0x80
        mov ebx, 3
        mov eax, SYS_EXIT
        int 0x80
.child_fail_wait:
        mov eax, SYS_PRINT
        mov ebx, msg_child_wait
        int 0x80
        mov ebx, 2
        mov eax, SYS_EXIT
        int 0x80
.child_fail_set:
        mov eax, SYS_PRINT
        mov ebx, msg_child_set
        int 0x80
        mov ebx, 4
        mov eax, SYS_EXIT
        int 0x80

.fail_create:
        mov esi, msg_fail_create
        jmp .report_fail
.fail_a_exit:
        mov ebx, msg_fail_a
        call obj_say
        mov eax, [child_exit]
        call obj_hex
        call obj_nl
        jmp .exit_fail_quiet
.fail_b_timeout:
        mov esi, msg_fail_b_timeout
        jmp .report_fail
.fail_b_exit:
        mov esi, msg_fail_b_exit
        jmp .report_fail
.fail_fork_a:
        mov esi, msg_fail_fork
        jmp .report_fail
.fail_set_a:
        mov esi, msg_fail_set
        jmp .report_fail
.fail_reset:
        mov esi, msg_fail_reset
        jmp .report_fail
.fail_fork_b:
        mov esi, msg_fail_fork
        jmp .report_fail
.report_fail:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
.exit_fail_quiet:
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

h_ev:       dd 0
h_child:    dd 0
pid_a:      dd 0
child_exit: dd 0
pid_b:      dd 0
ev_name:    db "/xwait_ev", 0
msg_ok:             db "XWAIT_OK", 10, 0
msg_fail_create:    db "XWAIT_FAIL_CREATE", 10, 0
msg_fail_a:         db "XWAIT_FAIL_A_EXIT=", 0
msg_fail_b_timeout: db "XWAIT_FAIL_B_TIMEOUT", 10, 0
msg_fail_b_exit:    db "XWAIT_FAIL_B_EXIT", 10, 0
msg_fail_fork:      db "XWAIT_FAIL_FORK", 10, 0
msg_fail_set:       db "XWAIT_FAIL_SET", 10, 0
msg_fail_reset:     db "XWAIT_FAIL_RESET", 10, 0
msg_child_open:     db "CHILD_OPEN_FAILED", 10, 0
msg_child_wait:     db "CHILD_WAIT_TIMED_OUT", 10, 0
msg_child_set:      db "CHILD_SET_FAILED", 10, 0

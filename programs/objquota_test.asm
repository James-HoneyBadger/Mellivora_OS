; objquota_test.asm - the per-process object quota is enforced and recoverable
;
; OBJECTS.md §7 charges every object to its creator. Until this test existed the
; charge was recorded and nothing enforced it, so a program that created objects
; in a loop could starve the slab and the failure landed on whatever allocated
; next - usually a different subsystem, with no trace back to the culprit.
;
; What is asserted:
;   1. creating objects in a loop eventually fails instead of grinding the
;      machine down, and the failure is a refusal (-1), not a crash
;   2. the number reached is the documented ceiling, not "whatever memory ran
;      out first" - within the slack the process's own objects take
;   3. closing one frees quota: a create after the refusal succeeds
;   4. closing the rest returns the count to where it started, so the quota is
;      an accounting of live objects and not a high-water mark
;
; Prints QUOTA_OK or QUOTA_FAIL.

%include "syscalls.inc"

QUOTA           equ 256                 ; OBJ_QUOTA_PER_PROCESS in kernel/object.inc
SLACK           equ 64                  ; this process's own objects before the loop
MAX_KEEP        equ QUOTA + SLACK       ; handles remembered for the teardown

; object_stats record layout (kernel/object.inc §OS_*)
OS_LIVE         equ 0
OS_PEAK         equ 4
OS_NAMELEN      equ 8
OS_NAME         equ 12
OS_REC          equ 32
OS_EVENT_LIVE   equ (OBJ_TYPE_EVENT * OS_REC) + OS_LIVE

start:
        ; --- baseline: how many objects already exist for this process ---
        mov eax, SYS_OBJECT_STATS
        mov ebx, stats_buf
        mov ecx, stats_len
        int 0x80
        cmp eax, 0
        jl .fail
        mov eax, [stats_buf + OS_EVENT_LIVE]
        mov [event_live_before], eax

        ; --- 1: create events until one is refused ---
        ; EBP is the counter: it is not an argument to the call, and the syscall
        ; ABI preserves everything but EAX.
        xor ebp, ebp
.create_loop:
        cmp ebp, QUOTA + SLACK
        jae .fail                       ; never refused: no quota at all
        ; A distinct name each time: OD_OPEN_IF on one name would hand back
        ; another handle to the same object, which is a handle-table test and
        ; not a quota test.
        mov esi, name_prefix
        mov edi, name_buf
        mov ecx, name_prefix_len
        rep movsb
        mov eax, ebp
        call append_u32
        mov byte [edi], 0
        mov eax, SYS_OBJECT_CREATE
        mov ebx, name_buf
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        xor edi, edi
        int 0x80
        cmp eax, -1
        je .refused
        cmp ebp, MAX_KEEP
        jae .skip_keep                  ; teardown can only close what it kept
        mov [handles + ebp*4], eax
.skip_keep:
        inc ebp
        test ebp, 0x3F
        jnz .create_loop
        mov eax, SYS_PRINT
        mov ebx, dot
        int 0x80
        jmp .create_loop

.refused:
        mov [made], ebp

        ; --- 2: the refusal came at the ceiling, not at "out of memory" ---
        mov eax, [made]
        add eax, SLACK
        cmp eax, QUOTA
        jb .fail                        ; refused far too early

        ; --- 3: closing one frees quota, so the next create succeeds ---
        cmp dword [made], 0
        jbe .fail
        mov eax, [made]
        dec eax
        mov ebx, [handles + eax*4]
        mov eax, SYS_OBJECT_CLOSE
        int 0x80
        test eax, eax
        jnz .fail
        mov esi, name_prefix
        mov edi, name_buf
        mov ecx, name_prefix_len
        rep movsb
        mov eax, [made]
        call append_u32
        mov byte [edi], 0
        mov eax, SYS_OBJECT_CREATE
        mov ebx, name_buf
        mov ecx, OBJ_TYPE_EVENT
        mov edx, R_ALL
        mov esi, OD_OPEN_IF
        xor edi, edi
        int 0x80
        cmp eax, -1
        je .fail                        ; quota was not returned by close
        mov ebx, eax
        mov eax, SYS_OBJECT_CLOSE
        int 0x80

        ; --- 4: closing the rest returns the live count to its baseline ---
        xor ebp, ebp
.teardown:
        cmp ebp, [made]
        jae .teardown_done
        cmp ebp, MAX_KEEP
        jae .teardown_done
        mov ebx, [handles + ebp*4]
        mov eax, SYS_OBJECT_CLOSE
        int 0x80
        inc ebp
        jmp .teardown
.teardown_done:
        mov eax, SYS_OBJECT_STATS
        mov ebx, stats_buf
        mov ecx, stats_len
        int 0x80
        cmp eax, 0
        jl .fail
        mov eax, [stats_buf + OS_EVENT_LIVE]
        cmp eax, [event_live_before]
        ja .fail                        ; objects leaked instead of closing

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

made:               dd 0
event_live_before:  dd 0
handles:            times MAX_KEEP dd 0
stats_buf:          times 8 * OS_REC + 64 db 0
stats_len           equ 4096
name_prefix:        db "/q"
name_prefix_len     equ 2
name_buf:           times 32 db 0
msg_ok:             db "QUOTA_OK", 10, 0
;-----------------------------------------------------------------------
; append_u32 - write EAX in decimal at EDI, advancing EDI
;   All registers preserved except EDI.
;-----------------------------------------------------------------------
append_u32:
        push eax
        push ebx
        push ecx
        push edx
        sub  esp, 16
        mov  ebx, 10
        xor  ecx, ecx
.ap_div:
        xor  edx, edx
        div  ebx
        add  dl, '0'
        mov  [esp + ecx], dl
        inc  ecx
        test eax, eax
        jnz  .ap_div
.ap_out:
        dec  ecx
        mov  al, [esp + ecx]
        stosb
        test ecx, ecx
        jnz  .ap_out
        add  esp, 16
        pop  edx
        pop  ecx
        pop  ebx
        pop  eax
        ret

msg_fail:           db "QUOTA_FAIL", 10, 0
dot:                db ".", 0

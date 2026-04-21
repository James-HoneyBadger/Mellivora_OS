; killall.asm - Kill processes by name
; Usage: killall [-SIGNAL] <name>
;   killall myprogram       send SIGTERM to all tasks named myprogram
;   killall -9 myprogram    send SIGKILL

%include "syscalls.inc"

TASK_FREE       equ 0
MAX_TASKS       equ 64          ; v4.0 scheduler max
INFO_BUF_SIZE   equ 48          ; state(4)+pid(4)+entry(4)+esp(4)+prio(4)+pgid(4)+sig(4)+exit(4)+name(16)

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces

        mov dword [signum], SIGTERM

        ; Check for signal flag
        cmp byte [esi], '-'
        jne .get_name
        inc esi

        ; Parse numeric signal
        cmp byte [esi], '0'
        jb .skip_flag_word
        cmp byte [esi], '9'
        jg .skip_flag_word
        xor ecx, ecx
.parse_sig:
        mov al, [esi]
        cmp al, '0'
        jb .sig_done
        cmp al, '9'
        ja .sig_done
        sub al, '0'
        imul ecx, 10
        add ecx, eax
        inc esi
        jmp .parse_sig
.sig_done:
        mov [signum], ecx
        call skip_spaces
        jmp .get_name

.skip_flag_word:
        ; skip word (e.g. "KILL", "TERM")
.sfw:   mov al, [esi]
        cmp al, ' '
        je .get_name
        cmp al, 0
        je .get_name
        inc esi
        jmp .sfw

.get_name:
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Copy target process name
        mov edi, target_name
        xor ecx, ecx
.copy_name:
        mov al, [esi]
        cmp al, ' '
        je .name_done
        cmp al, 0
        je .name_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_name
.name_done:
        mov byte [edi + ecx], 0

        ; Iterate all task slots, kill those matching name
        xor ebp, ebp            ; slot counter
        xor edi, edi            ; killed count

.scan_tasks:
        cmp ebp, MAX_TASKS
        jge .done_scan

        mov eax, SYS_PROCLIST
        mov ebx, ebp
        mov ecx, task_info
        int 0x80
        cmp eax, -1
        je .next_task

        ; Skip free slots
        cmp dword [task_info], TASK_FREE
        je .next_task

        ; Compare name (at offset 32 in task_info buffer)
        mov esi, task_info + 32
        mov ecx, target_name
        call name_match
        test eax, eax
        jz .next_task

        ; Name matches - send signal to pid
        mov eax, SYS_SIGNAL
        mov ebx, [task_info + 4]        ; pid
        mov ecx, [signum]
        int 0x80
        cmp eax, 0
        jl .next_task
        inc edi

.next_task:
        inc ebp
        jmp .scan_tasks

.done_scan:
        test edi, edi
        jz .not_found

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_noproc
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;---------------------------------------
; name_match: check if [esi] prefix-matches [ecx] (case-insensitive)
; Returns EAX=1 on match
;---------------------------------------
name_match:
        push esi
        push ecx
.nm:    mov al, [ecx]
        test al, al
        jz .nm_ok
        mov bl, [esi]
        or al, 0x20
        or bl, 0x20
        cmp al, bl
        jne .nm_no
        inc esi
        inc ecx
        jmp .nm
.nm_ok:
        mov eax, 1
        jmp .nm_ret
.nm_no:
        xor eax, eax
.nm_ret:
        pop ecx
        pop esi
        ret

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

msg_usage:      db "Usage: killall [-SIGNAL] <name>", 10, 0
msg_noproc:     db "killall: no process found", 10, 0

signum:         dd SIGTERM
target_name:    times 64 db 0
arg_buf:        times 256 db 0
task_info:      times INFO_BUF_SIZE db 0

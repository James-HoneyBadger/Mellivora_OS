;=======================================================================
; service.inc - the Watch: what starts before logon, and what happens when it
; dies (M4, docs/spec/SERVICES.md section 1)
;
; A service is a record in the Sett and a task in the scheduler. The record is
; the authority - start type, image, dependency, recovery - and the task is what
; the record starts. Between the two sits this file:
;
;   * records are read from \Registry\Machine\System\Services\<name> at boot,
;     so a service definition survives a reboot without a binary format of its
;     own;
;   * a dependency cycle is refused when the record is written, with both ends
;     named, rather than discovered at start time;
;   * the start sequence is boot, then system, then (at logon) auto: a boot
;     service runs before there is a session to log on to;
;   * a dependency that fails fails its dependants with the dependency named;
;   * a task that dies is a failure with a code, and the restart runs after the
;     backoff - 1 s, 5 s, 30 s - until the attempt count is spent, at which
;     point the service is FAILED and the count is reported.
;
; The SCM is a tick, not a thread: svc_tick runs where the kernel is already
; executing (the prompt, the sleep command) and never from an interrupt, so it
; cannot start a task in a half-saved context.
;=======================================================================

SVC_MAX          equ 8
SVC_NAME_LEN     equ 24
SVC_IMAGE_LEN    equ 48
SVC_DEP_LEN      equ 24

SVC_STOPPED      equ 0
SVC_RUNNING      equ 1
SVC_FAILED       equ 2
SVC_RETRY        equ 3                  ; waiting out a backoff
SVC_RECOVER      equ 4                  ; an action is owed to the tick

SVC_PEND_RUN     equ 1
SVC_PEND_REBOOT  equ 2

SVC_BOOT         equ 0
SVC_SYSTEM       equ 1
SVC_AUTO         equ 2
SVC_MANUAL       equ 3
SVC_DISABLED     equ 4

SVC_ACT_LEN      equ 32                 ; one stored recovery action, verbatim

SVC_ENTRY        equ 244
SVC_F_NAME       equ 0
SVC_F_IMAGE      equ 24
SVC_F_START      equ 72
SVC_F_STATE      equ 76
SVC_F_PID        equ 80                 ; TCB index (sched_create_task's return)
SVC_F_DEP        equ 84
SVC_F_ATTEMPTS   equ 108
SVC_F_FAILS      equ 112
SVC_F_NEXTTICK   equ 116
SVC_F_RESTARTS   equ 120
SVC_F_LASTCODE   equ 124
SVC_F_SVCIX      equ 128
SVC_F_ACT        equ 132                ; three actions: SVC_ACT_LEN each
SVC_F_RESET      equ 228                ; quiet seconds before the streak is forgotten
SVC_F_TIMEOUT    equ 232                ; seconds a start may take to appear
SVC_F_LASTFAIL   equ 236                ; tick of the last failure
SVC_F_PEND       equ 240                ; SVC_PEND_*: the action the tick owes

svc_prefix:      db "\Registry\Machine\System\Services", 0
svc_val_image:   db "Image", 0
svc_val_start:   db "Start", 0
svc_val_depend:  db "DependOn", 0
svc_val_attempts: db "Attempts", 0
svc_val_act1:    db "Failure1", 0
svc_val_act2:    db "Failure2", 0
svc_val_act3:    db "Failure3", 0
svc_val_reset:   db "Reset", 0
svc_val_timeout: db "StartTimeout", 0

svc_act_restart: db "restart", 0
svc_act_reboot:  db "reboot", 0
svc_act_none:    db "none", 0
svc_act_run:     db "run:", 0

align 4
svc_table:       times SVC_MAX * SVC_ENTRY db 0
svc_count:       dd 0
svc_name_buf:    times SVC_NAME_LEN db 0
svc_path_buf:    times SETT_KEY_PATH db 0
svc_img_buf:     times 64 db 0
svc_dep_buf:     times SVC_DEP_LEN db 0
svc_tok:         times 24 db 0
svc_argp:        dd 0
svc_arg1:        times 64 db 0
svc_arg2:        times 64 db 0

;-----------------------------------------------------------------------
; svc_slot - EAX = index -> EDI = record
;-----------------------------------------------------------------------
svc_slot:
        imul eax, SVC_ENTRY
        add eax, svc_table
        mov edi, eax
        ret

;-----------------------------------------------------------------------
; svc_find - ESI = name -> EAX = index, CF set when there is no such service
;-----------------------------------------------------------------------
svc_find:
        push esi
        push ebx
        push ecx
        push edx
        mov [.sf_name], esi
        xor ecx, ecx
.sf_loop:
        cmp ecx, [svc_count]
        jae .sf_none
        mov eax, ecx
        call svc_slot
        mov esi, [.sf_name]
        lea ebx, [edi + SVC_F_NAME]
        call key_name_equal
        jc .sf_found
        inc ecx
        jmp .sf_loop
.sf_found:
        mov eax, ecx
        pop edx
        pop ecx
        pop ebx
        pop esi
        clc
        ret
.sf_none:
        pop edx
        pop ecx
        pop ebx
        pop esi
        stc
        ret
.sf_name: dd 0

;-----------------------------------------------------------------------
; svc_task_pid - the pid of the task a record points at
;   In : EAX = TCB index (what SVC_F_PID holds)
;   Out: EAX = that task's pid, or -1 when the slot holds no task
;
; The record holds the task id sched_create_task returned, which is also how
; the TCB is found again (TCB_SVC). This translation is what keeps a signal
; from being aimed at a slot that has since been freed and reused: the slot is
; checked for a live task before its pid is handed to sys_kill.
;-----------------------------------------------------------------------
svc_task_pid:
        cmp eax, MAX_TASKS
        jae .stp_none
        imul eax, TCB_SIZE
        add eax, task_table
        cmp dword [eax + TCB_STATE], TASK_FREE
        je .stp_none
        mov eax, [eax + TCB_PID]
        ret
.stp_none:
        mov eax, -1
        ret

;-----------------------------------------------------------------------
; svc_clear - zero a record and give it the documented defaults
;   In : EDI = record
;-----------------------------------------------------------------------
svc_clear:
        push eax
        push ecx
        push edi
        mov [.sc_rec], edi
        mov ecx, SVC_ENTRY
        xor eax, eax
        cld
        rep stosb
        mov edi, [.sc_rec]
        mov dword [edi + SVC_F_START], SVC_MANUAL
        mov dword [edi + SVC_F_STATE], SVC_STOPPED
        mov dword [edi + SVC_F_PID], -1
        mov dword [edi + SVC_F_ATTEMPTS], 3
        mov dword [edi + SVC_F_SVCIX], -1
        mov dword [edi + SVC_F_LASTCODE], -1
        mov dword [edi + SVC_F_RESET], 60
        mov dword [edi + SVC_F_TIMEOUT], 30
        ; The default recovery is the one this tree had before it was a policy:
        ; the service is restarted, and the attempt count ends the streak.
        mov edi, [.sc_rec]
        lea edi, [edi + SVC_F_ACT]
        mov esi, svc_act_restart
        call str_copy
        mov edi, [.sc_rec]
        lea edi, [edi + SVC_F_ACT + SVC_ACT_LEN]
        mov esi, svc_act_restart
        call str_copy
        mov edi, [.sc_rec]
        lea edi, [edi + SVC_F_ACT + 2 * SVC_ACT_LEN]
        mov esi, svc_act_restart
        call str_copy
        pop edi
        pop ecx
        pop eax
        ret
.sc_rec: dd 0

;-----------------------------------------------------------------------
; svc_build_path - ESI = service name -> svc_path_buf, ESI = svc_path_buf
;-----------------------------------------------------------------------
svc_build_path:
        push eax
        push edi
        mov [.sbp_name], esi
        lea edi, [svc_path_buf]
        mov esi, svc_prefix
        call str_copy
        dec edi                          ; str_copy leaves EDI past the NUL
        mov al, '\'
        stosb
        mov esi, [.sbp_name]
        call str_copy
        lea esi, [svc_path_buf]
        pop edi
        pop eax
        ret
.sbp_name: dd 0

;-----------------------------------------------------------------------
; svc_child_name - EAX = Nth direct child of the services key
;   Out: EDI = svc_name_buf (empty when there is no Nth child), CF when done
;
; The component between the prefix and the next '\' is the name: the Sett has
; no child list, so the keys are walked and the direct children counted.
;-----------------------------------------------------------------------
svc_child_name:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.scn_want], eax
        mov dword [.scn_seen], -1
        mov byte [svc_name_buf], 0
        xor ecx, ecx
.scn_loop:
        cmp ecx, [sett_key_count]
        jae .scn_done
        mov eax, ecx
        call sett_key_base
        lea esi, [ebx + 4]
        mov edi, svc_prefix
        call sett_prefix
        jnc .scn_next
        ; The character after the prefix must be '\', and the component after
        ; that must have no separator of its own.
        lea esi, [svc_prefix]
        call sett_ci_len
        mov edx, eax
        lea esi, [ebx + 4 + edx]
        cmp byte [esi], '\'
        jne .scn_next
        inc esi
        xor edx, edx
.scn_scan:
        cmp byte [esi + edx], 0
        je .scn_name_end
        cmp byte [esi + edx], '\'
        je .scn_next                     ; a grandchild, not a service
        inc edx
        cmp edx, SVC_NAME_LEN - 1
        ja .scn_next
        jmp .scn_scan
.scn_name_end:
        inc dword [.scn_seen]
        mov eax, [.scn_seen]
        cmp eax, [.scn_want]
        jne .scn_next
        mov edi, svc_name_buf
        mov ecx, edx
        cld
        rep movsb
        mov byte [edi], 0
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        lea edi, [svc_name_buf]
        clc
        ret
.scn_next:
        inc ecx
        jmp .scn_loop
.scn_done:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        lea edi, [svc_name_buf]
        stc
        ret
.scn_want: dd 0
.scn_seen: dd 0

;-----------------------------------------------------------------------
; svc_read_str - read a STRING value into a record field
;   In : ESI = service name, EBX = value name, EDI = destination,
;        ECX = room   Out: CF set when the value is absent
;-----------------------------------------------------------------------
svc_read_str:
        push eax
        push ecx
        push edi
        mov [.srs_dst], edi
        mov [.srs_room], ecx
        mov [.srs_val], ebx
        mov esi, esi
        call svc_build_path
        mov edi, esi                     ; EDI = path
        mov esi, [.srs_val]
        call sett_kv_get_str             ; ESI = the string
        jc .srs_no
        mov edi, [.srs_dst]
        mov ecx, [.srs_room]
        call sett_copy_bounded
        pop edi
        pop ecx
        pop eax
        clc
        ret
.srs_no:
        pop edi
        pop ecx
        pop eax
        stc
        ret
.srs_dst:  dd 0
.srs_room: dd 0
.srs_val:  dd 0

;-----------------------------------------------------------------------
; svc_read_u32 - read a U32 value
;   In : ESI = service name, EBX = value name -> EAX = value, CF when absent
;-----------------------------------------------------------------------
svc_read_u32:
        push edi
        mov [.sru_val], ebx
        call svc_build_path
        mov edi, esi                     ; EDI = path
        mov esi, [.sru_val]
        call sett_kv_get_u32
        pop edi
        ret
.sru_val: dd 0

;=======================================================================
; Loading and persisting the records
;=======================================================================

; Read one record from the Sett by name.
;   In : ESI = name -> EAX = index, CF when the record could not be created
svc_load_one:
        pushad
        mov [.slo_name], esi
        call svc_find
        jnc .slo_exists
        mov eax, [svc_count]
        cmp eax, SVC_MAX
        jae .slo_fail
        inc dword [svc_count]
        mov [.slo_ix], eax
        call svc_slot
        mov [.slo_rec], edi
        call svc_clear
        mov esi, [.slo_name]
        mov edi, [.slo_rec]
        call str_copy
        jmp .slo_values
.slo_exists:
        mov [.slo_ix], eax
        mov eax, eax
        call svc_slot
        mov [.slo_rec], edi
        ; the name is already there; the values are re-read
.slo_values:
        mov esi, [.slo_name]
        mov ebx, svc_val_image
        mov edi, [.slo_rec]
        add edi, SVC_F_IMAGE
        mov ecx, SVC_IMAGE_LEN
        call svc_read_str
        mov esi, [.slo_name]
        mov ebx, svc_val_start
        call svc_read_u32
        jc .slo_default_start
        mov edi, [.slo_rec]
        cmp eax, SVC_DISABLED
        jbe .slo_start_ok
        mov eax, SVC_MANUAL
.slo_start_ok:
        mov [edi + SVC_F_START], eax
.slo_default_start:
        mov esi, [.slo_name]
        mov ebx, svc_val_depend
        mov edi, [.slo_rec]
        add edi, SVC_F_DEP
        mov ecx, SVC_DEP_LEN
        call svc_read_str
        mov esi, [.slo_name]
        mov ebx, svc_val_attempts
        call svc_read_u32
        jc .slo_no_att
        mov edi, [.slo_rec]
        cmp eax, 0
        jne .slo_att_ok
        mov eax, 3
.slo_att_ok:
        mov [edi + SVC_F_ATTEMPTS], eax
.slo_no_att:
        ; The three recovery actions. An absent value leaves the default that
        ; svc_clear wrote, so an older record restarts exactly as it did.
        mov esi, [.slo_name]
        mov ebx, svc_val_act1
        mov edi, [.slo_rec]
        add edi, SVC_F_ACT
        mov ecx, SVC_ACT_LEN
        call svc_read_str
        mov esi, [.slo_name]
        mov ebx, svc_val_act2
        mov edi, [.slo_rec]
        add edi, SVC_F_ACT + SVC_ACT_LEN
        mov ecx, SVC_ACT_LEN
        call svc_read_str
        mov esi, [.slo_name]
        mov ebx, svc_val_act3
        mov edi, [.slo_rec]
        add edi, SVC_F_ACT + 2 * SVC_ACT_LEN
        mov ecx, SVC_ACT_LEN
        call svc_read_str
        mov esi, [.slo_name]
        mov ebx, svc_val_reset
        call svc_read_u32
        jc .slo_no_reset
        mov edi, [.slo_rec]
        mov [edi + SVC_F_RESET], eax
.slo_no_reset:
        mov esi, [.slo_name]
        mov ebx, svc_val_timeout
        call svc_read_u32
        jc .slo_no_timeout
        mov edi, [.slo_rec]
        mov [edi + SVC_F_TIMEOUT], eax
.slo_no_timeout:
        mov eax, [.slo_ix]
        mov [esp + 28], eax
        popad
        clc
        ret
.slo_fail:
        popad
        stc
        ret
.slo_name: dd 0
.slo_ix:   dd 0
.slo_rec:  dd 0

; Load every service record the Sett knows.
svc_load_all:
        pushad
        mov dword [svc_count], 0
        xor eax, eax
.sla_loop:
        call svc_child_name
        jc .sla_done
        cmp byte [svc_name_buf], 0
        je .sla_done
        lea esi, [svc_name_buf]
        call svc_load_one
        jc .sla_done
        inc eax
        jmp .sla_loop
.sla_done:
        popad
        ret

; Write a record's values into the Sett and commit them.
;   In : EAX = index -> CF set when it could not be written
svc_persist:
        pushad
        mov [.sp_ix], eax
        call svc_slot
        mov [.sp_rec], edi
        lea esi, [edi + SVC_F_NAME]
        call svc_build_path
        mov [.sp_path], esi

        mov edi, [.sp_path]
        mov esi, svc_val_image
        mov edx, [.sp_rec]
        add edx, SVC_F_IMAGE
        call sett_kv_set_str
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_start
        mov edx, [.sp_rec]
        mov ebx, [edx + SVC_F_START]
        call sett_kv_set_u32
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_depend
        mov edx, [.sp_rec]
        add edx, SVC_F_DEP
        call sett_kv_set_str
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_attempts
        mov edx, [.sp_rec]
        mov ebx, [edx + SVC_F_ATTEMPTS]
        call sett_kv_set_u32
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_act1
        mov edx, [.sp_rec]
        add edx, SVC_F_ACT
        call sett_kv_set_str
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_act2
        mov edx, [.sp_rec]
        add edx, SVC_F_ACT + SVC_ACT_LEN
        call sett_kv_set_str
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_act3
        mov edx, [.sp_rec]
        add edx, SVC_F_ACT + 2 * SVC_ACT_LEN
        call sett_kv_set_str
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_reset
        mov edx, [.sp_rec]
        mov ebx, [edx + SVC_F_RESET]
        call sett_kv_set_u32
        jc .sp_fail
        mov edi, [.sp_path]
        mov esi, svc_val_timeout
        mov edx, [.sp_rec]
        mov ebx, [edx + SVC_F_TIMEOUT]
        call sett_kv_set_u32
        jc .sp_fail
        call sett_save
        jc .sp_fail
        popad
        clc
        ret
.sp_fail:
        popad
        stc
        ret
.sp_ix:   dd 0
.sp_rec:  dd 0
.sp_path: dd 0

;=======================================================================
; Launching: a service is a program task
;=======================================================================

; ESI = image file name in the root -> EAX = pid, CF set on failure
svc_launch_image:
        pushad
        mov [.sli_img], esi
        ; The image is a path - "svcrun" in the root, or "/bin/svcrun".
        ; hbfs_read_file resolves a path itself, including the directory walk,
        ; and restores the current directory before it returns.
        mov esi, [.sli_img]
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .sli_restore_fail
        ; A service image is a program like any other: the header decides.
        mov eax, ecx
        call image_load_program
        jc .sli_fail
        mov [program_entry], eax

.sli_entry:
        ; The launch owns the whole window above its image.
        mov edi, [program_brk_init]
        mov ecx, PROGRAM_END_ADDR
        sub ecx, edi
        shr ecx, 2
        xor eax, eax
        cld
        rep stosd
        call vm_random_stack_top
        mov [program_stack_top], eax
        mov byte [eax + 4], 0xB8
        mov dword [eax + 5], 0
        mov byte [eax + 9], 0xCD
        mov byte [eax + 10], 0x80
        lea edx, [eax + 4]
        mov [eax], edx

        mov byte [program_args_buf], 0
        mov esi, [.sli_img]
        call sched_set_create_name
        mov ebx, [program_entry]
        mov ecx, [program_stack_top]
        call sched_create_task
        test eax, eax
        js .sli_fail
        mov [.sli_pid], eax
        call sched_set_task_args

        ; Give the task its own copy of the window and its stack, exactly as a
        ; desktop launch does.
        mov eax, [.sli_pid]
        imul eax, TCB_SIZE
        add eax, task_table
        mov [.sli_tcb], eax
        call sched_apply_cr3
        call paging_isolate_program_window
        test eax, eax
        js .sli_fail_cr3
        mov eax, [.sli_tcb]
        call paging_setup_user_stack
        xor eax, eax
        call sched_apply_cr3             ; back to the master directory
        mov eax, [.sli_pid]
        mov [esp + 28], eax
        popad
        clc
        ret
.sli_fail_cr3:
        xor eax, eax
        call sched_apply_cr3
        mov eax, [.sli_pid]
        call sched_free_ready_task
.sli_fail:
        mov esi, msg_sli_fail_task
        call vga_print
        mov dword [esp + 28], -1
        popad
        stc
        ret
.sli_restore_fail:
        mov esi, msg_sli_fail_read
        call vga_print
        mov dword [esp + 28], -1
        popad
        stc
        ret
.sli_img: dd 0
.sli_pid: dd 0
.sli_tcb: dd 0

;=======================================================================
; Start, stop, and the recovery policy
;=======================================================================

; EAX = index -> CF set when the service could not be started.
; A dependency is started first; a dependency that will not run fails the
; dependant with the dependency named.
svc_start:
        pushad
        mov [.sst_ix], eax
        call svc_slot
        mov [.sst_rec], edi
        cmp dword [edi + SVC_F_START], SVC_DISABLED
        je .sst_disabled
        cmp dword [edi + SVC_F_STATE], SVC_RUNNING
        je .sst_ok
        ; dependency
        cmp byte [edi + SVC_F_DEP], 0
        je .sst_launch
        lea esi, [edi + SVC_F_DEP]
        call svc_find
        jc .sst_nodep
        mov [.sst_dep], eax
        call svc_start
        jc .sst_depfail
        mov eax, [.sst_dep]
        call svc_slot
        cmp dword [edi + SVC_F_STATE], SVC_RUNNING
        jne .sst_depfail
        jmp .sst_launch
.sst_nodep:
        ; A dependency that is not defined is a record error, not a start order
        ; problem: say which one is missing.
        mov esi, msg_svc_missingdep
        call vga_print
        mov edi, [.sst_rec]
        lea esi, [edi + SVC_F_DEP]
        call vga_print
        call vga_newline
        jmp .sst_fail
.sst_depfail:
        mov esi, msg_svc_depfail
        call vga_print
        mov edi, [.sst_rec]
        lea esi, [edi + SVC_F_DEP]
        call vga_print
        mov esi, msg_svc_depfail2
        call vga_print
        jmp .sst_fail

.sst_launch:
        mov eax, [tick_count]
        mov [.sst_t0], eax
        mov edi, [.sst_rec]
        lea esi, [edi + SVC_F_IMAGE]
        call svc_launch_image
        jc .sst_no_appear
        mov [.sst_pid], eax

        ; The start deadline. A start that spends longer than the record's
        ; timeout before its task exists has not appeared in time, and section
        ; 1.1 is explicit that a missed deadline is a failure whose recovery
        ; action runs - not a service that quietly runs late.
        mov eax, [tick_count]
        sub eax, [.sst_t0]
        mov edi, [.sst_rec]
        mov ecx, [edi + SVC_F_TIMEOUT]
        imul ecx, PIT_HZ
        test ecx, ecx
        jz .sst_up                       ; 0 = no deadline
        cmp eax, ecx
        ja .sst_slow

.sst_up:
        mov edi, [.sst_rec]
        mov eax, [.sst_pid]
        mov [edi + SVC_F_PID], eax
        mov dword [edi + SVC_F_STATE], SVC_RUNNING
        mov dword [edi + SVC_F_PEND], 0  ; a fresh start owes no recovery
        mov eax, [tick_count]
        mov [edi + SVC_F_NEXTTICK], eax
        ; The TCB points back at the record, so a dying task is attributable.
        mov eax, [.sst_pid]
        imul eax, TCB_SIZE
        add eax, task_table
        mov edx, [.sst_ix]
        mov [eax + TCB_SVC], edx
        LOGBOOT LOG_INFO, msg_svc_started
        popad
        clc
        ret
.sst_slow:
        ; The task exists but its start missed the deadline: stop it and count
        ; the attempt, so the policy sees a start that never appeared rather
        ; than one that is merely late.
        mov eax, [.sst_pid]
        call svc_task_pid
        cmp eax, -1
        je .sst_slow_nokill
        mov ebx, eax
        mov ecx, SIGKILL
        call sys_kill
.sst_slow_nokill:
        mov esi, msg_svc_timeout
        call vga_print
        mov edi, [.sst_rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        mov esi, msg_svc_timeout2
        call vga_print
        LOGBOOT LOG_WARN, msg_svc_timeout_log
        mov eax, [.sst_ix]
        mov ebx, -1
        call svc_fail
        popad
        stc
        ret
.sst_no_appear:
        ; The image could not be read or no task could be made: that is this
        ; service's failure, with the attempt counted and the action run.
        mov esi, msg_svc_noappear
        call vga_print
        mov edi, [.sst_rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        mov esi, msg_svc_noappear2
        call vga_print
        mov eax, [.sst_ix]
        mov ebx, -1
        call svc_fail
        popad
        stc
        ret
.sst_disabled:
        mov esi, msg_svc_disabled
        call vga_print
        jmp .sst_fail
.sst_ok:
        popad
        clc
        ret
.sst_fail:
        mov edi, [.sst_rec]
        mov dword [edi + SVC_F_STATE], SVC_FAILED
        popad
        stc
        ret
.sst_ix:   dd 0
.sst_rec:  dd 0
.sst_dep:  dd 0
.sst_pid:  dd 0
.sst_t0:   dd 0

; EAX = index -> CF set when the service was not running
svc_stop:
        pushad
        mov [.sstp_ix], eax
        call svc_slot
        mov [.sstp_rec], edi
        cmp dword [edi + SVC_F_STATE], SVC_RUNNING
        jne .sstp_no
        mov eax, [edi + SVC_F_PID]
        call svc_task_pid
        cmp eax, -1
        je .sstp_clean                  ; the task is already gone
        mov ebx, eax
        mov ecx, SIGKILL
        call sys_kill
.sstp_clean:
        mov edi, [.sstp_rec]
        mov dword [edi + SVC_F_STATE], SVC_STOPPED
        mov dword [edi + SVC_F_PID], -1
        mov dword [edi + SVC_F_FAILS], 0
        popad
        clc
        ret
.sstp_no:
        popad
        stc
        ret
.sstp_ix:  dd 0
.sstp_rec: dd 0

; The SCM tick: notice dead tasks, honour the backoff, and restart what is due.
; Runs in the kernel's own context, never in an interrupt.
svc_tick:
        pushad
        mov dword [.skt_i], 0
.skt_loop:
        mov eax, [.skt_i]
        cmp eax, [svc_count]
        jae .skt_done
        call svc_slot
        mov [.skt_rec], edi
        cmp dword [edi + SVC_F_STATE], SVC_RECOVER
        je .skt_recover
        cmp dword [edi + SVC_F_STATE], SVC_RETRY
        je .skt_due
        ; The reset period (section 1.3): a service that has run on without
        ; failing for long enough begins a new streak, so a failure hours later
        ; is the first of the new one and not the fourth of the old one.
        cmp dword [edi + SVC_F_STATE], SVC_RUNNING
        jne .skt_next
        cmp dword [edi + SVC_F_FAILS], 0
        je .skt_next
        mov eax, [tick_count]
        sub eax, [edi + SVC_F_LASTFAIL]
        mov ecx, [edi + SVC_F_RESET]
        imul ecx, PIT_HZ
        cmp eax, ecx
        jb .skt_next
        mov dword [edi + SVC_F_FAILS], 0
        jmp .skt_next
.skt_recover:
        ; An action that svc_fail could not take in the failing task's context.
        ; The service is not restarted by either action, so both end FAILED.
        mov eax, [edi + SVC_F_PEND]
        mov dword [edi + SVC_F_PEND], 0
        mov dword [edi + SVC_F_STATE], SVC_FAILED
        cmp eax, SVC_PEND_REBOOT
        je .skt_reboot
        cmp eax, SVC_PEND_RUN
        jne .skt_next
        ; The image is the one in the action that fired, which is the failure
        ; count's position in the list.
        mov eax, [edi + SVC_F_FAILS]
        dec eax
        cmp eax, 3
        jb .skt_run_slot
        mov eax, 2
.skt_run_slot:
        imul eax, SVC_ACT_LEN
        add eax, edi
        add eax, SVC_F_ACT + 4          ; past "run:"
        mov [.skt_img], eax
        mov esi, [.skt_img]
        call svc_launch_image
        jc .skt_run_fail
        jmp .skt_next
.skt_run_fail:
        mov esi, msg_svc_runact_fail
        call vga_print
        jmp .skt_next
.skt_reboot:
        call cmd_reboot                 ; does not return
        jmp .skt_next
.skt_due:
        mov eax, [tick_count]
        cmp eax, [edi + SVC_F_NEXTTICK]
        jb .skt_next
        mov eax, [.skt_i]
        call svc_start
        jc .skt_next
        mov edi, [.skt_rec]
        inc dword [edi + SVC_F_RESTARTS]
.skt_next:
        inc dword [.skt_i]
        jmp .skt_loop
.skt_done:
        popad
        call taskq_tick
        ret
.skt_i:   dd 0
.skt_rec: dd 0
.skt_img: dd 0

; A service task died. Called from sched_exit_task before the slot is freed.
;   In : EAX = record index, EBX = exit code
svc_on_exit:
        pushad
        mov [.soe_ix], eax
        cmp eax, SVC_MAX
        jae .soe_done
        call svc_slot
        mov [.soe_rec], edi
        ; The task may have been killed on purpose (sc stop): then the record is
        ; already STOPPED and there is nothing to recover.
        cmp dword [edi + SVC_F_STATE], SVC_RUNNING
        jne .soe_done
        mov [edi + SVC_F_LASTCODE], ebx
        mov dword [edi + SVC_F_PID], -1
        test ebx, ebx
        jnz .soe_failed_exit
        ; Exit zero is a service that finished, not one that crashed: there is
        ; nothing to recover, and the attempt count goes back to zero.
        mov dword [edi + SVC_F_STATE], SVC_STOPPED
        mov dword [edi + SVC_F_FAILS], 0
        jmp .soe_done
.soe_failed_exit:
        mov eax, [.soe_ix]
        call svc_fail
.soe_done:
        popad
        ret
.soe_ix:  dd 0
.soe_rec: dd 0

;=======================================================================
; The recovery policy (SERVICES.md section 1.3)
;=======================================================================

; A service failed. Count the attempt, then run this failure's action.
;   In : EAX = record index, EBX = exit code (-1 = the start never appeared)
;
; The action is the one at the position of the failure in the streak: the first
; failure takes Failure1, the second Failure2, and every failure past the third
; takes Failure3. An action is a word in the record:
;
;   restart   the service is restarted after the backoff (1 s, 5 s, 30 s)
;   run:X     the program X is started and the service is left FAILED
;   reboot    the machine is restarted
;   none      nothing is run and the service is left FAILED
;
; The attempt count still ends the streak: a failure that would be the
; ATTEMPTS'th is the one that makes the service FAILED, whatever the action
; would have been, so a policy cannot restart forever by accident.
svc_fail:
        pushad
        mov [.svf_code], ebx
        cmp eax, SVC_MAX
        jae .svf_done
        call svc_slot
        mov [.svf_rec], edi
        mov [edi + SVC_F_LASTCODE], ebx
        mov dword [edi + SVC_F_PID], -1
        inc dword [edi + SVC_F_FAILS]
        mov eax, [tick_count]
        mov [edi + SVC_F_LASTFAIL], eax
        mov ecx, [edi + SVC_F_FAILS]
        cmp ecx, [edi + SVC_F_ATTEMPTS]
        jae .svf_exhausted

        ; The action for this failure.
        mov eax, ecx
        dec eax
        cmp eax, 3
        jb .svf_have_act
        mov eax, 2
.svf_have_act:
        imul eax, SVC_ACT_LEN
        mov edi, [.svf_rec]
        add edi, SVC_F_ACT
        add eax, edi
        mov [.svf_act], eax

        mov esi, eax
        mov edi, svc_act_restart
        call sett_ci_equal
        jc .svf_restart
        mov esi, [.svf_act]
        mov edi, svc_act_reboot
        call sett_ci_equal
        jc .svf_reboot
        mov esi, [.svf_act]
        mov edi, svc_act_none
        call sett_ci_equal
        jc .svf_exhausted
        ; run:<image>
        mov esi, [.svf_act]
        mov edi, svc_act_run
        call sett_prefix
        jc .svf_run
        ; An action that is none of those is a record that cannot be trusted:
        ; refuse to guess, say so, and leave the service FAILED.
        mov esi, msg_svc_badaction
        call vga_print
        mov edi, [.svf_rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        call vga_newline
        jmp .svf_exhausted

.svf_restart:
        mov edi, [.svf_rec]
        ; 1 s, 5 s, 30 s, then every 30 s.
        mov eax, [tick_count]
        mov ecx, [edi + SVC_F_FAILS]
        cmp ecx, 1
        jne .svf_b2
        add eax, PIT_HZ
        jmp .svf_wait
.svf_b2:
        cmp ecx, 2
        jne .svf_b3
        add eax, 5 * PIT_HZ
        jmp .svf_wait
.svf_b3:
        add eax, 30 * PIT_HZ
.svf_wait:
        mov [edi + SVC_F_NEXTTICK], eax
        mov dword [edi + SVC_F_STATE], SVC_RETRY
        mov esi, msg_svc_restart
        cmp dword [.svf_code], 0
        jge .svf_msg
        mov esi, msg_svc_retry_noappear    ; it never started; it did not exit
.svf_msg:
        call vga_print
        mov edi, [.svf_rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        mov esi, msg_svc_restart2
        call vga_print
        mov edi, [.svf_rec]
        mov eax, [edi + SVC_F_FAILS]
        call vga_print_dec
        call vga_newline
        jmp .svf_done

.svf_run:
        ; The action names a program to run in place of a restart. It is not
        ; started here: a failure is noticed in the dying task's own address
        ; space (svc_on_exit runs inside sched_exit_task), and loading a program
        ; there would build the image under that task's page directory. The
        ; action is recorded and the tick - which always runs where the kernel
        ; is executing with the master directory - carries it out.
        mov esi, msg_svc_runact
        call vga_print
        mov edi, [.svf_rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        mov esi, msg_svc_runact2
        call vga_print
        mov esi, [.svf_act]
        add esi, 4                       ; past "run:"
        call vga_print
        call vga_newline
        LOGBOOT LOG_WARN, msg_svc_runact_log
        mov edi, [.svf_rec]
        mov dword [edi + SVC_F_PEND], SVC_PEND_RUN
        mov dword [edi + SVC_F_STATE], SVC_RECOVER
        jmp .svf_done

.svf_reboot:
        ; The same argument as `run:`: recorded now, run by the tick. A reset
        ; reaches the disk and the chipset, and the tick is the place in this
        ; kernel where that is known to be safe.
        mov esi, msg_svc_reboot
        call vga_print
        mov edi, [.svf_rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        mov esi, msg_svc_reboot2
        call vga_print
        LOGBOOT LOG_WARN, msg_svc_reboot_log
        mov edi, [.svf_rec]
        mov dword [edi + SVC_F_PEND], SVC_PEND_REBOOT
        mov dword [edi + SVC_F_STATE], SVC_RECOVER
        jmp .svf_done

.svf_exhausted:
        mov edi, [.svf_rec]
        mov dword [edi + SVC_F_STATE], SVC_FAILED
        mov esi, msg_svc_failed
        call vga_print
        mov edi, [.svf_rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        mov esi, msg_svc_failed2
        call vga_print
        mov edi, [.svf_rec]
        mov eax, [edi + SVC_F_FAILS]
        call vga_print_dec
        call vga_newline
        LOGBOOT LOG_WARN, msg_svc_failed_log
.svf_done:
        popad
        ret
.svf_rec:  dd 0
.svf_act:  dd 0
.svf_code: dd 0

;-----------------------------------------------------------------------
; svc_act_set - validate and normalise one recovery action
;   In : ESI = the word the user typed, EDI = the record slot to write
;   Out: CF clear on a valid action, CF set when it is not one
;
; The word is stored in one form so that the policy in the Sett reads the same
; whether it was typed as RESTART, Restart or restart.
;-----------------------------------------------------------------------
svc_act_set:
        pushad
        mov [.sas_src], esi
        mov [.sas_dst], edi
        mov edi, svc_act_restart
        call sett_ci_equal
        jc .sas_restart
        mov esi, [.sas_src]
        mov edi, svc_act_reboot
        call sett_ci_equal
        jc .sas_reboot
        mov esi, [.sas_src]
        mov edi, svc_act_none
        call sett_ci_equal
        jc .sas_none
        mov esi, [.sas_src]
        mov edi, svc_act_run
        call sett_prefix
        jc .sas_run
        jmp .sas_bad
.sas_restart:
        mov esi, svc_act_restart
        jmp .sas_copy
.sas_reboot:
        mov esi, svc_act_reboot
        jmp .sas_copy
.sas_none:
        mov esi, svc_act_none
.sas_copy:
        mov edi, [.sas_dst]
        call str_copy
        popad
        clc
        ret
.sas_run:
        ; "run:" and then an image name; the name is what the SCM launches, so
        ; the store keeps it exactly as typed and only the prefix is normalised.
        ; The word is copied whole and then its first four bytes are written as
        ; lower-case "run:", because str_copy copies the terminator too and an
        ; append after it would leave the value reading as "run:".
        mov esi, [.sas_src]
        add esi, 4
        cmp byte [esi], 0
        je .sas_bad
        mov esi, [.sas_src]
        mov edi, [.sas_dst]
        call str_copy
        mov edi, [.sas_dst]
        mov byte [edi + 0], 'r'
        mov byte [edi + 1], 'u'
        mov byte [edi + 2], 'n'
        mov byte [edi + 3], ':'
        popad
        clc
        ret
.sas_bad:
        popad
        stc
        ret
.sas_src: dd 0
.sas_dst: dd 0

;-----------------------------------------------------------------------
; svc_parse_u32 - a decimal number, all digits
;   In : ESI = text   Out: EAX = value, CF set when it is not a number
;-----------------------------------------------------------------------
svc_parse_u32:
        push esi
        push ebx
        mov bl, [esi]
        cmp bl, '0'
        jb .spu_bad
        cmp bl, '9'
        ja .spu_bad
        call parse_decimal
        pop ebx
        pop esi
        clc
        ret
.spu_bad:
        pop ebx
        pop esi
        stc
        ret

; Start every service whose start type is EAX (<= SVC_AUTO).
svc_start_type:
        pushad
        mov [.sstt_type], eax
        mov dword [.sstt_i], 0
.sstt_loop:
        mov eax, [.sstt_i]
        cmp eax, [svc_count]
        jae .sstt_done
        call svc_slot
        mov ecx, [.sstt_type]
        cmp dword [edi + SVC_F_START], ecx
        jne .sstt_next
        mov eax, [.sstt_i]
        call svc_start
.sstt_next:
        inc dword [.sstt_i]
        jmp .sstt_loop
.sstt_done:
        popad
        ret
.sstt_type: dd 0
.sstt_i:    dd 0

; Boot: load the records, run boot services, then system services.
svc_boot:
        pushad
        call svc_load_all
        xor eax, eax
        call svc_start_type
        mov eax, SVC_SYSTEM
        call svc_start_type
        popad
        ret

; Auto services start when somebody logs on.
svc_start_auto:
        pushad
        mov eax, SVC_AUTO
        call svc_start_type
        popad
        ret

;=======================================================================
; sc - the service control surface
;=======================================================================

svc_state_name:
        ; EAX = state -> ESI = word
        cmp eax, SVC_RUNNING
        je .ssn_run
        cmp eax, SVC_FAILED
        je .ssn_fail
        cmp eax, SVC_RETRY
        je .ssn_retry
        cmp eax, SVC_RECOVER
        je .ssn_recover
        mov esi, msg_svc_stopped
        ret
.ssn_run:
        mov esi, msg_svc_running
        ret
.ssn_fail:
        mov esi, msg_svc_failedword
        ret
.ssn_retry:
        mov esi, msg_svc_retryword
        ret
.ssn_recover:
        mov esi, msg_svc_recoverword
        ret

; Print one record. EAX = index.
svc_print_one:
        pushad
        call svc_slot
        mov [.spo_rec], edi
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        mov esi, msg_svc_image
        call vga_print
        mov edi, [.spo_rec]
        lea esi, [edi + SVC_F_IMAGE]
        call vga_print
        mov al, ' '
        call vga_putchar
        mov edi, [.spo_rec]
        mov eax, [edi + SVC_F_STATE]
        call svc_state_name
        call vga_print
        mov esi, msg_svc_pid
        call vga_print
        mov edi, [.spo_rec]
        mov eax, [edi + SVC_F_PID]
        call vga_print_dec
        mov esi, msg_svc_restarts
        call vga_print
        mov edi, [.spo_rec]
        mov eax, [edi + SVC_F_RESTARTS]
        call vga_print_dec
        mov esi, msg_svc_fails
        call vga_print
        mov edi, [.spo_rec]
        mov eax, [edi + SVC_F_FAILS]
        call vga_print_dec
        mov edi, [.spo_rec]
        cmp byte [edi + SVC_F_DEP], 0
        je .spo_nodep
        mov esi, msg_svc_depword
        call vga_print
        lea esi, [edi + SVC_F_DEP]
        call vga_print
.spo_nodep:
        call vga_newline
        popad
        ret
.spo_rec: dd 0

; Print one record's recovery policy on its own line.
;   In : EAX = index
; The policy is four fields the one-line record cannot hold without wrapping,
; and a wrapped `onfail=restart,restart,restart` is unreadable.
svc_print_policy:
        pushad
        call svc_slot
        mov [.spp_rec], edi
        mov esi, msg_svc_recovery
        call vga_print
        mov edi, [.spp_rec]
        mov eax, [edi + SVC_F_RESET]
        call vga_print_dec
        mov esi, msg_svc_resetword
        call vga_print
        mov edi, [.spp_rec]
        mov eax, [edi + SVC_F_TIMEOUT]
        call vga_print_dec
        mov esi, msg_svc_timeoutword
        call vga_print
        mov edi, [.spp_rec]
        mov eax, [edi + SVC_F_ATTEMPTS]
        call vga_print_dec
        mov esi, msg_svc_attemptsword
        call vga_print
        mov edi, [.spp_rec]
        lea esi, [edi + SVC_F_ACT]
        call vga_print
        mov al, ','
        call vga_putchar
        mov edi, [.spp_rec]
        lea esi, [edi + SVC_F_ACT + SVC_ACT_LEN]
        call vga_print
        mov al, ','
        call vga_putchar
        mov edi, [.spp_rec]
        lea esi, [edi + SVC_F_ACT + 2 * SVC_ACT_LEN]
        call vga_print
        call vga_newline
        popad
        ret
.spp_rec: dd 0

; Refuse a dependency that would form a cycle.
;   In : ESI = candidate name, EDI = service path not needed; EBX = the service
;         being configured (name), or 0 when creating under ESI.
;   Out: CF set with both ends printed.
svc_check_cycle:
        pushad
        mov [.scc_want], esi             ; the dependency
        mov [.scc_self], ebx             ; the record's own name
        ; A service that depends on itself is the shortest cycle there is.
        mov esi, [.scc_want]
        mov ebx, [.scc_self]
        call key_name_equal
        jc .scc_cycle
        mov dword [.scc_hops], 0
.scc_follow:
        inc dword [.scc_hops]
        cmp dword [.scc_hops], SVC_MAX
        ja .scc_cycle                    ; a loop that does not pass through us
        mov esi, [.scc_want]
        call svc_find
        jc .scc_ok                       ; not a known service: no cycle
        call svc_slot
        mov [.scc_cur], edi
        cmp byte [edi + SVC_F_DEP], 0
        je .scc_ok
        lea esi, [edi + SVC_F_DEP]
        mov ebx, [.scc_self]
        call key_name_equal
        jc .scc_cycle
        mov edi, [.scc_cur]
        lea esi, [edi + SVC_F_DEP]
        mov [.scc_want], esi
        jmp .scc_follow
.scc_cycle:
        mov esi, msg_svc_cycle
        call vga_print
        mov ebx, [.scc_self]
        test ebx, ebx
        jz .scc_cycle_dep
        mov esi, ebx
        call vga_print
        mov esi, msg_svc_arrow
        call vga_print
.scc_cycle_dep:
        mov esi, [.scc_want]
        call vga_print
        call vga_newline
        popad
        stc
        ret
.scc_ok:
        popad
        clc
        ret
.scc_want: dd 0
.scc_self: dd 0
.scc_cur:  dd 0
.scc_hops: dd 0

; `sc ...` from the shell
svc_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_sc_str
        call str_starts_with
        jc .have
        jmp .usage
.have:
        mov edi, svc_tok
        mov edx, 16
        call sett_word
        jc .usage
        ; The verb compares below use ESI, so the first argument is kept.
        mov [svc_argp], esi

        mov esi, svc_tok
        mov edi, svc_word_list
        call sett_ci_equal
        jc .list
        mov esi, svc_tok
        mov edi, svc_word_create
        call sett_ci_equal
        jc .create
        mov esi, svc_tok
        mov edi, svc_word_delete
        call sett_ci_equal
        jc .delete
        mov esi, svc_tok
        mov edi, svc_word_start
        call sett_ci_equal
        jc .start
        mov esi, svc_tok
        mov edi, svc_word_stop
        call sett_ci_equal
        jc .stop
        mov esi, svc_tok
        mov edi, svc_word_query
        call sett_ci_equal
        jc .query
        mov esi, svc_tok
        mov edi, svc_word_dep
        call sett_ci_equal
        jc .dep
        mov esi, svc_tok
        mov edi, svc_word_failure
        call sett_ci_equal
        jc .failure
        mov esi, svc_tok
        mov edi, svc_word_reset
        call sett_ci_equal
        jc .reset
        mov esi, svc_tok
        mov edi, svc_word_timeout
        call sett_ci_equal
        jc .timeout
        mov esi, svc_tok
        mov edi, svc_word_attempts
        call sett_ci_equal
        jc .attempts
        jmp .usage

;--- list --------------------------------------------------------------
.list:
        cmp dword [svc_count], 0
        jne .list_some
        mov esi, msg_svc_none
        call vga_print
        jmp .done
.list_some:
        mov dword [.i], 0
.list_loop:
        mov eax, [.i]
        cmp eax, [svc_count]
        jae .done
        call svc_print_one
        inc dword [.i]
        jmp .list_loop

;--- create NAME IMAGE [TYPE] [DEP] -------------------------------------
.create:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        mov edi, svc_arg2
        mov edx, SVC_IMAGE_LEN - 1
        call sett_word
        jc .usage
        mov byte [svc_tok], 0
        mov edi, svc_tok
        mov edx, 15
        call sett_word
        mov [.next], esi
        jc .create_type_default
        jmp .create_type_given
.create_type_default:
        mov dword [.type], SVC_MANUAL
        jmp .create_dep
.create_type_given:
        mov esi, svc_tok
        mov edi, svc_word_boot
        call sett_ci_equal
        jc .create_boot
        mov esi, svc_tok
        mov edi, svc_word_system
        call sett_ci_equal
        jc .create_system
        mov esi, svc_tok
        mov edi, svc_word_auto
        call sett_ci_equal
        jc .create_auto
        mov esi, svc_tok
        mov edi, svc_word_disabled
        call sett_ci_equal
        jc .create_disabled
        mov dword [.type], SVC_MANUAL
        jmp .create_dep
.create_boot:
        mov dword [.type], SVC_BOOT
        jmp .create_dep
.create_system:
        mov dword [.type], SVC_SYSTEM
        jmp .create_dep
.create_auto:
        mov dword [.type], SVC_AUTO
        jmp .create_dep
.create_disabled:
        mov dword [.type], SVC_DISABLED
.create_dep:
        ; The type compares use ESI; the dependency, if any, follows the type.
        mov esi, [.next]
        mov byte [svc_dep_buf], 0
        mov edi, svc_dep_buf
        mov edx, SVC_DEP_LEN - 1
        call sett_word
        jc .create_go

.create_go:
        ; A duplicate name is refused: the record would be ambiguous.
        lea esi, [svc_arg1]
        call svc_find
        jnc .create_dup
        ; A dependency cycle is refused now, naming both ends.
        cmp byte [svc_dep_buf], 0
        je .create_new
        lea esi, [svc_dep_buf]
        lea ebx, [svc_arg1]
        call svc_check_cycle
        jc .done
.create_new:
        mov eax, [svc_count]
        cmp eax, SVC_MAX
        jae .create_full
        inc dword [svc_count]
        mov [.ix], eax
        call svc_slot
        call svc_clear
        mov [.rec], edi
        lea esi, [svc_arg1]
        call str_copy
        mov edi, [.rec]
        lea edi, [edi + SVC_F_IMAGE]
        lea esi, [svc_arg2]
        call str_copy
        mov edi, [.rec]
        mov eax, [.type]
        mov [edi + SVC_F_START], eax
        lea edi, [edi + SVC_F_DEP]
        lea esi, [svc_dep_buf]
        call str_copy
        mov eax, [.ix]
        call svc_persist
        jc .create_io
        mov esi, msg_svc_created
        call vga_print
        lea esi, [svc_arg1]
        call vga_print
        call vga_newline
        jmp .done
.create_dup:
        mov esi, msg_svc_dup
        call vga_print
        jmp .done
.create_full:
        mov esi, msg_svc_full
        call vga_print
        jmp .done
.create_io:
        mov esi, msg_svc_iowrite
        call vga_print
        jmp .done

;--- delete NAME --------------------------------------------------------
.delete:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        mov [.ix], eax
        call svc_slot
        cmp dword [edi + SVC_F_STATE], SVC_RUNNING
        je .delete_running
        ; Remove the key from the Sett, then rebuild the table from it.
        lea esi, [edi + SVC_F_NAME]
        call svc_build_path
        mov edi, esi
        call sett_kv_delkey
        call sett_save
        jc .create_io
        call svc_load_all
        mov esi, msg_svc_deleted
        call vga_print
        jmp .done
.delete_running:
        mov esi, msg_svc_delete_running
        call vga_print
        jmp .done

;--- start / stop NAME --------------------------------------------------
.start:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        call svc_start
        jc .start_failed
        mov esi, msg_svc_started_user
        call vga_print
        lea esi, [svc_arg1]
        call vga_print
        call vga_newline
        jmp .done
.start_failed:
        mov esi, msg_svc_startfail
        call vga_print
        lea esi, [svc_arg1]
        call vga_print
        call vga_newline
        jmp .done

.stop:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        call svc_stop
        jc .stop_failed
        mov esi, msg_svc_stopped_user
        call vga_print
        jmp .done
.stop_failed:
        mov esi, msg_svc_stopfail
        call vga_print
        jmp .done

;--- query [NAME] ------------------------------------------------------
.query:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .list
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        mov [.ix], eax
        call svc_print_one
        mov eax, [.ix]
        call svc_print_policy
        jmp .done

;--- failure NAME [A1 [A2 [A3]]] ---------------------------------------
; Section 1.3: up to three actions, taken in order as failures accumulate.
; With no actions given it prints the policy instead of changing it.
.failure:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        mov [.next], esi
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        mov [.ix], eax
        call svc_slot
        mov [.rec], edi
        mov esi, [.next]
        mov edi, svc_arg2
        mov edx, SVC_ACT_LEN - 1
        call sett_word
        jc .failure_show
        mov [.next], esi
        lea esi, [svc_arg2]
        mov edi, [.rec]
        add edi, SVC_F_ACT
        call svc_act_set
        jc .badaction
        mov esi, [.next]
        mov edi, svc_arg2
        mov edx, SVC_ACT_LEN - 1
        call sett_word
        jc .failure_go
        mov [.next], esi
        lea esi, [svc_arg2]
        mov edi, [.rec]
        add edi, SVC_F_ACT + SVC_ACT_LEN
        call svc_act_set
        jc .badaction
        mov esi, [.next]
        mov edi, svc_arg2
        mov edx, SVC_ACT_LEN - 1
        call sett_word
        jc .failure_go
        lea esi, [svc_arg2]
        mov edi, [.rec]
        add edi, SVC_F_ACT + 2 * SVC_ACT_LEN
        call svc_act_set
        jc .badaction
.failure_go:
        jmp .policy_go
.failure_show:
        mov eax, [.ix]
        call svc_print_policy
        jmp .done

;--- reset NAME SECONDS ------------------------------------------------
.reset:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        mov [.next], esi
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        mov [.ix], eax
        call svc_slot
        mov [.rec], edi
        mov esi, [.next]
        mov edi, svc_tok
        mov edx, 15
        call sett_word
        jc .usage
        mov esi, svc_tok
        call svc_parse_u32
        jc .badnum
        mov edi, [.rec]
        mov [edi + SVC_F_RESET], eax
        jmp .policy_go

;--- timeout NAME SECONDS ----------------------------------------------
.timeout:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        mov [.next], esi
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        mov [.ix], eax
        call svc_slot
        mov [.rec], edi
        mov esi, [.next]
        mov edi, svc_tok
        mov edx, 15
        call sett_word
        jc .usage
        mov esi, svc_tok
        call svc_parse_u32
        jc .badnum
        mov edi, [.rec]
        mov [edi + SVC_F_TIMEOUT], eax
        jmp .policy_go

;--- attempts NAME COUNT -----------------------------------------------
.attempts:
        mov esi, [svc_argp]
        mov edi, svc_arg1
        mov edx, SVC_NAME_LEN - 1
        call sett_word
        jc .usage
        mov [.next], esi
        lea esi, [svc_arg1]
        call svc_find
        jc .nosuch
        mov [.ix], eax
        call svc_slot
        mov [.rec], edi
        mov esi, [.next]
        mov edi, svc_tok
        mov edx, 15
        call sett_word
        jc .usage
        mov esi, svc_tok
        call svc_parse_u32
        jc .badnum
        test eax, eax
        jz .badnum
        mov edi, [.rec]
        mov [edi + SVC_F_ATTEMPTS], eax
        jmp .policy_go

.policy_go:
        mov eax, [.ix]
        call svc_persist
        jc .create_io
        mov esi, msg_svc_policy_set
        call vga_print
        lea esi, [svc_arg1]
        call vga_print
        call vga_newline
        jmp .done
.badnum:
        mov esi, msg_svc_badnum
        call vga_print
        jmp .done
.badaction:
        mov esi, msg_svc_badaction_use
        call vga_print
        jmp .done

;--- dep: describe the start order -------------------------------------
.dep:
        cmp dword [svc_count], 0
        jne .dep_some
        mov esi, msg_svc_none
        call vga_print
        jmp .done
.dep_some:
        mov dword [.i], 0
.dep_loop:
        mov eax, [.i]
        cmp eax, [svc_count]
        jae .done
        call svc_slot
        mov [.rec], edi
        mov eax, [edi + SVC_F_START]
        mov esi, msg_svc_bootword
        cmp eax, SVC_BOOT
        je .dep_name
        mov esi, msg_svc_systemword
        cmp eax, SVC_SYSTEM
        je .dep_name
        mov esi, msg_svc_autoword
        cmp eax, SVC_AUTO
        je .dep_name
        mov esi, msg_svc_manualword
        cmp eax, SVC_MANUAL
        je .dep_name
        mov esi, msg_svc_disabledword
.dep_name:
        call vga_print
        mov al, ' '
        call vga_putchar
        mov edi, [.rec]
        lea esi, [edi + SVC_F_NAME]
        call vga_print
        cmp byte [edi + SVC_F_DEP], 0
        je .dep_nl
        mov esi, msg_svc_needs
        call vga_print
        lea esi, [edi + SVC_F_DEP]
        call vga_print
.dep_nl:
        call vga_newline
        inc dword [.i]
        jmp .dep_loop

;--- messages ----------------------------------------------------------
.nosuch:
        mov esi, msg_svc_nosuch
        call vga_print
        jmp .done
.usage:
        mov esi, msg_svc_usage
        call vga_print
.done:
        popad
        ret
.i:    dd 0
.ix:   dd 0
.rec:  dd 0
.type: dd 0
.next: dd 0

svc_word_list:     db "list", 0
svc_word_create:   db "create", 0
svc_word_delete:   db "delete", 0
svc_word_start:    db "start", 0
svc_word_stop:     db "stop", 0
svc_word_query:    db "query", 0
svc_word_dep:      db "dep", 0
svc_word_failure:  db "failure", 0
svc_word_reset:    db "reset", 0
svc_word_timeout:  db "timeout", 0
svc_word_attempts: db "attempts", 0
svc_word_boot:     db "boot", 0
svc_word_system:   db "system", 0
svc_word_auto:     db "auto", 0
svc_word_manual:   db "manual", 0
svc_word_disabled: db "disabled", 0

msg_sli_fail_task:  db "scm: the program could not be started as a task", 0x0A, 0
msg_sli_fail_read:  db "scm: the image could not be read", 0x0A, 0
msg_svc_started:    db "scm: a service started", 0
msg_svc_restart:    db "scm: service '", 0
msg_svc_restart2:   db "' exited non-zero; restart number ", 0
msg_svc_retry_noappear: db "' did not start; retry number ", 0
msg_svc_failed:     db "scm: service '", 0
msg_svc_failed2:    db "' is FAILED after ", 0
msg_svc_failed_log: db "scm: a service exhausted its retries", 0
msg_svc_missingdep: db "sc: the dependency is not defined: ", 0
msg_svc_depfail:    db "sc: the dependency '", 0
msg_svc_depfail2:   db "' did not start", 0x0A, 0
msg_svc_disabled:   db "sc: the service is disabled", 0x0A, 0
msg_svc_cycle:      db "sc: dependency cycle: ", 0
msg_svc_arrow:      db " -> ", 0
msg_svc_running:    db "RUNNING", 0
msg_svc_stopped:    db "STOPPED", 0
msg_svc_failedword: db "FAILED", 0
msg_svc_retryword:  db "RESTARTING", 0
msg_svc_recoverword: db "RECOVERING", 0
msg_svc_image:      db " image=", 0
msg_svc_pid:        db " pid=", 0
msg_svc_restarts:   db " restarts=", 0
msg_svc_fails:      db " fails=", 0
msg_svc_depword:    db " needs=", 0
msg_svc_none:       db "sc: no services", 0x0A, 0
msg_svc_created:    db "sc: created ", 0
msg_svc_deleted:    db "sc: deleted", 0x0A, 0
msg_svc_dup:        db "sc: a service of that name already exists", 0x0A, 0
msg_svc_full:       db "sc: the service table is full", 0x0A, 0
msg_svc_iowrite:    db "sc: could not write the record", 0x0A, 0
msg_svc_delete_running: db "sc: delete is refused while the service runs", 0x0A, 0
msg_svc_started_user: db "sc: started ", 0
msg_svc_startfail:  db "sc: could not start ", 0
msg_svc_stopped_user: db "sc: stopped", 0x0A, 0
msg_svc_stopfail:   db "sc: the service was not running", 0x0A, 0
msg_svc_nosuch:     db "sc: no such service", 0x0A, 0
msg_svc_needs:      db " needs ", 0
msg_svc_bootword:   db "boot  ", 0
msg_svc_systemword: db "system", 0
msg_svc_autoword:   db "auto  ", 0
msg_svc_manualword: db "manual", 0
msg_svc_disabledword: db "off   ", 0
msg_svc_usage:
        db "sc list | sc create NAME IMAGE [boot|system|auto|manual|disabled] [DEP]", 0x0A
        db "sc start NAME | sc stop NAME | sc query [NAME] | sc delete NAME", 0x0A
        db "sc dep - the start order and what each service needs", 0x0A
        db "sc failure NAME [restart|reboot|run:IMAGE|none] ... (up to three)", 0x0A
        db "sc reset NAME SECS | sc timeout NAME SECS | sc attempts NAME COUNT", 0x0A, 0
msg_svc_policy_set:  db "sc: policy changed for ", 0
msg_svc_badnum:      db "sc: that is not a number", 0x0A, 0
msg_svc_badaction_use: db "sc: an action is restart, reboot, run:IMAGE or none", 0x0A, 0
msg_svc_badaction:   db "scm: the recovery action is not one this build knows: ", 0
msg_svc_timeout:     db "scm: service '", 0
msg_svc_timeout2:    db "' missed its start timeout", 0x0A, 0
msg_svc_timeout_log: db "scm: a service missed its start timeout", 0
msg_svc_noappear:    db "scm: service '", 0
msg_svc_noappear2:   db "' did not appear", 0x0A, 0
msg_svc_runact:      db "scm: service '", 0
msg_svc_runact2:     db "' runs the recovery program ", 0
msg_svc_runact_log:  db "scm: a service ran its recovery program", 0
msg_svc_runact_fail: db "scm: the recovery program could not be started", 0x0A, 0
msg_svc_reboot:      db "scm: service '", 0
msg_svc_reboot2:     db "' recovery: restarting the machine", 0x0A, 0
msg_svc_reboot_log:  db "scm: a service recovery action rebooted the machine", 0
msg_svc_recovery:    db "recovery: reset=", 0
msg_svc_resetword:   db "s timeout=", 0
msg_svc_timeoutword: db "s tries=", 0
msg_svc_attemptsword: db " onfail=", 0

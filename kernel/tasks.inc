;=======================================================================
; tasks.inc - the task scheduler (M4, ROADMAP.md M4)
;
; A task is a program the machine runs on a trigger rather than on a command:
; at boot, at logon, every N seconds, or once a day at a wall-clock time. The
; record lives in the Sett under \Registry\Machine\System\Tasks\<name>, so a
; scheduled job survives a reboot the same way a service definition does.
;
; The missed-run policy is the interesting half. A machine that was off at
; 03:00 has a choice when it comes back at 09:00: run the job it missed, or
; accept that the window passed. `skip` is the default because a daily report
; that runs at the wrong hour is often worse than one that does not run, and
; `run` is there for the jobs where the opposite is true.
;
; Like the SCM this is a tick, not a thread: taskq_tick runs at the prompt and
; inside sleep, never in an interrupt, and it launches through the service
; manager's image loader so there is one way a kernel task becomes a program.
;=======================================================================

TASKQ_MAX        equ 6
TASKQ_NAME       equ 24
TASKQ_IMAGE      equ 48
TASKQ_TRIG       equ 24

TASKQ_ENTRY      equ 120
TQ_F_NAME        equ 0
TQ_F_IMAGE       equ 24
TQ_F_TRIG        equ 72
TQ_F_TYPE        equ 96           ; 0 boot, 1 logon, 2 every, 3 daily
TQ_F_VALUE       equ 100          ; seconds, or minute-of-day
TQ_F_POLICY      equ 104          ; 0 skip, 1 run
TQ_F_LASTTICK    equ 108
TQ_F_LASTDAY     equ 112
TQ_F_RUNS        equ 116

TASKQ_BOOT       equ 0
TASKQ_LOGON      equ 1
TASKQ_EVERY      equ 2
TASKQ_DAILY      equ 3

TASKQ_SKIP       equ 0
TASKQ_RUN        equ 1

taskq_prefix:    db "\Registry\Machine\System\Tasks", 0
taskq_val_image: db "Image", 0
taskq_val_trig:  db "Trigger", 0
taskq_val_policy: db "Policy", 0

align 4
taskq_table:     times TASKQ_MAX * TASKQ_ENTRY db 0
taskq_count:     dd 0
taskq_namebuf:   times TASKQ_NAME db 0
taskq_pathbuf:   times SETT_KEY_PATH db 0
taskq_arg1:      times 32 db 0
taskq_arg2:      times 64 db 0
taskq_arg3:      times 32 db 0
taskq_argp:      dd 0
taskq_scratch:   times TASKQ_ENTRY db 0

;-----------------------------------------------------------------------
; taskq_slot - EAX = index -> EDI = record
;-----------------------------------------------------------------------
taskq_slot:
        imul eax, TASKQ_ENTRY
        add eax, taskq_table
        mov edi, eax
        ret

;-----------------------------------------------------------------------
; taskq_find - ESI = name -> EAX = index, CF set when absent
;-----------------------------------------------------------------------
taskq_find:
        push esi
        push ebx
        push ecx
        push edx
        mov [.tf_name], esi
        xor ecx, ecx
.tf_loop:
        cmp ecx, [taskq_count]
        jae .tf_none
        mov eax, ecx
        call taskq_slot
        mov esi, [.tf_name]
        lea ebx, [edi + TQ_F_NAME]
        call key_name_equal
        jc .tf_found
        inc ecx
        jmp .tf_loop
.tf_found:
        mov eax, ecx
        pop edx
        pop ecx
        pop ebx
        pop esi
        clc
        ret
.tf_none:
        pop edx
        pop ecx
        pop ebx
        pop esi
        stc
        ret
.tf_name: dd 0

;-----------------------------------------------------------------------
; taskq_build_path - ESI = name -> taskq_pathbuf
;-----------------------------------------------------------------------
taskq_build_path:
        push eax
        push edi
        mov [.tbp_name], esi
        lea edi, [taskq_pathbuf]
        mov esi, taskq_prefix
        call str_copy
        dec edi
        mov al, '\'
        stosb
        mov esi, [.tbp_name]
        call str_copy
        lea esi, [taskq_pathbuf]
        pop edi
        pop eax
        ret
.tbp_name: dd 0

;-----------------------------------------------------------------------
; taskq_parse_trigger - ESI = text, EDI = record
;   Out: CF set when the trigger is not one this scheduler knows
;-----------------------------------------------------------------------
taskq_parse_trigger:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.tpt_rec], edi
        mov [.tpt_text], esi
        mov esi, [.tpt_text]
        mov edi, taskq_word_boot
        call sett_ci_equal
        jc .tpt_boot
        mov esi, [.tpt_text]
        mov edi, taskq_word_logon
        call sett_ci_equal
        jc .tpt_logon
        mov esi, [.tpt_text]
        mov edi, taskq_word_every
        call sett_prefix
        jc .tpt_every
        mov esi, [.tpt_text]
        mov edi, taskq_word_daily
        call sett_prefix
        jc .tpt_daily
        jmp .tpt_bad

.tpt_boot:
        mov edi, [.tpt_rec]
        mov dword [edi + TQ_F_TYPE], TASKQ_BOOT
        jmp .tpt_ok
.tpt_logon:
        mov edi, [.tpt_rec]
        mov dword [edi + TQ_F_TYPE], TASKQ_LOGON
        jmp .tpt_ok

.tpt_every:
        mov esi, [.tpt_text]
        add esi, 6                       ; past "every:"
        call parse_decimal
        test eax, eax
        jz .tpt_bad
        mov edi, [.tpt_rec]
        mov dword [edi + TQ_F_TYPE], TASKQ_EVERY
        mov [edi + TQ_F_VALUE], eax
        jmp .tpt_ok

.tpt_daily:
        ; "daily:HH:MM" -> minutes since midnight
        mov esi, [.tpt_text]
        add esi, 6
        call parse_decimal
        cmp eax, 23
        ja .tpt_bad
        imul eax, 60
        mov ebx, eax
        mov esi, [.tpt_text]
        add esi, 6
.tpt_colon:
        mov al, [esi]
        test al, al
        jz .tpt_bad
        inc esi
        cmp al, ':'
        jne .tpt_colon
        call parse_decimal
        cmp eax, 59
        ja .tpt_bad
        add eax, ebx
        mov edi, [.tpt_rec]
        mov dword [edi + TQ_F_TYPE], TASKQ_DAILY
        mov [edi + TQ_F_VALUE], eax
        jmp .tpt_ok

.tpt_ok:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        clc
        ret
.tpt_bad:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        stc
        ret
.tpt_rec:  dd 0
.tpt_text: dd 0

;-----------------------------------------------------------------------
; taskq_load_one - read a record from the Sett
;   In : ESI = name -> EAX = index, CF when the table is full
;-----------------------------------------------------------------------
taskq_load_one:
        pushad
        mov [.tlo_name], esi
        call taskq_find
        jnc .tlo_have
        mov eax, [taskq_count]
        cmp eax, TASKQ_MAX
        jae .tlo_fail
        inc dword [taskq_count]
        mov [.tlo_ix], eax
        call taskq_slot
        push edi
        mov ecx, TASKQ_ENTRY
        xor eax, eax
        cld
        rep stosb
        pop edi
        mov esi, [.tlo_name]
        call str_copy
        jmp .tlo_values
.tlo_have:
        mov [.tlo_ix], eax
        call taskq_slot
.tlo_values:
        mov [.tlo_rec], edi

        mov esi, [.tlo_name]
        call taskq_build_path
        mov [.tlo_path], esi

        mov edi, [.tlo_path]
        mov esi, taskq_val_image
        call sett_kv_get_str
        jc .tlo_done
        mov edi, [.tlo_rec]
        add edi, TQ_F_IMAGE
        mov ecx, TASKQ_IMAGE
        call sett_copy_bounded

        mov edi, [.tlo_path]
        mov esi, taskq_val_trig
        call sett_kv_get_str
        jc .tlo_done
        mov edi, [.tlo_rec]
        add edi, TQ_F_TRIG
        mov ecx, TASKQ_TRIG
        call sett_copy_bounded
        ; parse the trigger text just copied
        mov edi, [.tlo_rec]
        lea esi, [edi + TQ_F_TRIG]
        call taskq_parse_trigger

        mov edi, [.tlo_path]
        mov esi, taskq_val_policy
        call sett_kv_get_u32
        jc .tlo_done
        mov edi, [.tlo_rec]
        cmp eax, TASKQ_RUN
        jbe .tlo_pol_ok
        mov eax, TASKQ_SKIP
.tlo_pol_ok:
        mov [edi + TQ_F_POLICY], eax
.tlo_done:
        mov eax, [.tlo_ix]
        mov [esp + 28], eax
        popad
        clc
        ret
.tlo_fail:
        popad
        stc
        ret
.tlo_name: dd 0
.tlo_ix:   dd 0
.tlo_rec:  dd 0
.tlo_path: dd 0

;-----------------------------------------------------------------------
; taskq_load_all - read every record the Sett knows
;-----------------------------------------------------------------------
taskq_load_all:
        pushad
        mov dword [taskq_count], 0
        xor eax, eax
.tla_loop:
        mov esi, taskq_prefix
        lea edi, [taskq_namebuf]
        mov edx, TASKQ_NAME - 1
        call sett_child_name
        jc .tla_done
        cmp byte [taskq_namebuf], 0
        je .tla_done
        lea esi, [taskq_namebuf]
        call taskq_load_one
        jc .tla_done
        inc eax
        jmp .tla_loop
.tla_done:
        popad
        ret

;-----------------------------------------------------------------------
; taskq_persist - write a record back and commit
;   In : EAX = index -> CF set when the write failed
;-----------------------------------------------------------------------
taskq_persist:
        pushad
        mov [.tp_ix], eax
        call taskq_slot
        mov [.tp_rec], edi
        lea esi, [edi + TQ_F_NAME]
        call taskq_build_path
        mov [.tp_path], esi

        mov edi, [.tp_path]
        mov esi, taskq_val_image
        mov edx, [.tp_rec]
        add edx, TQ_F_IMAGE
        call sett_kv_set_str
        jc .tp_fail
        mov edi, [.tp_path]
        mov esi, taskq_val_trig
        mov edx, [.tp_rec]
        add edx, TQ_F_TRIG
        call sett_kv_set_str
        jc .tp_fail
        mov edi, [.tp_path]
        mov esi, taskq_val_policy
        mov edx, [.tp_rec]
        mov ebx, [edx + TQ_F_POLICY]
        call sett_kv_set_u32
        jc .tp_fail
        call sett_save
        jc .tp_fail
        popad
        clc
        ret
.tp_fail:
        popad
        stc
        ret
.tp_ix:   dd 0
.tp_rec:  dd 0
.tp_path: dd 0

;-----------------------------------------------------------------------
; taskq_run - start the record's image
;   In : EAX = index -> CF set when the program could not be started
;-----------------------------------------------------------------------
taskq_run:
        pushad
        mov [.tr_ix], eax
        call taskq_slot
        lea esi, [edi + TQ_F_IMAGE]
        call svc_launch_image
        jc .tr_fail
        mov edi, [.tr_ix]
        mov eax, edi
        call taskq_slot
        inc dword [edi + TQ_F_RUNS]
        mov eax, [tick_count]
        mov [edi + TQ_F_LASTTICK], eax
        mov esi, msg_task_ran
        call vga_print
        mov edi, [.tr_ix]
        mov eax, edi
        call taskq_slot
        lea esi, [edi + TQ_F_NAME]
        call vga_print
        call vga_newline
        popad
        clc
        ret
.tr_fail:
        popad
        stc
        ret
.tr_ix: dd 0

;-----------------------------------------------------------------------
; taskq_run_type - run every record of one trigger type
;   In : EAX = TASKQ_BOOT or TASKQ_LOGON
;-----------------------------------------------------------------------
taskq_run_type:
        pushad
        mov [.trt_type], eax
        mov dword [.trt_i], 0
.trt_loop:
        mov eax, [.trt_i]
        cmp eax, [taskq_count]
        jae .trt_done
        call taskq_slot
        mov ecx, [.trt_type]
        cmp dword [edi + TQ_F_TYPE], ecx
        jne .trt_next
        mov eax, [.trt_i]
        call taskq_run
.trt_next:
        inc dword [.trt_i]
        jmp .trt_loop
.trt_done:
        popad
        ret
.trt_type: dd 0
.trt_i:    dd 0

;-----------------------------------------------------------------------
; taskq_tick - run what is due
;
; `every:N` is due N seconds after its last run. If more than two intervals
; have passed, the job was missed: the policy decides whether it runs once now
; or is dropped and the clock restarts. `daily:HH:MM` is due once a day; the
; first tick of a new day, at or after the time, is the catch-up point.
;-----------------------------------------------------------------------
taskq_tick:
        pushad
        mov dword [.tt_i], 0
.tt_loop:
        mov eax, [.tt_i]
        cmp eax, [taskq_count]
        jae .tt_done
        call taskq_slot
        mov [.tt_rec], edi
        mov eax, [edi + TQ_F_TYPE]
        cmp eax, TASKQ_EVERY
        je .tt_every
        cmp eax, TASKQ_DAILY
        je .tt_daily
        jmp .tt_next

.tt_every:
        mov eax, [edi + TQ_F_VALUE]
        imul eax, PIT_HZ                  ; seconds -> ticks
        mov ecx, [edi + TQ_F_LASTTICK]
        add ecx, eax                      ; due at
        mov edx, [tick_count]
        cmp edx, ecx
        jb .tt_next
        ; missed if a whole further interval has gone by since the due point
        mov ebx, edx
        sub ebx, ecx
        cmp ebx, eax
        jb .tt_run
        cmp dword [edi + TQ_F_POLICY], TASKQ_RUN
        je .tt_run
        mov [edi + TQ_F_LASTTICK], edx    ; drop it and restart the clock
        jmp .tt_next
.tt_run:
        mov eax, [.tt_i]
        call taskq_run
        jmp .tt_next

.tt_daily:
        call rtc_read_time
        movzx eax, byte [rtc_hour]
        imul eax, 60
        movzx ecx, byte [rtc_min]
        add eax, ecx                      ; minute of day now
        mov edi, [.tt_rec]
        movzx edx, byte [rtc_day]
        cmp edx, [edi + TQ_F_LASTDAY]
        je .tt_next                       ; already had its day
        cmp eax, [edi + TQ_F_VALUE]
        jb .tt_next                       ; too early today
        ; It is the first time the machine has looked today and the time has
        ; passed: this is the missed-run decision.
        cmp dword [edi + TQ_F_POLICY], TASKQ_RUN
        je .tt_daily_run
        mov [edi + TQ_F_LASTDAY], edx     ; the window is gone: do not run it
        jmp .tt_next
.tt_daily_run:
        mov [edi + TQ_F_LASTDAY], edx
        mov eax, [.tt_i]
        call taskq_run

.tt_next:
        inc dword [.tt_i]
        jmp .tt_loop
.tt_done:
        popad
        ret
.tt_i:   dd 0
.tt_rec: dd 0

taskq_boot:
        pushad
        call taskq_load_all
        mov eax, TASKQ_BOOT
        call taskq_run_type
        popad
        ret

taskq_logon:
        pushad
        mov eax, TASKQ_LOGON
        call taskq_run_type
        popad
        ret

;=======================================================================
; `tasks ...`
;=======================================================================
taskq_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_tasks_str
        call str_starts_with
        jc .have
        jmp .usage
.have:
        mov edi, taskq_namebuf
        mov edx, 16
        call sett_word
        jc .usage
        mov [taskq_argp], esi
        mov esi, taskq_namebuf
        mov edi, taskq_word_add
        call sett_ci_equal
        jc .add
        mov esi, taskq_namebuf
        mov edi, taskq_word_del
        call sett_ci_equal
        jc .del
        mov esi, taskq_namebuf
        mov edi, taskq_word_run
        call sett_ci_equal
        jc .run
        mov esi, taskq_namebuf
        mov edi, taskq_word_list
        call sett_ci_equal
        jc .list
        jmp .usage

.add:
        mov esi, [taskq_argp]
        mov edi, taskq_arg1
        mov edx, TASKQ_NAME - 1
        call sett_word
        jc .usage
        mov edi, taskq_arg2
        mov edx, TASKQ_IMAGE - 1
        call sett_word
        jc .usage
        mov edi, taskq_arg3
        mov edx, TASKQ_TRIG - 1
        call sett_word
        jc .usage
        mov [.next], esi

        ; Validate the trigger before anything is written, into a scratch
        ; record so a bad one cannot change a real record's type.
        lea esi, [taskq_arg3]
        mov edi, taskq_scratch
        call taskq_parse_trigger
        jc .badtrig

        lea esi, [taskq_arg1]
        call taskq_find
        jnc .dup
        mov eax, [taskq_count]
        cmp eax, TASKQ_MAX
        jae .full
        inc dword [taskq_count]
        mov [.ix], eax
        call taskq_slot
        mov [.rec], edi
        push edi
        mov ecx, TASKQ_ENTRY
        xor eax, eax
        cld
        rep stosb
        pop edi
        lea esi, [taskq_arg1]
        call str_copy
        mov edi, [.rec]
        add edi, TQ_F_IMAGE
        lea esi, [taskq_arg2]
        call str_copy
        mov edi, [.rec]
        add edi, TQ_F_TRIG
        lea esi, [taskq_arg3]
        call str_copy
        ; parse into the record itself
        mov edi, [.rec]
        lea esi, [edi + TQ_F_TRIG]
        call taskq_parse_trigger
        mov dword [edi + TQ_F_POLICY], TASKQ_SKIP
        ; an optional fourth word chooses the missed-run policy
        mov esi, [.next]
        mov edi, taskq_arg2
        mov edx, 15
        call sett_word
        jc .persist
        mov esi, taskq_arg2
        mov edi, taskq_word_run
        call sett_ci_equal
        jnc .persist
        mov edi, [.rec]
        mov dword [edi + TQ_F_POLICY], TASKQ_RUN
.persist:
        mov eax, [.ix]
        call taskq_persist
        jc .iowrite
        mov esi, msg_task_added
        call vga_print
        lea esi, [taskq_arg1]
        call vga_print
        call vga_newline
        jmp .done
.dup:
        mov esi, msg_task_dup
        call vga_print
        jmp .done
.full:
        mov esi, msg_task_full
        call vga_print
        jmp .done
.badtrig:
        mov esi, msg_task_badtrig
        call vga_print
        jmp .done
.iowrite:
        mov esi, msg_task_iowrite
        call vga_print
        jmp .done

.del:
        mov esi, [taskq_argp]
        mov edi, taskq_arg1
        mov edx, TASKQ_NAME - 1
        call sett_word
        jc .usage
        lea esi, [taskq_arg1]
        call taskq_find
        jc .nosuch
        mov [.ix], eax
        call taskq_slot
        lea esi, [edi + TQ_F_NAME]
        call taskq_build_path
        mov edi, esi
        call sett_kv_delkey
        jc .nosuch
        call sett_save
        call taskq_load_all
        mov esi, msg_task_deleted
        call vga_print
        jmp .done

.run:
        mov esi, [taskq_argp]
        mov edi, taskq_arg1
        mov edx, TASKQ_NAME - 1
        call sett_word
        jc .usage
        lea esi, [taskq_arg1]
        call taskq_find
        jc .nosuch
        call taskq_run
        jc .runfail
        jmp .done
.runfail:
        mov esi, msg_task_runfail
        call vga_print
        jmp .done

.list:
        cmp dword [taskq_count], 0
        jne .list_some
        mov esi, msg_task_none
        call vga_print
        jmp .done
.list_some:
        mov dword [.i], 0
.list_loop:
        mov eax, [.i]
        cmp eax, [taskq_count]
        jae .done
        call taskq_slot
        mov [.rec], edi
        lea esi, [edi + TQ_F_NAME]
        call vga_print
        mov al, ' '
        call vga_putchar
        mov edi, [.rec]
        lea esi, [edi + TQ_F_TRIG]
        call vga_print
        mov esi, msg_task_runs
        call vga_print
        mov edi, [.rec]
        mov eax, [edi + TQ_F_RUNS]
        call vga_print_dec
        call vga_newline
        inc dword [.i]
        jmp .list_loop

.nosuch:
        mov esi, msg_task_nosuch
        call vga_print
        jmp .done
.usage:
        mov esi, msg_task_usage
        call vga_print
.done:
        popad
        ret
.i:    dd 0
.ix:   dd 0
.rec:  dd 0
.type: dd 0
.next: dd 0

taskq_word_boot:   db "boot", 0
taskq_word_logon:  db "logon", 0
taskq_word_every:  db "every:", 0
taskq_word_daily:  db "daily:", 0
taskq_word_add:    db "add", 0
taskq_word_del:    db "del", 0
taskq_word_run:    db "run", 0
taskq_word_list:   db "list", 0

msg_task_ran:      db "tasks: ran ", 0
msg_task_added:    db "tasks: added ", 0
msg_task_deleted:  db "tasks: deleted", 0x0A, 0
msg_task_dup:      db "tasks: that name already exists", 0x0A, 0
msg_task_full:     db "tasks: the table is full", 0x0A, 0
msg_task_badtrig:  db "tasks: the trigger is boot, logon, every:SECONDS or daily:HH:MM", 0x0A, 0
msg_task_iowrite:  db "tasks: could not write the record", 0x0A, 0
msg_task_nosuch:   db "tasks: no such task", 0x0A, 0
msg_task_runfail:  db "tasks: the program could not be started", 0x0A, 0
msg_task_none:     db "tasks: nothing scheduled", 0x0A, 0
msg_task_runs:     db " runs=", 0
msg_task_usage:
        db "tasks add NAME IMAGE TRIGGER [skip|run] | tasks del NAME", 0x0A
        db "tasks run NAME | tasks list", 0x0A
        db "  TRIGGER is boot, logon, every:SECONDS or daily:HH:MM; the last", 0x0A
        db "  word is the missed-run policy (skip by default).", 0x0A, 0

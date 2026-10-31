;=======================================================================
; tls.inc - per-module, per-thread thread-local storage (EXEC.md section 6)
;
; A module that wants thread-local storage declares a directory in its image:
;
;   u32 raw_size     initialised bytes in the template
;   u32 data_rva     the template, inside the image
;   u32 index_rva    a dword the loader fills with this module's index
;   u32 base_rva     a dword the loader fills with this thread's block
;
; The division of labour follows from when each number can be known:
;
;   * The **index** is assigned while the image is still in the window, before
;     it is copied or mapped - the main image is module 0, and each library
;     takes the next free index - so it is written into the file copy and both
;     roads out of the loader carry it (kernel/hbx.inc).
;
;   * The **block** needs the thread, because it is that thread's own storage:
;     it is page-rounded, filled from the module's template and zeroed past it,
;     mapped where the next random draw puts it, and recorded as a vm area of
;     its thread group, so exit releases it with every other mapping.
;
; The base slot is the awkward part. A module reads it with one load -
; `mov ebx, [base_rva]` - and the slot lives in the image, which every thread of
; a process shares. So the slot can only hold the value that is true for the
; thread running right now, and the scheduler rewrites it on every switch. Each
; task carries a **view**: one entry per module, naming the slot to write and
; the block that belongs to this thread, plus the template a later thread
; copies from. Swapping two threads of one process therefore makes the same slot
; name two different blocks, which is what makes the storage thread-local.
;
; A thread that does not need TLS never pays for it: an image with no TLS
; directory contributes no entries, and tls_apply_view writes nothing.
;=======================================================================

;-----------------------------------------------------------------------
; tls_setup_task - give a launched process its first thread's blocks
;   In : EDI = task id
;   Out: EAX = blocks mapped
;
; Called once the task exists, next to lib_map_modules and for the same reason.
; A block that cannot be mapped is reported and skipped: the module's base slot
; stays zero, and a program that reads it without checking faults inside its own
; task, which the fault path contains.
;
; The slot is written here as well as on every switch into the task, so a window
; image has its base in place before anything can run.
;-----------------------------------------------------------------------
tls_setup_task:
        pushad
        call tls_begin
        jc .tst_done

        ; ---- the main image, module 0 ----
        mov eax, [hbx_main_tls_dir]
        test eax, eax
        jz .tst_libs
        mov eax, [hbx_main_tls_size]
        mov [tls_size], eax
        mov eax, [hbx_main_base]
        add eax, [hbx_main_tls_data]
        mov [tls_tmpl_addr], eax
        mov eax, [hbx_main_base]
        add eax, [hbx_main_tls_base_rva]
        mov [tls_slot_addr], eax
        mov dword [tls_write_slot], 1
        call tls_add_block

.tst_libs:
        ; ---- each library, which took the next index at load ----
        mov dword [tls_idx], 0
.tst_loop:
        mov eax, [tls_idx]
        cmp eax, [lib_module_count]
        jae .tst_done
        imul eax, eax, LIB_MOD_SIZE
        add eax, lib_modules
        mov [lib_rec], eax
        cmp dword [eax + LIB_MOD_TLS], 0
        je .tst_next
        mov eax, [lib_rec]
        mov ecx, [eax + LIB_MOD_TLS_SIZE]
        mov [tls_size], ecx
        mov eax, [lib_rec]
        mov ecx, [eax + LIB_MOD_TLS_DATA]
        mov edx, [eax + LIB_MOD_BASE]
        add ecx, edx                    ; the template, through the module's base
        mov [tls_tmpl_addr], ecx
        mov eax, [lib_rec]
        mov ecx, [eax + LIB_MOD_TLS_BASE]
        mov edx, [eax + LIB_MOD_BASE]
        add ecx, edx                    ; the base slot, likewise
        mov [tls_slot_addr], ecx
        mov dword [tls_write_slot], 1
        call tls_add_block
.tst_next:
        inc dword [tls_idx]
        jmp .tst_loop

.tst_done:
        mov eax, [tls_mapped]
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; tls_thread_setup - give a new thread blocks of its own
;   In : EDI = the new thread's task id
;   Out: EAX = blocks mapped
;
; The modules are the ones the calling thread already has - a thread is created
; inside a process, so the image and its libraries are settled - so the current
; thread's view is copied entry by entry and only the block is new. Every block
; is filled from the same template, which is why a thread starts with the
; template's values and cannot see another thread's writes.
;
; The slots are deliberately left alone: they belong to whichever thread is
; running, and the caller is about to get them back. The new thread's view is
; installed the first time it is switched to.
;-----------------------------------------------------------------------
tls_thread_setup:
        mov [tls_new_pid], edi
        pushad
        call sched_get_current_task
        test eax, eax
        jz .tts_done
        mov [tls_parent_tcb], eax
        mov edi, [tls_new_pid]
        call tls_begin
        jc .tts_done
        mov dword [tls_idx], 0
.tts_loop:
        mov edi, [tls_parent_tcb]
        mov ecx, [tls_idx]
        cmp ecx, [edi + TCB_TLS_COUNT]
        jae .tts_done
        imul edx, ecx, TLS_VIEW_SIZE
        lea esi, [edi + TCB_TLS_VIEW]
        add esi, edx
        mov eax, [esi + 0]
        mov [tls_slot_addr], eax
        mov eax, [esi + 8]
        mov [tls_tmpl_addr], eax
        mov eax, [esi + 12]
        mov [tls_size], eax
        mov dword [tls_write_slot], 0
        call tls_add_block
        inc dword [tls_idx]
        jmp .tts_loop
.tts_done:
        mov eax, [tls_mapped]
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; tls_begin - start a task's view and set up the block allocator
;   In : EDI = task id
;   Out: CF clear on success; [tls_tcb], [tls_pd], [tls_top] ready
;-----------------------------------------------------------------------
tls_begin:
        push eax
        push ebx
        push edi
        mov dword [tls_mapped], 0
        mov [tls_pid], edi
        cmp edi, MAX_TASKS
        jae .tb_fail
        imul eax, edi, TCB_SIZE
        add eax, task_table
        mov [tls_tcb], eax
        mov ebx, [eax + TCB_CR3]
        test ebx, ebx
        jz .tb_fail
        mov [tls_pd], ebx
        mov dword [eax + TCB_TLS_COUNT], 0
        ; Blocks come down from the top of the demand zone, and a process's
        ; threads share one address space, so the top is the group's, not the
        ; task's: it lives in the leader's TCB and every thread's allocation
        ; continues below the last one.
        mov ecx, [eax + TCB_TGID]
        cmp ecx, MAX_TASKS
        jae .tb_fail
        imul ecx, ecx, TCB_SIZE
        add ecx, task_table
        mov [tls_group_tcb], ecx
        mov ebx, [ecx + TCB_TLS_TOP]
        test ebx, ebx
        jnz .tb_have_top
        mov ebx, USER_DEMAND_MAX
.tb_have_top:
        mov [tls_top], ebx
        clc
        jmp .tb_out
.tb_fail:
        stc
.tb_out:
        pop edi
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; tls_add_block - one module's block for the task being set up
;   In : [tls_tcb], [tls_pd], [tls_top]
;        [tls_size], [tls_tmpl_addr], [tls_slot_addr], [tls_write_slot]
;   Out: CF clear on success, and the view gains an entry
;
; The area is recorded before its pages are mapped, so the failure path has one
; way to give everything back: vm_unrecord_for_pid releases whatever pages were
; mapped and clears the record.
;-----------------------------------------------------------------------
tls_add_block:
        pushad
        mov dword [tls_base], 0
        mov eax, [tls_size]
        test eax, eax
        jz .tab_fail
        add eax, 0xFFF
        shr eax, 12
        mov [tls_pages], eax

        ; Where it goes: down from the top of the demand zone. Images are drawn
        ; in the first 256 MB of it (hbx_choose_base), so the two can never
        ; meet, and blocks stack without overlapping each other.
        mov ecx, eax
        shl ecx, 12                     ; bytes this block needs
        mov eax, [tls_top]
        sub eax, ecx
        cmp eax, [user_demand_base]
        jb .tab_fail
        mov [tls_top], eax
        mov [tls_base], eax
        ; Remember the new top where the whole group can see it.
        mov ebx, [tls_group_tcb]
        mov [ebx + TCB_TLS_TOP], eax

        ; Record first: from here on, failure is one call.
        mov eax, [tls_base]
        mov ecx, [tls_pages]
        mov edx, PROT_READ | PROT_WRITE
        mov ebx, [tls_tcb]
        mov edi, [ebx + TCB_TGID]
        call vm_record_for_pid
        test eax, eax
        jz .tab_fail

        mov dword [tls_page], 0
.tab_page:
        mov eax, [tls_page]
        cmp eax, [tls_pages]
        jae .tab_view

        call pmm_alloc_page
        test eax, eax
        jz .tab_unmap
        mov [tls_frame], eax

        ; Zero it, then copy this page of the template over the front of it.
        mov edi, eax
        xor eax, eax
        mov ecx, 1024
        cld
        rep stosd

        mov eax, [tls_page]
        shl eax, 12
        mov ecx, [tls_size]
        sub ecx, eax
        jbe .tab_map_now
        cmp ecx, 4096
        jbe .tab_copy
        mov ecx, 4096
.tab_copy:
        ; The template is read through the task's own page tables: at launch
        ; this runs in the shell's address space, where a mapped image is not
        ; visible at all, and a physical frame is.
        mov ebx, [tls_tmpl_addr]
        add ebx, eax
        mov eax, [tls_pd]
        push ecx
        call vm_pte_phys
        pop ecx
        test eax, eax
        jz .tab_map_now                 ; template page missing: leave zeroes
        and ebx, 0xFFF
        add eax, ebx
        mov esi, eax
        mov edi, [tls_frame]
        rep movsb

.tab_map_now:
        mov eax, [tls_page]
        shl eax, 12
        add eax, [tls_base]
        mov ebx, [tls_frame]
        mov ecx, PG_PRESENT | PG_WRITABLE | PG_USER
        mov edi, [tls_pd]
        call paging_map_page_at
        inc dword [tls_page]
        jmp .tab_page

.tab_view:
        ; Remember what to write on every switch, and what a thread created
        ; later has to copy its block from.
        mov eax, [tls_tcb]
        mov ecx, [eax + TCB_TLS_COUNT]
        cmp ecx, TLS_VIEW_MAX
        jae .tab_slot
        imul edx, ecx, TLS_VIEW_SIZE
        lea edi, [eax + TCB_TLS_VIEW]
        add edi, edx
        mov eax, [tls_slot_addr]
        mov [edi + 0], eax
        mov eax, [tls_base]
        mov [edi + 4], eax
        mov eax, [tls_tmpl_addr]
        mov [edi + 8], eax
        mov eax, [tls_size]
        mov [edi + 12], eax
        mov eax, [tls_tcb]
        inc dword [eax + TCB_TLS_COUNT]

.tab_slot:
        cmp dword [tls_write_slot], 0
        je .tab_count
        mov ebx, [tls_slot_addr]
        test ebx, ebx
        jz .tab_count
        mov edx, [tls_base]
        call tls_write_task_u32

.tab_count:
        inc dword [tls_mapped]
        clc
        jmp .tab_out

.tab_unmap:
        mov eax, [tls_pd]
        mov ebx, [tls_base]
        mov edi, [tls_tcb]
        mov edi, [edi + TCB_TGID]
        call vm_unrecord_for_pid
.tab_fail:
        stc
.tab_out:
        popad
        ret

;-----------------------------------------------------------------------
; tls_apply_view - install the incoming thread's base slots
;   In : EDX = that thread's TCB
;   Out: nothing; the task's page directory must already be active
;
; Called by sched_apply_cr3, which is the one door every path into a task goes
; through. A slot whose page is not mapped is skipped: the task has not been
; given its image yet, or has just replaced it.
;-----------------------------------------------------------------------
tls_apply_view:
        pushad
        mov [tls_view_tcb], edx
        mov eax, [edx + TCB_CR3]
        test eax, eax
        jz .tav_done
        mov [tls_pd], eax
        mov dword [tls_view_i], 0
.tav_loop:
        mov edx, [tls_view_tcb]
        mov ecx, [tls_view_i]
        cmp ecx, [edx + TCB_TLS_COUNT]
        jae .tav_done
        imul esi, ecx, TLS_VIEW_SIZE
        lea eax, [edx + TCB_TLS_VIEW]
        add esi, eax
        mov ebx, [esi + 0]
        test ebx, ebx
        jz .tav_next
        mov edx, [esi + 4]
        test edx, edx
        jz .tav_next
        call tls_write_task_u32
.tav_next:
        inc dword [tls_view_i]
        jmp .tav_loop
.tav_done:
        popad
        ret

;-----------------------------------------------------------------------
; tls_release_thread - give back one thread's blocks
;   In : EDI = that thread's TCB
;
; Called when a thread ends without ending its process, and for every thread a
; process teardown sweeps up. The records live in the group's table, so they are
; found by their block address rather than by the thread's own id.
;-----------------------------------------------------------------------
tls_release_thread:
        pushad
        mov [tls_view_tcb], edi
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz .trt_clear
        mov [tls_pd], eax
        mov dword [tls_view_i], 0
.trt_loop:
        mov edi, [tls_view_tcb]
        mov ecx, [tls_view_i]
        cmp ecx, [edi + TCB_TLS_COUNT]
        jae .trt_clear
        imul edx, ecx, TLS_VIEW_SIZE
        lea esi, [edi + TCB_TLS_VIEW]
        add esi, edx
        mov ebx, [esi + 4]
        test ebx, ebx
        jz .trt_next
        mov eax, [tls_pd]
        mov edi, [edi + TCB_TGID]
        call vm_unrecord_for_pid
.trt_next:
        inc dword [tls_view_i]
        jmp .trt_loop
.trt_clear:
        mov edi, [tls_view_tcb]
        mov dword [edi + TCB_TLS_COUNT], 0
        popad
        ret

;-----------------------------------------------------------------------
; tls_write_task_u32 - write a dword into a task's address space
;   In : EBX = address, EDX = value, [tls_pd] = that task's page directory
;   Out: CF set if the page is not mapped
;
; The same rule the relocation path follows: a cached window address is
; identity-mapped, so the write is direct; anything else is translated and the
; frame is written through the kernel's identity map.
;-----------------------------------------------------------------------
tls_write_task_u32:
        push eax
        push ebx
        push ecx
        push edx
        cmp ebx, PROGRAM_BASE
        jb .twt_translate
        cmp ebx, PROGRAM_END_ADDR
        jae .twt_translate
        mov [ebx], edx
        clc
        jmp .twt_out
.twt_translate:
        mov ecx, ebx
        and ecx, 0xFFF
        and ebx, ~0xFFF
        mov eax, [tls_pd]
        call vm_pte_phys
        test eax, eax
        jz .twt_fail
        add eax, ecx
        mov [eax], edx
        clc
        jmp .twt_out
.twt_fail:
        stc
.twt_out:
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; TLS STATE
;=======================================================================
tls_pid:            dd 0
tls_pd:             dd 0
tls_size:           dd 0
tls_tmpl_addr:      dd 0
tls_slot_addr:      dd 0
tls_write_slot:     dd 0
tls_pages:          dd 0
tls_page:           dd 0
tls_frame:          dd 0
tls_base:           dd 0
tls_mapped:         dd 0
tls_idx:            dd 0
tls_top:            dd 0
tls_tcb:            dd 0
tls_group_tcb:      dd 0
tls_new_pid:        dd 0
tls_parent_tcb:     dd 0
tls_view_tcb:       dd 0
tls_view_i:         dd 0

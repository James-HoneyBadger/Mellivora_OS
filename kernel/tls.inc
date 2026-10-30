;=======================================================================
; tls.inc - per-module thread-local storage (EXEC.md section 6)
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
;   * The **block** needs the task, because it lives in the task's own address
;     space. It is one page-rounded allocation per module, filled from the
;     template and zeroed past it, mapped where the next random draw puts it,
;     and recorded as a vm area so fork copies it copy-on-write and exit
;     releases it with every other mapping. The address goes into base_rva,
;     which is how a module finds its own storage: `[base_rva]` is the block,
;     read once and kept in a register.
;
; A thread that does not need TLS never pays for it: the slot is written only
; for modules that declared a directory.
;=======================================================================

;-----------------------------------------------------------------------
; tls_setup_task - give a launched task its thread-local blocks
;   In : EDI = task id
;   Out: EAX = blocks mapped
;
; Called once the task exists, next to lib_map_modules and for the same reason.
; A block that cannot be mapped is reported and skipped: the module's base slot
; stays zero, and a program that reads it without checking faults inside its own
; task, which the fault path contains.
;-----------------------------------------------------------------------
tls_setup_task:
        pushad
        mov dword [tls_mapped], 0
        mov [tls_pid], edi
        mov eax, USER_DEMAND_MAX
        mov [tls_top], eax
        mov eax, edi
        cmp eax, MAX_TASKS
        jae .tst_done
        imul eax, TCB_SIZE
        add eax, task_table
        mov eax, [eax + TCB_CR3]
        test eax, eax
        jz .tst_done
        mov [tls_pd], eax

        ; ---- the main image, module 0 ----
        mov eax, [hbx_main_tls_dir]
        test eax, eax
        jz .tst_libs
        mov eax, [hbx_main_tls_size]
        mov [tls_size], eax
        mov eax, [hbx_main_tls_data]
        mov [tls_data], eax
        mov eax, [hbx_main_tls_base_rva]
        mov [tls_base_rva], eax
        mov eax, [hbx_main_base]
        mov [tls_img_base], eax
        call tls_map_block
        jc .tst_libs

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
        ; A library's base slot is written where the library was mapped, so the
        ; write goes through the same page-table translation as a relocation.
        mov eax, [lib_rec]
        mov ecx, [eax + LIB_MOD_TLS_SIZE]
        mov [tls_size], ecx
        mov eax, [lib_rec]
        mov ecx, [eax + LIB_MOD_TLS_DATA]
        mov [tls_data], ecx
        mov eax, [lib_rec]
        mov ecx, [eax + LIB_MOD_TLS_BASE]
        mov [tls_base_rva], ecx
        mov eax, [lib_rec]
        mov ecx, [eax + LIB_MOD_BASE]
        mov [tls_img_base], ecx
        call tls_map_block
.tst_next:
        inc dword [tls_idx]
        jmp .tst_loop

.tst_done:
        mov eax, [tls_mapped]
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; tls_map_block - one module's block, in the task's address space
;   In : [tls_size], [tls_data] (RVA), [tls_base_rva], [tls_img_base], [tls_pd]
;   Out: CF clear on success
;-----------------------------------------------------------------------
tls_map_block:
        pushad
        mov eax, [tls_size]
        test eax, eax
        jz .tmb_fail
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
        jb .tmb_fail
        mov [tls_top], eax
        mov [tls_base], eax

        mov dword [tls_page], 0
.tmb_page:
        mov eax, [tls_page]
        cmp eax, [tls_pages]
        jae .tmb_write

        call pmm_alloc_page
        test eax, eax
        jz .tmb_unmap
        mov [tls_frame], eax

        ; Zero it, then copy the template over the front of it.
        mov edi, eax
        xor eax, eax
        mov ecx, 1024
        cld
        rep stosd

        mov eax, [tls_page]
        shl eax, 12
        mov ecx, [tls_size]
        sub ecx, eax
        jbe .tmb_map_now
        cmp ecx, 4096
        jbe .tmb_copy
        mov ecx, 4096
.tmb_copy:
        mov esi, [tls_img_base]
        add esi, [tls_data]
        add esi, eax
        mov edi, [tls_frame]
        rep movsb

.tmb_map_now:
        mov eax, [tls_page]
        shl eax, 12
        add eax, [tls_base]
        mov ebx, [tls_frame]
        mov ecx, PG_PRESENT | PG_WRITABLE | PG_USER
        mov edi, [tls_pd]
        call paging_map_page_at
        inc dword [tls_page]
        jmp .tmb_page

.tmb_write:
        ; base_rva holds this thread's block, so a module can find its own.
        mov ebx, [tls_img_base]
        add ebx, [tls_base_rva]
        mov edx, [tls_base]
        call tls_write_task_u32
        jc .tmb_unmap

        mov eax, [tls_base]
        mov ecx, [tls_pages]
        mov edx, PROT_READ | PROT_WRITE
        mov edi, [tls_pid]
        call vm_record_for_pid
        test eax, eax
        jz .tmb_unmap
        inc dword [tls_mapped]
        clc
        jmp .tmb_out

.tmb_fail:
.tmb_unmap:
        stc
.tmb_out:
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
tls_data:           dd 0
tls_base_rva:       dd 0
tls_img_base:       dd 0
tls_pages:          dd 0
tls_page:           dd 0
tls_frame:          dd 0
tls_base:           dd 0
tls_mapped:         dd 0
tls_idx:            dd 0
tls_top:            dd 0

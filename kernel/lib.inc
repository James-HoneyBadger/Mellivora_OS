;=======================================================================
; lib.inc - shared libraries: finding one, and binding an import to it
;   (docs/spec/EXEC.md sections 3, 6 and 8)
;
; M5's contract is that a program built against a library calls into it. That
; takes three steps in order:
;
;   1. Resolve. For every import descriptor in a program, find the module's
;      image, verify it, and look each symbol up in its export table. This runs
;      during loading, before a process exists, so a missing module or symbol
;      is an ordinary loader refusal (EXEC.md section 3 step 5) and there is
;      nothing to unwind.
;
;   2. Patch. The resolved address goes into the program's import address table
;      *in the file copy still sitting at PROGRAM_BASE*. Both roads out of the
;      loader - the copy into the window and the mapping at a random base -
;      then carry the resolved IAT with them, so this file needs no page-table
;      code at all.
;
;   3. Map. Once the task exists and has a page directory, each library is
;      mapped into that task's own address space at the base step 1 chose, so
;      two processes get two copies and neither can write into the other's.
;      The mapping is the same hbx_map_task_image the main image uses, with
;      [hbx_src_base] pointing at the staging slot instead of the window.
;
; A library is a relocatable HBX image marked `dll`. It must be relocatable
; because it is mapped wherever the loader chooses, and it may not import
; itself yet: binding a library's own imports needs a resolution order
; (EXEC.md section 6's delay-loaded story), which is a later increment.
;
; Staging: a library is read into one of LIB_MAX_MODULES fixed slots at the top
; of the program window. Slots rather than one buffer because every module is
; resolved before any of them is mapped, and the mapping needs the bytes again
; once the task exists.
;
; The program's own numbers - its header size, file length and import table -
; are copied into lib_prog_* at entry, because hbx_verify overwrites the shared
; hbx_* statics with the library's every time a module is loaded.
;=======================================================================

LIB_NAME_LEN    equ 32

; One record per module the program imports.
LIB_MOD_NAME    equ 0                   ; LIB_NAME_LEN bytes, NUL-terminated
LIB_MOD_SLOT    equ 32                  ; where the file was staged
LIB_MOD_LEN     equ 36                  ; bytes read
LIB_MOD_BASE    equ 40                  ; the base chosen for this launch
LIB_MOD_DELTA   equ 44                  ; base - preferred base
LIB_MOD_HDR     equ 48                  ; header_size
LIB_MOD_PAYLOAD equ 52                  ; payload length
LIB_MOD_IMG     equ 56                  ; image size
LIB_MOD_RELOC   equ 60                  ; relocation table RVA
LIB_MOD_EXPORT  equ 64                  ; export table RVA
LIB_MOD_TLS     equ 68                  ; TLS directory RVA, 0 if none
LIB_MOD_TLS_SIZE equ 72                 ; initialised bytes in the template
LIB_MOD_TLS_DATA equ 76                 ; template RVA
LIB_MOD_TLS_BASE equ 80                 ; where this thread's block goes
LIB_MOD_SIZE    equ 88

;-----------------------------------------------------------------------
; lib_resolve_imports - bind a program's imports to real library images
;   In : the statics hbx_verify filled, the file at [hbx_src_base]
;   Out: CF clear, and lib_module_count records how many to map
;        CF set - the refusal is already reported and EAX is an mv_status
;-----------------------------------------------------------------------
lib_resolve_imports:
        pushad
        mov dword [lib_module_count], 0
        mov dword [lib_ret], 0

        ; What is true of the *program*, before any library overwrites it.
        ; Saved even when there is nothing to resolve: the restore at the end
        ; runs either way, and a program without imports still needs its own
        ; description back.
        mov eax, [hbx_import_count]
        mov [lib_prog_imp_count], eax
        mov eax, [hbx_import_rva]
        mov [lib_prog_imp_rva], eax
        mov eax, [hbx_hdr_size]
        mov [lib_prog_hdr], eax
        mov eax, [hbx_file_len]
        mov [lib_prog_len], eax
        mov eax, [hbx_src_base]
        mov [lib_prog_base], eax
        mov eax, [hbx_payload_len]
        mov [lib_prog_payload], eax
        mov eax, [hbx_img_size]
        mov [lib_prog_img], eax
        mov eax, [hbx_reloc_rva]
        mov [lib_prog_reloc], eax
        mov eax, [hbx_image_base]
        mov [lib_prog_ibase], eax
        mov eax, [hbx_entry]
        mov [lib_prog_entry], eax
        mov eax, [hbx_export_count]
        mov [lib_prog_exp_count], eax
        mov eax, [hbx_export_rva]
        mov [lib_prog_exp_rva], eax
        mov eax, [hbx_tls_dir]
        mov [hbx_main_tls_dir], eax
        mov eax, [hbx_tls_size]
        mov [hbx_main_tls_size], eax
        mov eax, [hbx_tls_data]
        mov [hbx_main_tls_data], eax
        mov eax, [hbx_tls_base_rva]
        mov [hbx_main_tls_base_rva], eax

        cmp dword [lib_prog_imp_count], 0
        jz .lri_out

        ; The staging area sits below the stack guard, so an image that reaches
        ; it leaves the loader nowhere to put a library.
        mov eax, [hbx_img_size]
        add eax, PROGRAM_BASE
        jc .lri_noroom
        cmp eax, LIB_STAGE_BASE
        ja .lri_noroom

        mov dword [lib_desc_idx], 0
.lri_desc:
        mov eax, [lib_desc_idx]
        cmp eax, [lib_prog_imp_count]
        jae .lri_done
        cmp eax, LIB_MAX_MODULES
        jae .lri_toomany

        ; descriptor = program's RVA 0 + import_rva + index * 12
        imul eax, eax, 12
        add eax, [lib_prog_imp_rva]
        add eax, [lib_prog_hdr]
        add eax, [lib_prog_base]
        mov [lib_desc_ptr], eax

        ; The module name, into a buffer: libraries are verified from staging
        ; slots, which must not disturb anything this walk still needs.
        mov eax, [lib_desc_ptr]
        mov esi, [eax]                  ; module name RVA
        add esi, [lib_prog_base]
        add esi, [lib_prog_hdr]
        mov edi, lib_name_buf
        call lib_copy_name

        ; The thunk array is named now: if the module itself is missing, the
        ; refusal still has a symbol to name (EXEC.md section 8).
        mov eax, [lib_desc_ptr]
        mov esi, [eax + 4]              ; thunk array RVA
        add esi, [lib_prog_base]
        add esi, [lib_prog_hdr]
        mov [lib_thunk_ptr], esi
        mov eax, [eax + 8]              ; thunk count
        mov [lib_thunk_left], eax

        call lib_load_module            ; EAX = module index, CF on refusal
        jc .lri_fail

.lri_thunk:
        mov ecx, [lib_thunk_left]
        test ecx, ecx
        jz .lri_next_desc

        mov esi, [lib_thunk_ptr]
        mov eax, [esi + 4]              ; IAT slot RVA, in the program
        mov [lib_iat_rva], eax

        mov eax, [esi]                  ; symbol name RVA
        add eax, [lib_prog_base]
        add eax, [lib_prog_hdr]
        mov esi, eax
        call lib_find_export            ; EAX = export RVA, CF if not exported
        jc .lri_missing

        ; The slot lives in the program's file copy, header_size bytes above
        ; where the image will be after loading. Both roads copy that byte.
        mov ecx, [lib_iat_rva]
        add ecx, [lib_prog_hdr]
        cmp ecx, [lib_prog_len]
        jae .lri_bad
        mov edx, [lib_module_index]
        imul edx, edx, LIB_MOD_SIZE
        add edx, lib_modules
        mov edx, [edx + LIB_MOD_BASE]
        add eax, edx
        mov ebx, [lib_prog_base]
        mov [ebx + ecx], eax

        add dword [lib_thunk_ptr], 8
        dec dword [lib_thunk_left]
        jmp .lri_thunk

.lri_next_desc:
        inc dword [lib_desc_idx]
        jmp .lri_desc

.lri_done:
        mov dword [lib_ret], 1
        clc
        jmp .lri_out

        ; ---- refusals (EXEC.md section 8) ----
.lri_missing:
        ; Module and symbol, named - the sentence section 8 promises.
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_bang
        call vga_print
        mov esi, [lib_thunk_ptr]
        mov esi, [esi]
        add esi, [lib_prog_base]
        add esi, [lib_prog_hdr]
        mov edi, lib_symbol_buf
        call lib_copy_name
        mov esi, lib_symbol_buf
        call vga_print
        mov esi, msg_lib_not_found_tail
        call vga_print
        mov eax, MV_DLL_NOT_FOUND
        jmp .lri_fail
.lri_noroom:
        mov esi, msg_lib_noroom
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .lri_fail
.lri_toomany:
        mov esi, msg_lib_toomany
        call vga_print
        mov eax, MV_DLL_NOT_FOUND
        jmp .lri_fail
.lri_bad:
        mov esi, msg_lib_bad
        call vga_print
        mov eax, MV_BAD_IMAGE
.lri_fail:
        stc
.lri_out:
        call lib_restore_program
        mov eax, [lib_ret]
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; lib_restore_program - put the program's description back in hbx_*
;
; Every library load runs hbx_verify, which overwrites the statics that
; describe the image being loaded. The caller - hbx_prepare - is only half way
; through using them, so they are handed back here whether the resolution
; succeeded or not.
;-----------------------------------------------------------------------
lib_restore_program:
        push eax
        mov eax, [lib_prog_base]
        mov [hbx_src_base], eax
        mov eax, [lib_prog_len]
        mov [hbx_file_len], eax
        mov eax, [lib_prog_hdr]
        mov [hbx_hdr_size], eax
        mov eax, [lib_prog_payload]
        mov [hbx_payload_len], eax
        mov eax, [lib_prog_img]
        mov [hbx_img_size], eax
        mov eax, [lib_prog_reloc]
        mov [hbx_reloc_rva], eax
        mov eax, [lib_prog_ibase]
        mov [hbx_image_base], eax
        mov eax, [lib_prog_entry]
        mov [hbx_entry], eax
        mov eax, [lib_prog_imp_count]
        mov [hbx_import_count], eax
        mov eax, [lib_prog_imp_rva]
        mov [hbx_import_rva], eax
        mov eax, [lib_prog_exp_count]
        mov [hbx_export_count], eax
        mov eax, [lib_prog_exp_rva]
        mov [hbx_export_rva], eax
        pop eax
        ret

;-----------------------------------------------------------------------
; lib_load_module - find, verify and record one module
;   In : lib_name_buf = the module name
;   Out: EAX = its record index, CF clear; hbx_src_base is the program again
;        CF set - the refusal is already reported
;
; A module already loaded for this program is reused rather than read twice, so
; two descriptors naming one library map it once (EXEC.md section 6, "one
; mapping per image per process").
;-----------------------------------------------------------------------
lib_load_module:
        pushad
        mov dword [lib_module_index], -1

        ; Already loaded?
        xor ecx, ecx
.lm_seen:
        cmp ecx, [lib_module_count]
        jae .lm_new
        push ecx
        imul eax, ecx, LIB_MOD_SIZE
        add eax, lib_modules
        lea edi, [eax + LIB_MOD_NAME]
        mov esi, lib_name_buf
        call lib_str_equal
        pop ecx
        jc .lm_reuse
        inc ecx
        jmp .lm_seen

.lm_reuse:
        ; Republish its statics: the export lookup and the mapping both read
        ; them, and loading another module in between would have moved them.
        mov [lib_module_index], ecx
        imul eax, ecx, LIB_MOD_SIZE
        add eax, lib_modules
        mov [lib_rec], eax
        call lib_publish_record
        jmp .lm_done

.lm_new:
        mov eax, [lib_module_count]
        cmp eax, LIB_MAX_MODULES
        jae .lm_toomany
        imul eax, eax, LIB_MOD_SIZE
        add eax, lib_modules
        mov [lib_rec], eax
        mov dword [lib_retry], 0

        ; slot = LIB_STAGE_BASE + index * LIB_SLOT_SIZE
        mov eax, [lib_module_count]
        imul eax, eax, LIB_SLOT_SIZE
        add eax, LIB_STAGE_BASE
        mov [lib_slot], eax

        ; The image is <name>.hbl in /lib, then in the root.
        mov esi, msg_lib_dir
        call lib_build_path
        mov esi, lib_path_buf
        mov edi, [lib_slot]
        call hbfs_read_file
        jnc .lm_read
        mov esi, msg_lib_slash
        call lib_build_path
        mov esi, lib_path_buf
        mov edi, [lib_slot]
        call hbfs_read_file
        jc .lm_missing
.lm_read:
        cmp ecx, LIB_SLOT_SIZE
        ja .lm_big
        mov [lib_len], ecx

        ; Verify it exactly as a program is verified, from its slot.
        mov eax, [lib_slot]
        mov [hbx_src_base], eax
        mov eax, [lib_len]
        call hbx_verify
        jc .lm_fail                     ; the reason and status are already out

        ; A module the program imports has to be a library, has to be
        ; relocatable, and may not import anything itself yet.
        mov ebx, [hbx_src_base]
        test dword [ebx + HBX_OFF_FLAGS], HBX_FLAG_DLL
        jz .lm_notlib
        cmp dword [hbx_reloc_rva], 0
        je .lm_noreloc
        cmp dword [hbx_import_count], 0
        jne .lm_imports

        call hbx_choose_base            ; EAX = this launch's base for it
        mov [lib_base], eax

        ; Two modules may not land on each other: both are 64 KB aligned, so
        ; the distance between the two bases is what has to exceed the image.
        xor ecx, ecx
.lm_overlap:
        cmp ecx, [lib_module_count]
        jae .lm_place
        imul edx, ecx, LIB_MOD_SIZE
        add edx, lib_modules
        mov edx, [edx + LIB_MOD_BASE]
        mov esi, [lib_base]
        mov edi, [hbx_img_size]
        sub esi, edx
        jae .lm_ov_abs
        neg esi
.lm_ov_abs:
        cmp esi, edi
        jb .lm_again
        inc ecx
        jmp .lm_overlap
.lm_again:
        inc dword [lib_retry]
        cmp dword [lib_retry], 8
        jae .lm_noroom
        call hbx_choose_base
        mov [lib_base], eax
        jmp .lm_overlap

.lm_place:
        ; Record it, then put the program's statics back for the caller.
        mov edi, [lib_rec]
        lea edi, [edi + LIB_MOD_NAME]
        mov esi, lib_name_buf
        call lib_copy_name
        mov edi, [lib_rec]
        mov eax, [lib_slot]
        mov [edi + LIB_MOD_SLOT], eax
        mov eax, [lib_len]
        mov [edi + LIB_MOD_LEN], eax
        mov eax, [lib_base]
        mov [edi + LIB_MOD_BASE], eax
        sub eax, [hbx_image_base]
        mov [edi + LIB_MOD_DELTA], eax
        mov eax, [hbx_hdr_size]
        mov [edi + LIB_MOD_HDR], eax
        mov eax, [hbx_payload_len]
        mov [edi + LIB_MOD_PAYLOAD], eax
        mov eax, [hbx_img_size]
        mov [edi + LIB_MOD_IMG], eax
        mov eax, [hbx_reloc_rva]
        mov [edi + LIB_MOD_RELOC], eax
        mov eax, [hbx_export_rva]
        mov [edi + LIB_MOD_EXPORT], eax

        mov eax, [lib_module_count]
        mov [lib_module_index], eax
        inc dword [lib_module_count]
        call lib_publish_record

.lm_done:
        mov eax, [lib_module_index]
        mov [esp + 28], eax
        popad
        clc
        ret

.lm_missing:
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_bang
        call vga_print
        mov esi, [lib_thunk_ptr]
        mov esi, [esi]
        add esi, [lib_prog_base]
        add esi, [lib_prog_hdr]
        mov edi, lib_symbol_buf
        call lib_copy_name
        mov esi, lib_symbol_buf
        call vga_print
        mov esi, msg_lib_not_found_tail
        call vga_print
        mov eax, MV_DLL_NOT_FOUND
        jmp .lm_fail
.lm_big:
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_too_big
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .lm_fail
.lm_notlib:
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_notlib
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .lm_fail
.lm_noreloc:
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_noreloc
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .lm_fail
.lm_imports:
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_imports
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .lm_fail
.lm_noroom:
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_noroom_tail
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .lm_fail
.lm_toomany:
        mov esi, msg_lib_toomany
        call vga_print
        mov eax, MV_DLL_NOT_FOUND
.lm_fail:
        ; The caller keeps reading the program's file: leave the shared
        ; statics describing it, not the library.
        mov dword [hbx_src_base], PROGRAM_BASE
        stc
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; lib_publish_record - make one module record describe hbx_* again
;   In : [lib_rec] = record
;-----------------------------------------------------------------------
lib_publish_record:
        push eax
        push ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_SLOT]
        mov [hbx_src_base], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_LEN]
        mov [hbx_file_len], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_HDR]
        mov [hbx_hdr_size], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_PAYLOAD]
        mov [hbx_payload_len], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_IMG]
        mov [hbx_img_size], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_RELOC]
        mov [hbx_reloc_rva], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_BASE]
        mov [hbx_map_base], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_DELTA]
        mov [hbx_map_delta], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_EXPORT]
        mov [lib_export_rva], ebx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; lib_reload_module - read one module again and publish its description
;   In : [lib_rec] = record
;   Out: CF clear - hbx_* describes the module at its staging slot
;        CF set - reported, and the caller skips this module
;
; The resolve pass leaves the file in a staging slot, but the staging area sits
; in the program window above the image and the window is cleared between
; loading and the launch (a new task must not see the last program's heap). The
; file is the authority, so it is read again here - after the clear and before
; the mapping - rather than trusting bytes that were true a moment ago.
;-----------------------------------------------------------------------
lib_reload_module:
        pushad
        mov dword [lib_ret], 0

        mov edi, [lib_rec]
        lea esi, [edi + LIB_MOD_NAME]
        mov edi, lib_name_buf
        call lib_copy_name

        mov esi, msg_lib_dir
        call lib_build_path
        mov esi, lib_path_buf
        mov edi, [lib_rec]
        mov edi, [edi + LIB_MOD_SLOT]
        call hbfs_read_file
        jnc .lrm_read
        mov esi, msg_lib_slash
        call lib_build_path
        mov esi, lib_path_buf
        mov edi, [lib_rec]
        mov edi, [edi + LIB_MOD_SLOT]
        call hbfs_read_file
        jc .lrm_missing
.lrm_read:
        mov [lib_len], ecx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_SLOT]
        mov [hbx_src_base], ebx
        mov eax, [lib_len]
        call hbx_verify
        jc .lrm_fail                    ; the reason and status are already out

        ; Its thread-local index is this module's slot, written into the copy
        ; that is about to be mapped: the reload above started from the file,
        ; so the index has to be written after it, not before.
        mov eax, [hbx_tls_dir]
        test eax, eax
        jz .lrm_no_tls
        mov ecx, [hbx_tls_index_rva]
        add ecx, [hbx_hdr_size]
        cmp ecx, [hbx_file_len]
        jae .lrm_fail
        mov ebx, [hbx_src_base]
        mov edx, [lib_map_idx]
        inc edx                         ; module 0 is the program itself
        mov [ebx + ecx], edx
        mov eax, [lib_rec]
        mov ebx, [hbx_tls_dir]
        mov [eax + LIB_MOD_TLS], ebx
        mov ebx, [hbx_tls_size]
        mov [eax + LIB_MOD_TLS_SIZE], ebx
        mov ebx, [hbx_tls_data]
        mov [eax + LIB_MOD_TLS_DATA], ebx
        mov ebx, [hbx_tls_base_rva]
        mov [eax + LIB_MOD_TLS_BASE], ebx
        jmp .lrm_have_tls
.lrm_no_tls:
        mov eax, [lib_rec]
        mov dword [eax + LIB_MOD_TLS], 0
.lrm_have_tls:

        ; The base was drawn and the IAT patched in the resolve pass; the
        ; mapping has to land on that base or every resolved address is wrong.
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_BASE]
        mov [hbx_map_base], ebx
        mov eax, [lib_rec]
        mov ebx, [eax + LIB_MOD_DELTA]
        mov [hbx_map_delta], ebx
        mov dword [lib_ret], 1
        clc
        jmp .lrm_out

.lrm_missing:
        mov esi, msg_lib_prefix
        call vga_print
        mov esi, lib_name_buf
        call vga_print
        mov esi, msg_lib_no_module
        call vga_print
        jmp .lrm_fail
.lrm_fail:
        ; Only a failed module hands the window back: the mapping that follows a
        ; good one reads the stage (hbx_src_base) and must not find it reset.
        mov dword [hbx_src_base], PROGRAM_BASE
        stc
.lrm_out:
        mov eax, [lib_ret]
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; lib_find_export - look a symbol up in the module being resolved
;   In : ESI = symbol name, in the program's file
;   Out: EAX = RVA of the export, CF clear; CF set - not exported
;-----------------------------------------------------------------------
lib_find_export:
        pushad
        ; Ask for the module's own description first. lib_load_module restores the
        ; program's hbx_* on the way out (so the import walk can carry on), which
        ; leaves hbx_src_base and hbx_hdr_size naming the *program* while
        ; lib_export_rva names the library. For a relocatable program the library
        ; is published again at map time and the two agree; a fixed-base image is
        ; never mapped, so the export table was read at program-base + the
        ; library's export RVA - nothing - and every import from a library failed
        ; with "not found" for a symbol that was exported.
        call lib_publish_record
        mov [lib_sym_ptr], esi
        mov ebx, [hbx_src_base]
        add ebx, [hbx_hdr_size]         ; RVA 0 of the library
        mov [lib_rva0], ebx
        mov eax, [lib_export_rva]
        test eax, eax
        jz .lfe_no
        add eax, ebx
        mov edx, [eax]                  ; count of {name_rva, rva}
        add eax, 4
        mov [lib_exp_ptr], eax
        mov [lib_exp_left], edx
.lfe_loop:
        mov ecx, [lib_exp_left]
        test ecx, ecx
        jz .lfe_no
        mov esi, [lib_exp_ptr]
        mov eax, [esi]                  ; this export's name RVA
        add eax, [lib_rva0]
        mov esi, eax                    ; the name itself, not its RVA
        mov edi, [lib_sym_ptr]
        call lib_str_equal
        jc .lfe_found
        add dword [lib_exp_ptr], 8
        dec dword [lib_exp_left]
        jmp .lfe_loop
.lfe_found:
        mov esi, [lib_exp_ptr]
        mov eax, [esi + 4]
        mov [esp + 28], eax
        popad
        clc
        ret
.lfe_no:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; lib_map_modules - map the resolved libraries into a task
;   In : EDI = task id
;   Out: EAX = number mapped
;
; Called once the task has a page directory, because a library lives in the
; task's own address space and not in the window. hbx_map_task_image does the
; work, so a library gets the same page protections, relocation fixups and vm
; area record the main image does.
;
; A module that cannot be mapped is reported and skipped: its imports stay
; unresolved and the first call through one faults inside that one task, which
; the fault path contains. EXEC.md section 6 records that as the gap until a
; load can be failed after the process exists.
;-----------------------------------------------------------------------
lib_map_modules:
        pushad
        mov dword [lib_mapped], 0
        cmp dword [lib_module_count], 0
        je .lmm_done
        mov [lib_map_pid], edi

        mov dword [lib_map_idx], 0
.lmm_loop:
        mov eax, [lib_map_idx]
        cmp eax, [lib_module_count]
        jae .lmm_done
        imul eax, eax, LIB_MOD_SIZE
        add eax, lib_modules
        mov [lib_rec], eax
        call lib_reload_module          ; CF on refusal: reported, then skipped
        jc .lmm_skip

        mov byte [hbx_map_pending], 1
        mov edi, [lib_map_pid]
        call hbx_map_task_image
        cmp eax, 1
        jne .lmm_skip
        inc dword [lib_mapped]

.lmm_skip:
        inc dword [lib_map_idx]
        jmp .lmm_loop

.lmm_done:
        mov dword [hbx_src_base], PROGRAM_BASE
        mov byte [hbx_map_pending], 0
        mov eax, [lib_mapped]
        mov [esp + 28], eax
        popad
        ret

;=======================================================================
; STRING AND PATH HELPERS
;=======================================================================

; ESI = source, EDI = destination. Copies at most LIB_NAME_LEN-1 bytes.
lib_copy_name:
        push ecx
        mov ecx, LIB_NAME_LEN - 1
.lcn_loop:
        test ecx, ecx
        jz .lcn_end
        lodsb
        stosb
        test al, al
        jz .lcn_done
        dec ecx
        jmp .lcn_loop
.lcn_end:
        mov byte [edi], 0
.lcn_done:
        pop ecx
        ret

; ESI = a, EDI = b. CF set when the strings are the same.
lib_str_equal:
        push eax
        push esi
        push edi
.lse_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .lse_no
        test al, al
        jz .lse_yes
        inc esi
        inc edi
        jmp .lse_loop
.lse_yes:
        pop edi
        pop esi
        pop eax
        stc
        ret
.lse_no:
        pop edi
        pop esi
        pop eax
        clc
        ret

; ESI = a prefix ("/lib/" or "/"). Builds lib_path_buf = prefix + name + ".hbl".
lib_build_path:
        pushad
        mov edi, lib_path_buf
.lbp_prefix:
        lodsb
        test al, al
        jz .lbp_name
        stosb
        jmp .lbp_prefix
.lbp_name:
        mov esi, lib_name_buf
.lbp_name_loop:
        lodsb
        test al, al
        jz .lbp_ext
        stosb
        jmp .lbp_name_loop
.lbp_ext:
        mov esi, msg_lib_ext
.lbp_ext_loop:
        lodsb
        stosb
        test al, al
        jnz .lbp_ext_loop
        popad
        ret

;=======================================================================
; LIBRARY STATE AND MESSAGES
;=======================================================================
lib_modules:        times LIB_MAX_MODULES * LIB_MOD_SIZE db 0
lib_module_count:   dd 0
lib_module_index:   dd 0
lib_desc_idx:       dd 0
lib_desc_ptr:       dd 0
lib_thunk_ptr:      dd 0
lib_thunk_left:     dd 0
lib_iat_rva:        dd 0
lib_ret:            dd 0
lib_retry:          dd 0
lib_rec:            dd 0
lib_slot:           dd 0
lib_len:            dd 0
lib_base:           dd 0
lib_export_rva:     dd 0
lib_exp_ptr:        dd 0
lib_exp_left:       dd 0
lib_sym_ptr:        dd 0
lib_rva0:           dd 0
lib_map_pid:        dd 0
lib_map_idx:        dd 0
lib_mapped:         dd 0
; The program's own numbers, saved before a library overwrites hbx_*.
lib_prog_imp_count: dd 0
lib_prog_imp_rva:   dd 0
lib_prog_hdr:       dd 0
lib_prog_len:       dd 0
lib_prog_base:      dd 0
lib_prog_payload:   dd 0
lib_prog_img:       dd 0
lib_prog_reloc:     dd 0
lib_prog_ibase:     dd 0
lib_prog_entry:     dd 0
lib_prog_exp_count: dd 0
lib_prog_exp_rva:   dd 0
; What the program itself declared for thread-local storage. tls_setup_task
; reads these after every library has overwritten hbx_tls_*.
hbx_main_tls_dir:      dd 0
hbx_main_tls_size:     dd 0
hbx_main_tls_data:     dd 0
hbx_main_tls_base_rva: dd 0
hbx_main_base:         dd 0
lib_name_buf:       times LIB_NAME_LEN db 0
lib_symbol_buf:     times LIB_NAME_LEN db 0
lib_path_buf:       times LIB_NAME_LEN + 16 db 0

msg_lib_prefix:         db "hbx: ", 0
msg_lib_bang:           db "!", 0
msg_lib_not_found_tail: db " not found", 0x0A, 0
msg_lib_dir:            db "/lib/", 0
msg_lib_slash:          db "/", 0
msg_lib_ext:            db ".hbl", 0
msg_lib_no_module:      db " is not installed", 0x0A, 0
msg_lib_too_big:        db " is larger than a library slot", 0x0A, 0
msg_lib_notlib:         db " is not a library", 0x0A, 0
msg_lib_noreloc:        db " cannot be moved: it has no relocations", 0x0A, 0
msg_lib_imports:        db " imports, which libraries may not do yet", 0x0A, 0
msg_lib_noroom_tail:    db " cannot be placed: no free address", 0x0A, 0
msg_lib_noroom:         db "hbx: the program leaves no room to stage a library", 0x0A, 0
msg_lib_toomany:        db "hbx: too many libraries for one program", 0x0A, 0
msg_lib_bad:            db "hbx: an import slot is outside the image", 0x0A, 0

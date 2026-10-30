;=======================================================================
; IMAGE LOADING - docs/spec/EXEC.md
;
; A program is one of three things, and its header says which: an HBX image
; (the native format, section 2), an ELF32 development image, or a flat binary
; with no header at all. The extension never decides - a file whose header
; disagrees with its name is loaded by the header and the disagreement is not
; this routine's business.
;
; image_load_program is the only door. Before M5 each launch path compared the
; first dword against ELF_MAGIC itself, so the shell and the service manager
; each had their own copy of the decision; a third format would have meant a
; third branch in each.
;
; An HBX image takes one of two roads. An image built for the program window
; is copied into it, which is what a flat binary does. An image that carries
; relocations is instead mapped at a random base in the demand zone, one page
; per section protection, when its task is launched - sections 3 and 4. The
; choice is made here, but the mapping itself happens at the launch site
; because it needs the task's page directory.
;=======================================================================

;-----------------------------------------------------------------------
; image_load_program - load the file at PROGRAM_BASE
;   In : EAX = bytes read into PROGRAM_BASE
;   Out: EAX = entry address, CF clear
;        EAX = mv_status,   CF set - the refusal is already reported
;   Sets program_size and program_brk_init so the heap starts after the image.
;   When hbx_map_pending is set the image has not been copied: the launch site
;   must call hbx_map_task_image before the task runs.
;-----------------------------------------------------------------------
image_load_program:
        push ebx
        push ecx
        push edx

        ; Only an HBX image can import. Clearing the count here means a flat,
        ; ELF32 or PE32 launch after an importing one cannot inherit its table.
        mov dword [hbx_import_count], 0

        cmp dword [PROGRAM_BASE], HBX_MAGIC
        je .ilp_hbx
        cmp dword [PROGRAM_BASE], ELF_MAGIC
        je .ilp_elf
        cmp word [PROGRAM_BASE], 0x5A4D  ; 'MZ'
        je .ilp_pe

        ; A flat binary: the file is the image and its first byte is the entry.
        mov [program_size], eax
        mov ebx, eax
        add ebx, PROGRAM_BASE
        mov eax, ebx
        call set_program_brk_init
        mov eax, PROGRAM_BASE
        clc
        jmp .ilp_out

.ilp_elf:
        call elf_load_program           ; EAX = entry, EDX = first free byte
        jc .ilp_fail
        ; The entry comes from the image's own header, so it has to be recorded
        ; here: program_entry is what the launch site reads, and an ELF image is
        ; the one format whose entry is not simply PROGRAM_BASE.
        mov [program_entry], eax
        mov ecx, edx
        sub ecx, PROGRAM_BASE
        mov [program_size], ecx
        mov eax, edx
        call set_program_brk_init
        mov eax, [program_entry]
        clc
        jmp .ilp_out

.ilp_hbx:
        mov byte [hbx_map_pending], 0
        call hbx_prepare                ; EAX = entry, CF on refusal
        jc .ilp_fail
        mov [program_entry], eax
        clc
        jmp .ilp_out

.ilp_pe:
        call pe_load_program            ; EAX = bytes read; entry out, CF on refusal
        jc .ilp_fail
        mov [program_entry], eax
        clc
        jmp .ilp_out

.ilp_fail:
        stc
.ilp_out:
        pop edx
        pop ecx
        pop ebx
        ret

;=======================================================================
; HBX LOADER - docs/spec/EXEC.md sections 2, 3, 4 and 8
;
; Order matters: every refusal that can be made without touching memory is
; made first, so a refused image never leaves half of itself behind. The copy
; is the first write, and it only runs once the header, the checksum, the
; sections, the entry point and the imports have all been accepted.
;=======================================================================

;-----------------------------------------------------------------------
; hbx_verify - check an HBX image in the window before anything is written
;   In : EAX = bytes read, and the file at [hbx_src_base]
;   Out: CF clear, and the statics below describe the image
;        CF set - the reason is on the console and EAX is an mv_status
;
; Every check runs against the file as it lies in memory: the header, the
; 32-bit halves, the checksum, the section table, the entry point, the
; relocation table's bounds and the import table (whose RVAs are read through
; header_size, because the image bytes have not been moved yet).
;
; [hbx_src_base] is PROGRAM_BASE for a program and a staging slot for a shared
; library, so one verifier checks both (EXEC.md sections 2 and 6).
;-----------------------------------------------------------------------
hbx_verify:
        pushad
        mov [hbx_file_len], eax

        cmp eax, HBX_HDR_SIZE
        jb .bad_header
        mov ebx, [hbx_src_base]

        cmp dword [ebx + HBX_OFF_VERSION], HBX_VERSION
        jne .bad_version
        test dword [ebx + HBX_OFF_FLAGS], HBX_FLAG_64BIT
        jnz .is64

        ; The header has to hold the section table it declares.
        mov eax, [ebx + HBX_OFF_HDRSIZE]
        cmp eax, HBX_HDR_SIZE
        jb .bad_header
        cmp eax, [hbx_file_len]
        ja .bad_header
        mov [hbx_hdr_size], eax
        mov ecx, [ebx + HBX_OFF_SECTIONS]
        cmp ecx, HBX_MAX_SECTIONS
        ja .bad_header
        mov [hbx_sec_count], ecx
        imul edx, ecx, HBX_SECTION_SIZE
        add edx, HBX_HDR_SIZE
        cmp edx, eax
        ja .bad_header

        ; Every u64 is a 32-bit value here: a high half that is not zero is a
        ; 64-bit image whatever its flags said.
        cmp dword [ebx + HBX_OFF_IMAGE_BASE + 4], 0
        jne .is64
        cmp dword [ebx + HBX_OFF_IMAGE_SIZE + 4], 0
        jne .bad_header
        cmp dword [ebx + HBX_OFF_ENTRY + 4], 0
        jne .bad_header
        cmp dword [ebx + HBX_OFF_CHECKSUM + 4], 0
        jne .bad_header

        mov eax, [ebx + HBX_OFF_IMAGE_BASE]
        mov [hbx_image_base], eax
        mov eax, [ebx + HBX_OFF_IMAGE_SIZE]
        mov [hbx_img_size], eax
        cmp eax, PROGRAM_MAX_SIZE
        ja .bad_header
        mov ecx, [hbx_file_len]
        sub ecx, [hbx_hdr_size]
        mov [hbx_payload_len], ecx
        cmp eax, ecx                    ; the image cannot be smaller than its
        jb .bad_header                  ; own bytes

        ; ---- checksum: CRC-32C over the whole file, the field read as zero ----
        mov eax, [ebx + HBX_OFF_CHECKSUM]
        mov [hbx_stored_crc], eax
        mov dword [ebx + HBX_OFF_CHECKSUM], 0
        mov eax, ebx
        mov ecx, [hbx_file_len]
        call crc32c
        mov [hbx_calc_crc], eax
        mov ecx, [hbx_stored_crc]
        mov [ebx + HBX_OFF_CHECKSUM], ecx
        cmp eax, ecx
        jne .bad_checksum

        ; The image is copied down onto the header's own address (or mapped
        ; elsewhere), so every field the loader still needs afterwards is taken
        ; now.
        mov eax, [ebx + HBX_OFF_ENTRY]
        mov [hbx_entry], eax
        mov eax, [ebx + HBX_OFF_RELOC]
        mov [hbx_reloc_rva], eax
        mov eax, [ebx + HBX_OFF_IMPORTS]
        mov [hbx_import_count], eax
        mov eax, [ebx + HBX_OFF_IMPORT_RVA]
        mov [hbx_import_rva], eax
        mov eax, [ebx + HBX_OFF_EXPORTS]
        mov [hbx_export_count], eax
        mov eax, [ebx + HBX_OFF_EXPORT_RVA]
        mov [hbx_export_rva], eax

        ; ---- sections: bounds, no writable-and-executable, entry inside one ----
        mov esi, ebx
        add esi, HBX_HDR_SIZE
        mov ecx, [hbx_sec_count]
        xor edi, edi                    ; 1 when an executable section has entry
.sec_loop:
        test ecx, ecx
        jz .sec_done
        mov [hbx_sec_ptr], esi
        mov eax, [esi + HBX_SCN_OFF_CHARS]
        and eax, HBX_SCN_WRITE | HBX_SCN_EXEC
        cmp eax, HBX_SCN_WRITE | HBX_SCN_EXEC
        je .wx
        mov eax, [esi + HBX_SCN_OFF_RVA]
        mov edx, [esi + HBX_SCN_OFF_SIZE]
        add eax, edx
        jc .bad_section
        cmp eax, [hbx_img_size]
        ja .bad_section
        mov eax, [esi + HBX_SCN_OFF_RVA]
        cmp [hbx_entry], eax
        jb .sec_next
        add eax, edx
        cmp [hbx_entry], eax
        jae .sec_next
        test dword [esi + HBX_SCN_OFF_CHARS], HBX_SCN_EXEC
        jz .sec_next
        mov edi, 1
.sec_next:
        add esi, HBX_SECTION_SIZE
        dec ecx
        jmp .sec_loop
.sec_done:
        mov eax, [hbx_sec_count]
        test eax, eax
        jz .entry_no_sections
        test edi, edi
        jz .entry_not_exec
        jmp .base_ok
.entry_no_sections:
        ; A header without a section table is loaded whole: the entry only has
        ; to be inside the image.
        mov eax, [hbx_entry]
        cmp eax, [hbx_img_size]
        jae .entry_not_exec

.base_ok:
        ; ---- the relocation table and the import table are inside the image ----
        mov ebp, [hbx_src_base]
        add ebp, [hbx_hdr_size]         ; where RVA 0 lives in the file

        mov ecx, [hbx_reloc_rva]
        test ecx, ecx
        jz .no_relocs
        mov eax, ecx
        add eax, 4
        jc .bad_reloc
        cmp eax, [hbx_img_size]
        ja .bad_reloc
        mov edx, [ebp + ecx]            ; count, then that many u32 RVAs
        cmp edx, PROGRAM_MAX_SIZE / 4
        ja .bad_reloc
        mov eax, edx
        shl eax, 2
        add eax, ecx
        add eax, 4
        cmp eax, [hbx_img_size]
        ja .bad_reloc
.no_relocs:

        ; ---- thread-local storage (EXEC.md section 6) ----
        ; The directory is {raw_size, data_rva, index_rva, base_rva}: a template
        ; inside the image, and two slots the loader fills - this module's
        ; index, and this thread's block. Bounds are checked here; the block is
        ; made once the task exists.
        mov eax, [ebx + HBX_OFF_TLS]
        mov [hbx_tls_dir], eax
        mov dword [hbx_tls_size], 0
        test eax, eax
        jz .no_tls
        mov edx, eax
        add edx, 16
        jc .bad_reloc
        cmp edx, [hbx_img_size]
        ja .bad_reloc
        mov ebp, [hbx_src_base]
        add ebp, [hbx_hdr_size]         ; RVA 0 of this image
        mov ecx, [ebp + eax]            ; raw_size
        test ecx, ecx
        jz .bad_header                  ; a directory with nothing in it
        mov [hbx_tls_size], ecx
        mov edx, [ebp + eax + 4]        ; data_rva
        mov [hbx_tls_data], edx
        add edx, ecx
        jc .bad_reloc
        cmp edx, [hbx_img_size]
        ja .bad_reloc
        mov edx, [ebp + eax + 8]        ; index_rva
        mov [hbx_tls_index_rva], edx
        add edx, 4
        jc .bad_reloc
        cmp edx, [hbx_img_size]
        ja .bad_reloc
        mov edx, [ebp + eax + 12]       ; base_rva
        mov [hbx_tls_base_rva], edx
        add edx, 4
        jc .bad_reloc
        cmp edx, [hbx_img_size]
        ja .bad_reloc
.no_tls:

        ; ---- the export table, which is what a library is for ----
        ; A table is a u32 count and that many {name_rva, rva} pairs.
        mov ecx, [hbx_export_count]
        test ecx, ecx
        jz .no_exports
        mov esi, [hbx_export_rva]
        test esi, esi
        jz .bad_header
        mov eax, ecx
        shl eax, 3
        add eax, 4
        jc .bad_reloc
        add eax, esi
        jc .bad_reloc
        cmp eax, [hbx_img_size]
        ja .bad_reloc
.no_exports:

        ; An import table has to be somewhere, and the first descriptor has to
        ; be inside the image; a count without an RVA is a header the loader
        ; cannot act on. The symbols themselves are resolved against the real
        ; libraries by lib_resolve_imports right after this returns (EXEC.md
        ; sections 3 and 6), so nothing here refuses a named import.
        mov ecx, [hbx_import_count]
        test ecx, ecx
        jz .verified
        mov esi, [hbx_import_rva]
        test esi, esi
        jz .bad_header
        mov eax, esi
        add eax, 12
        jc .bad_reloc
        cmp eax, [hbx_img_size]
        ja .bad_reloc

.verified:
        popad
        clc
        ret

        ; ---- refusals (EXEC.md section 8) ----
.bad_version:
        mov esi, msg_hbx_version
        call vga_print
        mov eax, [ebx + HBX_OFF_VERSION]
        call vga_print_dec
        call vga_newline
        jmp .bad_image

.bad_checksum:
        mov esi, msg_hbx_bad_checksum
        call vga_print
        mov eax, [hbx_stored_crc]
        call vga_print_hex
        mov esi, msg_hbx_comma
        call vga_print
        mov eax, [hbx_calc_crc]
        call vga_print_hex
        call vga_newline
        jmp .bad_image

.bad_section:
        mov [hbx_bad_off], eax
        jmp .bad_at
.bad_reloc:
        mov [hbx_bad_off], esi
.bad_at:
        mov esi, msg_hbx_bad_at
        call vga_print
        mov eax, [hbx_bad_off]
        call vga_print_hex
        call vga_newline
        jmp .bad_image

.wx:
        mov esi, msg_hbx_section
        call vga_print
        mov esi, [hbx_sec_ptr]
        call hbx_print_section_name
        mov esi, msg_hbx_wx_tail
        call vga_print
        jmp .bad_image

.entry_not_exec:
        mov esi, msg_hbx_entry
        call vga_print
        mov eax, [hbx_entry]
        call vga_print_hex
        mov esi, msg_hbx_entry_tail
        call vga_print
        mov eax, MV_ENTRYPOINT_NOT_FOUND
        jmp .fail

.is64:
        mov esi, msg_hbx_32bit
        call vga_print
.bad_header:
        mov esi, msg_hbx_bad_header
        call vga_print
.bad_image:
        mov eax, MV_BAD_IMAGE
.fail:
        mov [esp + 28], eax
        popad
        stc
        ret

;-----------------------------------------------------------------------
; hbx_prepare - verify, then choose the road this image takes
;   In : EAX = bytes read into PROGRAM_BASE
;   Out: EAX = entry address, CF clear
;        EAX = mv_status,   CF set - the refusal is already reported
;
; An image built for PROGRAM_BASE is copied into the window, as an HBX image
; always had been. An image that carries relocations is mapped at a random base
; when its task is launched (section 4), so nothing is copied here and the file
; stays in the window until then. An image built elsewhere that cannot be
; relocated has no legal home, and section 8 says to say so.
;-----------------------------------------------------------------------
hbx_prepare:
        pushad
        mov byte [hbx_map_pending], 0
        mov dword [hbx_src_base], PROGRAM_BASE
        call hbx_verify
        jc .hp_refused

        ; Imports are resolved against real libraries before any of the image
        ; moves, and the resolved addresses are written into the file copy's
        ; import address table: both roads - the copy into the window and the
        ; mapping at a random base - then inherit them for free.
        call lib_resolve_imports        ; EAX = mv_status on refusal
        jc .hp_fail

        ; The main image is module 0, so its thread-local index is written into
        ; the file copy too: a module reads its own index, and the block it
        ; points at is made once the task exists (kernel/tls.inc).
        mov dword [hbx_tls_index], 0
        mov eax, [hbx_tls_dir]
        test eax, eax
        jz .hp_no_tls
        mov dword [hbx_tls_index], 0
        mov ecx, [hbx_tls_index_rva]
        add ecx, [hbx_hdr_size]
        cmp ecx, [hbx_file_len]
        jae .hp_fail
        mov ebx, [hbx_src_base]
        mov dword [ebx + ecx], 0
.hp_no_tls:

        mov eax, [hbx_image_base]
        cmp eax, PROGRAM_BASE
        je .hp_window
        cmp dword [hbx_reloc_rva], 0
        je .hp_noreloc

        ; A relocatable image: pick where it will live and let the launch site
        ; map it. The heap still starts after the file in the window, which is
        ; where sbrk grows it; the image itself is elsewhere.
        call hbx_choose_base
        mov eax, [hbx_map_base]
        sub eax, [hbx_image_base]
        mov [hbx_map_delta], eax
        mov eax, [hbx_map_base]
        add eax, [hbx_entry]
        mov [hbx_map_entry], eax
        mov byte [hbx_map_pending], 1
        mov eax, [hbx_map_base]
        mov [hbx_main_base], eax
        mov eax, [hbx_file_len]
        mov [program_size], eax
        add eax, PROGRAM_BASE
        call set_program_brk_init
        mov eax, [hbx_map_entry]
        clc
        jmp .hp_out

.hp_window:
        ; Built for the window. If it cannot be relocated it keeps its base, and
        ; section 4 says the user is entitled to know that.
        cmp dword [hbx_reloc_rva], 0
        jne .hp_window_load
        mov esi, msg_hbx_fixed
        call vga_print
        mov eax, [hbx_image_base]
        call vga_print_hex
        call vga_newline
.hp_window_load:
        call hbx_load_window
        mov dword [hbx_main_base], PROGRAM_BASE
        clc
        jmp .hp_out

.hp_noreloc:
        mov esi, msg_hbx_cannot_load
        call vga_print
        mov eax, [hbx_image_base]
        call vga_print_hex
        mov esi, msg_hbx_noreloc_tail
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .hp_fail

.hp_refused:
        ; hbx_verify already reported it; EAX carries its status.
.hp_fail:
        stc
.hp_out:
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; hbx_load_window - copy a verified image into the program window
;   In : the statics hbx_verify filled, the file at PROGRAM_BASE
;   Out: EAX = entry address; program_entry, program_size and the break set
;-----------------------------------------------------------------------
hbx_load_window:
        pushad
        mov ebx, PROGRAM_BASE

        ; The image bytes follow the header in the file and start at RVA 0.
        ; They are already in the window, so the copy walks downwards: the
        ; destination is always header_size bytes below the source.
        mov esi, ebx
        add esi, [hbx_hdr_size]
        mov edi, PROGRAM_BASE
        mov ecx, [hbx_payload_len]
        cld
        rep movsb
        mov ecx, [hbx_img_size]
        sub ecx, [hbx_payload_len]
        xor al, al
        rep stosb

        ; ---- relocations, when the image was built for another base ----
        mov eax, PROGRAM_BASE
        sub eax, [hbx_image_base]
        mov [hbx_delta], eax
        test eax, eax
        jz .loaded
        mov esi, [hbx_reloc_rva]
        mov ecx, [PROGRAM_BASE + esi]
        add esi, 4
.rel_loop:
        test ecx, ecx
        jz .loaded
        mov edx, [PROGRAM_BASE + esi]
        mov eax, [PROGRAM_BASE + edx]
        add eax, [hbx_delta]
        mov [PROGRAM_BASE + edx], eax
        add esi, 4
        dec ecx
        jmp .rel_loop

.loaded:
        mov eax, [hbx_img_size]
        mov [program_size], eax
        add eax, PROGRAM_BASE
        call set_program_brk_init
        mov eax, [hbx_entry]
        add eax, PROGRAM_BASE
        mov [program_entry], eax
        mov [esp + 28], eax
        popad
        ret

;=======================================================================
; MAPPING AT A RANDOM BASE - EXEC.md sections 3 and 4
;
; The image is mapped into the task's own directory, page by page, from the
; file still sitting in the shell's window. Nothing is mapped into the master
; directory: a mapping there would be inherited by every task created after it
; and would collide with the next launch.
;
; A page covered by a writable section is mapped writable; every other page is
; read-only. On a 32-bit non-PAE page table there is no execute bit, so
; read-only is how "code cannot be rewritten" is enforced - which is the half
; of W^X this format can hold.
;=======================================================================

;-----------------------------------------------------------------------
; hbx_choose_base - pick this launch's image base
;   Out: EAX = base; hbx_map_base is set
;
; The demand zone is the only place user mappings may live (VMM.md), so the
; spread is applied there: a random 64 KB-aligned address in the first 256 MB
; of the zone - the alignment VMM.md 2.1 asks of a main image - with the whole
; image kept below the top of the zone.
;-----------------------------------------------------------------------
hbx_choose_base:
        push edx
        call vm_rand
        and eax, 0x0FFF0000
        add eax, [user_demand_base]
        mov edx, eax
        add edx, [hbx_img_size]
        cmp edx, USER_DEMAND_MAX
        jb .hcb_ok
        mov eax, [user_demand_base]
.hcb_ok:
        mov [hbx_map_base], eax
        pop edx
        ret

;-----------------------------------------------------------------------
; hbx_page_writable - does a writable section cover this page?
;   In : EAX = page index     Out: EAX = 1 or 0
;-----------------------------------------------------------------------
hbx_page_writable:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov edi, eax
        shl edi, 12                     ; page start RVA
        mov esi, PROGRAM_BASE
        add esi, HBX_HDR_SIZE
        mov ecx, [hbx_sec_count]
        xor ebx, ebx
.hpw_loop:
        test ecx, ecx
        jz .hpw_done
        test dword [esi + HBX_SCN_OFF_CHARS], HBX_SCN_WRITE
        jz .hpw_next
        mov eax, [esi + HBX_SCN_OFF_RVA]
        mov edx, [esi + HBX_SCN_OFF_SIZE]
        add edx, eax                    ; section end, exclusive
        cmp edx, edi
        jbe .hpw_next                   ; ends at or before this page
        lea eax, [edi + 4096]           ; this page's end
        cmp eax, [esi + HBX_SCN_OFF_RVA]
        jbe .hpw_next                   ; starts at or after this page
        mov ebx, 1
.hpw_next:
        add esi, HBX_SECTION_SIZE
        dec ecx
        jmp .hpw_loop
.hpw_done:
        mov eax, ebx
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; hbx_map_task_image - map a prepared image into a task
;   In : EDI = task id
;   Out: EAX = 1  mapped; program_entry already names the mapped entry
;        EAX = 0  nothing to map (not a relocatable HBX image)
;        EAX = -1 failed; whatever was mapped has been released, and the caller
;                 must call hbx_fallback_window
;-----------------------------------------------------------------------
hbx_map_task_image:
        pushad
        mov dword [hbx_map_ret], 0
        cmp byte [hbx_map_pending], 0
        jne .hmt_map
        jmp .hmt_out

.hmt_map:
        mov [hbx_map_pid], edi
        mov eax, edi
        cmp eax, MAX_TASKS
        jae .hmt_fail_t
        imul eax, TCB_SIZE
        add eax, task_table
        mov eax, [eax + TCB_CR3]
        test eax, eax
        jz .hmt_fail_c
        mov [hbx_map_pd], eax
        mov eax, [hbx_img_size]
        add eax, 0xFFF
        shr eax, 12
        mov [hbx_map_pages], eax
        mov dword [hbx_map_idx], 0

.hmt_page:
        mov eax, [hbx_map_idx]
        cmp eax, [hbx_map_pages]
        jae .hmt_relocs
        call pmm_alloc_page
        test eax, eax
        jz .hmt_fail_p
        mov [hbx_map_frame], eax
        ; A fresh frame is not zeroed by the allocator, and the tail of the
        ; image is BSS: every byte of it is written here.
        mov edi, eax
        xor eax, eax
        mov ecx, 1024
        cld
        rep stosd

        mov eax, [hbx_map_idx]
        shl eax, 12                     ; offset into the image
        mov ecx, [hbx_payload_len]
        sub ecx, eax
        jbe .hmt_flags                  ; past the file: zeros only
        cmp ecx, 4096
        jbe .hmt_copy
        mov ecx, 4096
.hmt_copy:
        mov esi, [hbx_src_base]
        add esi, [hbx_hdr_size]
        add esi, eax
        mov edi, [hbx_map_frame]
        rep movsb

.hmt_flags:
        mov eax, [hbx_map_idx]
        call hbx_page_writable
        mov ecx, PG_PRESENT | PG_USER
        test eax, eax
        jz .hmt_map_now
        or ecx, PG_WRITABLE
.hmt_map_now:
        mov eax, [hbx_map_idx]
        shl eax, 12
        add eax, [hbx_map_base]
        mov ebx, [hbx_map_frame]
        mov edi, [hbx_map_pd]
        call paging_map_page_at
        inc dword [hbx_map_idx]
        jmp .hmt_page

.hmt_relocs:
        mov eax, [hbx_map_delta]
        test eax, eax
        jz .hmt_record
        mov esi, [hbx_src_base]
        add esi, [hbx_hdr_size]
        add esi, [hbx_reloc_rva]
        mov ecx, [esi]
        add esi, 4
.hmt_rel_loop:
        test ecx, ecx
        jz .hmt_record
        mov [hbx_rel_count], ecx
        mov [hbx_rel_ptr], esi
        mov edx, [esi]                  ; RVA of the dword to fix
        add edx, [hbx_map_base]         ; where it is now
        mov eax, edx
        and eax, 0xFFF
        mov [hbx_rel_off], eax
        mov eax, [hbx_map_pd]
        mov ebx, edx
        and ebx, ~0xFFF
        call vm_pte_phys
        test eax, eax
        jz .hmt_fail_r
        add eax, [hbx_rel_off]          ; the frame is identity-mapped
        mov ebx, [eax]
        add ebx, [hbx_map_delta]
        mov [eax], ebx
        mov esi, [hbx_rel_ptr]
        add esi, 4
        mov ecx, [hbx_rel_count]
        dec ecx
        jmp .hmt_rel_loop

.hmt_record:
        ; One area covers the image, so fork copies it copy-on-write and exit
        ; releases its frames with every other mapping.
        mov eax, [hbx_map_base]
        mov ecx, [hbx_map_pages]
        mov edx, PROT_READ | PROT_WRITE
        mov edi, [hbx_map_pid]
        call vm_record_for_pid
        test eax, eax
        jz .hmt_fail
        mov byte [hbx_map_pending], 0
        mov dword [hbx_map_ret], 1
        jmp .hmt_out

.hmt_fail_t:
.hmt_fail_c:
.hmt_fail_p:
.hmt_fail_r:
.hmt_fail:
        call hbx_unmap_partial
        mov byte [hbx_map_pending], 0
        mov dword [hbx_map_ret], -1
.hmt_out:
        mov eax, [hbx_map_ret]
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; hbx_unmap_partial - release the pages hbx_map_task_image already mapped
;-----------------------------------------------------------------------
hbx_unmap_partial:
        pushad
        mov ecx, [hbx_map_idx]
        test ecx, ecx
        jz .hup_done
        mov [hbx_unmap_left], ecx
        mov dword [hbx_unmap_i], 0
.hup_loop:
        mov eax, [hbx_unmap_i]
        shl eax, 12
        add eax, [hbx_map_base]
        mov [hbx_unmap_va], eax
        mov ebx, eax
        mov eax, [hbx_map_pd]
        call vm_pte_phys
        test eax, eax
        jz .hup_clear
        call pmm_free_page
.hup_clear:
        mov eax, [hbx_map_pd]
        mov ebx, [hbx_unmap_va]
        call vm_clear_pte
        inc dword [hbx_unmap_i]
        dec dword [hbx_unmap_left]
        jnz .hup_loop
.hup_done:
        popad
        ret

;-----------------------------------------------------------------------
; hbx_fallback_window - load the window copy of a prepared image
;   In : EDI = task id whose entry must be repointed, or -1 for none
;
; Called when a task exists but its image could not be mapped: the file is
; still untouched in the window, so the ordinary copy is made and the task's
; saved EIP moves with it.
;-----------------------------------------------------------------------
hbx_fallback_window:
        pushad
        mov [hbx_fb_pid], edi
        mov byte [hbx_map_pending], 0
        call hbx_load_window            ; EAX = entry, sets program_entry
        cmp dword [hbx_fb_pid], -1
        je .hfw_done
        mov eax, [hbx_fb_pid]
        cmp eax, MAX_TASKS
        jae .hfw_done
        imul eax, TCB_SIZE
        add eax, task_table
        mov ebx, [program_entry]
        mov [eax + TCB_ENTRY], ebx
        mov ecx, [eax + TCB_ESP]
        mov [ecx + 32], ebx             ; EIP in the initial iretd frame
.hfw_done:
        popad
        ret

;-----------------------------------------------------------------------
; hbx_print_name - print a NUL-terminated name from inside the image
;   In : ESI = name
;   Out: CF set when the NUL is not inside the image (a torn header)
;-----------------------------------------------------------------------
hbx_print_name:
        pushad
        mov edi, esi
        mov ecx, PROGRAM_BASE
        add ecx, [hbx_file_len]
        sub ecx, edi
        jbe .hpn_fail
        xor edx, edx
.hpn_scan:
        cmp byte [edi + edx], 0
        je .hpn_ok
        inc edx
        cmp edx, ecx
        jb .hpn_scan
        jmp .hpn_fail
.hpn_ok:
        mov esi, edi
        call vga_print
        clc
        jmp .hpn_out
.hpn_fail:
        stc
.hpn_out:
        popad
        ret

;-----------------------------------------------------------------------
; hbx_print_section_name - print a section's 8-byte name
;   In : ESI = section table entry
;-----------------------------------------------------------------------
hbx_print_section_name:
        pushad
        mov edi, hbx_name_buf
        mov ecx, HBX_SCN_NAME_LEN
        cld
        rep movsb
        mov byte [hbx_name_buf + HBX_SCN_NAME_LEN], 0
        mov esi, hbx_name_buf
        call vga_print
        popad
        ret

;=======================================================================
; HBX LOADER STATE AND MESSAGES
;=======================================================================
hbx_src_base:       dd PROGRAM_BASE     ; where the file under test lies
hbx_tls_dir:        dd 0                ; RVA of the TLS directory, 0 if none
hbx_tls_size:       dd 0                ; initialised bytes in its template
hbx_tls_data:       dd 0                ; RVA of the template
hbx_tls_index:      dd 0                ; this module's index, assigned at load
hbx_tls_index_rva:  dd 0
hbx_tls_base_rva:   dd 0
hbx_file_len:       dd 0
hbx_export_count:   dd 0
hbx_export_rva:     dd 0
hbx_hdr_size:       dd 0
hbx_payload_len:    dd 0
hbx_img_size:       dd 0
hbx_image_base:     dd 0
hbx_delta:          dd 0
hbx_entry:          dd 0
hbx_sec_count:      dd 0
hbx_stored_crc:     dd 0
hbx_calc_crc:       dd 0
hbx_sec_ptr:        dd 0
hbx_bad_off:        dd 0
hbx_reloc_rva:      dd 0
hbx_import_count:   dd 0
hbx_import_rva:     dd 0
hbx_name_buf:       times (HBX_SCN_NAME_LEN + 1) db 0

hbx_map_pending:    db 0
hbx_map_base:       dd 0
hbx_map_entry:      dd 0
hbx_map_delta:      dd 0
hbx_map_pages:      dd 0
hbx_map_idx:        dd 0
hbx_map_frame:      dd 0
hbx_map_pd:         dd 0
hbx_map_pid:        dd 0
hbx_map_ret:        dd 0
hbx_unmap_left:     dd 0
hbx_unmap_i:        dd 0
hbx_unmap_va:       dd 0
hbx_rel_ptr:        dd 0
hbx_rel_count:      dd 0
hbx_rel_off:        dd 0
hbx_fb_pid:         dd 0

msg_hbx_bad_header:     db "hbx: bad image in the header", 0x0A, 0
msg_hbx_version:        db "hbx: unsupported header version ", 0
msg_hbx_bad_checksum:   db "hbx: bad checksum: stored 0x", 0
msg_hbx_comma:          db ", computed 0x", 0
msg_hbx_bad_at:         db "hbx: bad image at 0x", 0
msg_hbx_section:        db "hbx: section ", 0
msg_hbx_wx_tail:        db " is writable and executable", 0x0A, 0
msg_hbx_cannot_load:    db "hbx: cannot load at 0x", 0
msg_hbx_noreloc_tail:   db ": no relocations", 0x0A, 0
msg_hbx_fixed:          db "hbx: not relocatable, loaded at 0x", 0
msg_hbx_entry:          db "hbx: entry 0x", 0
msg_hbx_entry_tail:     db " is not executable", 0x0A, 0
msg_hbx_32bit:          db "hbx: this system is 32-bit", 0x0A, 0
msg_hbx_prefix:         db "hbx: ", 0
msg_hbx_bang:           db "!", 0
msg_hbx_import_tail:    db " not found", 0x0A, 0

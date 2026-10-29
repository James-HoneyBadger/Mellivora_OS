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
;=======================================================================

;-----------------------------------------------------------------------
; image_load_program - load the file at PROGRAM_BASE
;   In : EAX = bytes read into PROGRAM_BASE
;   Out: EAX = entry address, CF clear
;        EAX = mv_status,   CF set - the refusal is already reported
;   Sets program_size and program_brk_init so the heap starts after the image.
;-----------------------------------------------------------------------
image_load_program:
        push ebx
        push ecx
        push edx

        cmp dword [PROGRAM_BASE], HBX_MAGIC
        je .ilp_hbx
        cmp dword [PROGRAM_BASE], ELF_MAGIC
        je .ilp_elf

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
        mov ecx, edx
        sub ecx, PROGRAM_BASE
        mov [program_size], ecx
        mov eax, edx
        call set_program_brk_init
        mov eax, [program_entry]
        clc
        jmp .ilp_out

.ilp_hbx:
        call hbx_load_program           ; EAX = entry, CF on refusal
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
; HBX LOADER - docs/spec/EXEC.md sections 2, 3 and 8
;
; Order matters: every refusal that can be made without touching the window is
; made first, so a refused image never leaves half of itself behind. The copy
; is the first write, and it only runs once the header, the checksum, the
; sections and the entry point have all been accepted.
;=======================================================================

;-----------------------------------------------------------------------
; hbx_load_program - verify and load an HBX image at PROGRAM_BASE
;   In : EAX = bytes read into PROGRAM_BASE
;   Out: EAX = entry address, CF clear
;        EAX = mv_status,   CF set - the reason is on the console
;
; The image is loaded at PROGRAM_BASE, not at its preferred base: the program
; window is one fixed megabyte (the layout in kernel.asm), so an image that
; asks for another base is relocated by the difference, and an image that
; cannot be relocated is refused by name. ASLR needs mapping at an arbitrary
; base and arrives with section-based mapping.
;-----------------------------------------------------------------------
hbx_load_program:
        pushad
        mov [hbx_file_len], eax

        cmp eax, HBX_HDR_SIZE
        jb .bad_header
        mov ebx, PROGRAM_BASE

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

        ; The image is copied down onto the header's own address, so every
        ; field the loader still needs after the copy is taken now.
        mov eax, [ebx + HBX_OFF_ENTRY]
        mov [hbx_entry], eax
        mov eax, [ebx + HBX_OFF_RELOC]
        mov [hbx_reloc_rva], eax
        mov eax, [ebx + HBX_OFF_IMPORTS]
        mov [hbx_import_count], eax
        mov eax, [ebx + HBX_OFF_IMPORT_RVA]
        mov [hbx_import_rva], eax

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
        ; ---- the delta between where it was built and where it is going ----
        mov eax, PROGRAM_BASE
        sub eax, [hbx_image_base]
        mov [hbx_delta], eax
        test eax, eax
        jz .have_base
        cmp dword [hbx_reloc_rva], 0
        je .noreloc

.have_base:
        ; An import table has to be somewhere; a count without an RVA is a
        ; header the loader cannot act on.
        cmp dword [hbx_import_count], 0
        je .copy
        cmp dword [hbx_import_rva], 0
        je .bad_header

.copy:
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

        ; ---- relocations, only when the delta is not zero ----
        mov eax, [hbx_delta]
        test eax, eax
        jz .imports
        mov esi, [hbx_reloc_rva]
        mov eax, esi
        add eax, 4                      ; count, then that many u32 RVAs
        jc .bad_reloc
        cmp eax, [hbx_img_size]
        ja .bad_reloc
        mov ecx, [PROGRAM_BASE + esi]
        cmp ecx, PROGRAM_MAX_SIZE / 4
        ja .bad_reloc
        mov edx, ecx
        shl edx, 2
        add edx, eax
        cmp edx, [hbx_img_size]
        ja .bad_reloc
        add esi, 4
.rel_loop:
        test ecx, ecx
        jz .imports
        mov edx, [PROGRAM_BASE + esi]
        mov eax, edx
        add eax, 4
        jc .bad_reloc
        cmp eax, [hbx_img_size]
        ja .bad_reloc
        mov eax, [PROGRAM_BASE + edx]
        add eax, [hbx_delta]
        mov [PROGRAM_BASE + edx], eax
        add esi, 4
        dec ecx
        jmp .rel_loop

.imports:
        ; No library is loadable yet, so every import is missing. The contract
        ; that matters now is the one in section 8: name the module and the
        ; symbol, and leave nothing behind - which is why this runs before the
        ; process exists. A resolver replaces the refusal when libraries land.
        mov ecx, [hbx_import_count]
        test ecx, ecx
        jz .loaded
        mov esi, [hbx_import_rva]
        mov eax, esi
        add eax, 12
        jc .bad_reloc
        cmp eax, [hbx_img_size]
        ja .bad_reloc
        mov edi, [PROGRAM_BASE + esi]           ; module name RVA
        cmp edi, [hbx_img_size]
        jae .bad_reloc
        mov ebp, [PROGRAM_BASE + esi + 4]       ; thunk array RVA
        mov eax, ebp
        add eax, 8
        jc .bad_reloc
        cmp eax, [hbx_img_size]
        ja .bad_reloc
        mov edx, [PROGRAM_BASE + ebp]           ; first symbol name RVA
        cmp edx, [hbx_img_size]
        jae .bad_reloc
        mov esi, msg_hbx_prefix
        call vga_print
        mov eax, PROGRAM_BASE
        add eax, edi
        mov esi, eax
        call hbx_print_name
        jc .bad_header
        mov esi, msg_hbx_bang
        call vga_print
        mov eax, PROGRAM_BASE
        add eax, edx
        mov esi, eax
        call hbx_print_name
        jc .bad_header
        mov esi, msg_hbx_import_tail
        call vga_print
        mov eax, MV_DLL_NOT_FOUND
        jmp .fail

.loaded:
        mov eax, [hbx_img_size]
        mov [program_size], eax
        add eax, PROGRAM_BASE
        call set_program_brk_init
        mov eax, [hbx_entry]
        add eax, PROGRAM_BASE
        mov [esp + 28], eax
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

.noreloc:
        mov esi, msg_hbx_cannot_load
        call vga_print
        mov eax, [hbx_image_base]
        call vga_print_hex
        mov esi, msg_hbx_noreloc_tail
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
; hbx_print_name - print a NUL-terminated name from inside the image
;   In : ESI = name
;   Out: CF set when the NUL is not inside the image (a torn header)
;-----------------------------------------------------------------------
hbx_print_name:
        pushad
        mov edi, esi
        mov ecx, PROGRAM_BASE
        add ecx, [hbx_img_size]
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
hbx_file_len:       dd 0
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
hbx_reloc_rva:      dd 0
hbx_import_count:   dd 0
hbx_import_rva:     dd 0
hbx_bad_off:        dd 0
hbx_name_buf:       times (HBX_SCN_NAME_LEN + 1) db 0

msg_hbx_bad_header:     db "hbx: bad image in the header", 0x0A, 0
msg_hbx_version:        db "hbx: unsupported header version ", 0
msg_hbx_bad_checksum:   db "hbx: bad checksum: stored 0x", 0
msg_hbx_comma:          db ", computed 0x", 0
msg_hbx_bad_at:         db "hbx: bad image at 0x", 0
msg_hbx_section:        db "hbx: section ", 0
msg_hbx_wx_tail:        db " is writable and executable", 0x0A, 0
msg_hbx_cannot_load:    db "hbx: cannot load at 0x", 0
msg_hbx_noreloc_tail:   db ": no relocations", 0x0A, 0
msg_hbx_entry:          db "hbx: entry 0x", 0
msg_hbx_entry_tail:     db " is not executable", 0x0A, 0
msg_hbx_32bit:          db "hbx: this system is 32-bit", 0x0A, 0
msg_hbx_prefix:         db "hbx: ", 0
msg_hbx_bang:           db "!", 0
msg_hbx_import_tail:    db " not found", 0x0A, 0

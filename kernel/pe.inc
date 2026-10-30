;=======================================================================
; pe.inc - the PE32 loader (docs/spec/EXEC.md sections 1, 2 and 8)
;
; M5's reason to exist is that a program built for the parity target's
; ecosystem runs. That format is PE32, and the spec's promise is that adding it
; is one branch in the loader rather than one per caller. This file is that
; branch: image_load_program sees `MZ` and comes here.
;
; The subset is deliberately small, and each refusal below is a rule the
; fixtures in tools/pe_fixtures.py build an image for:
;
;   * `MZ` with `e_lfanew` pointing at `PE\0\0` inside the file;
;   * machine 0x014C (i386) - anything else names its own machine number;
;   * optional header magic 0x010B (PE32); 0x020B is PE32+ and is refused with
;     the same sentence a 64-bit HBX image gets;
;   * ImageBase equal to PROGRAM_BASE. Nothing relocates a PE32 image yet, so
;     one built elsewhere is refused by name instead of loaded in the wrong
;     place - the rule HBX images already live by;
;   * no import directory, because there is no library to resolve against yet;
;   * every section inside the file and inside SizeOfImage, sections in
;     increasing virtual address order that do not overlap, and none both
;     writable and executable (SECURITY.md section 8);
;   * an entry point inside an executable section.
;
; Loading is a copy, not a mapping, so the file has to get out of the image's
; way first: it is moved to the top of the program window, the image area is
; cleared, and each section is copied down from the moved file. Copying in
; place would work only while every section's raw data sat above the whole
; image, which is true of a wrapped flat payload and false of anything a linker
; emits. EBP holds where the file is - PROGRAM_BASE until it moves - because
; every field read is relative to it.
;=======================================================================

PE_DOS_LFANEW       equ 0x3C
PE_SIG              equ 0x00004550          ; 'PE',0,0
PE_MACHINE_I386     equ 0x014C
PE_MAGIC_32         equ 0x010B
PE_MAGIC_64         equ 0x020B
PE_MAX_SECTIONS     equ 16
PE_OPT_MIN          equ 96                  ; through NumberOfRvaAndSizes
PE_SCN_SIZE         equ 40

; Offsets from the start of the COFF header (just after the signature).
PE_COFF_MACHINE     equ 4
PE_COFF_SECTIONS    equ 6
PE_COFF_OPT_SIZE    equ 20                  ; SizeOfOptionalHeader

; Offsets from the start of the optional header (PE32 layout).
PE_OPT_MAGIC        equ 0
PE_OPT_ENTRY        equ 16                  ; AddressOfEntryPoint, an RVA
PE_OPT_IMAGE_BASE   equ 28
PE_OPT_SIZE_IMAGE   equ 56
PE_OPT_NUM_DIRS     equ 92                  ; NumberOfRvaAndSizes
PE_OPT_DIRS         equ 96                  ; DataDirectory[]
PE_DIR_IMPORT       equ 8                   ; DataDirectory[1].VirtualAddress

; Offsets from the start of a section table entry.
PE_SCN_NAME         equ 0                   ; 8 bytes, not NUL-terminated
PE_SCN_VSIZE        equ 8
PE_SCN_VA           equ 12
PE_SCN_RAWSIZE      equ 16
PE_SCN_RAWPTR       equ 20
PE_SCN_CHARS        equ 36
PE_SCN_EXECUTE      equ 0x20000000
PE_SCN_WRITE        equ 0x80000000

;-----------------------------------------------------------------------
; pe_load_program - verify a PE32 image in the window and copy it into place
;   In : EAX = bytes read into PROGRAM_BASE
;   Out: EAX = entry address, CF clear, and program_size/break are set
;        EAX = mv_status, CF set - the refusal is already reported
;
; Nothing is written until the whole header and every section have been
; accepted, so a refused image leaves the window exactly as it found it.
;-----------------------------------------------------------------------
pe_load_program:
        pushad
        mov ebp, PROGRAM_BASE                   ; the file, until it moves
        mov [pe_file_len], eax

        ; ---- the DOS header, and where it says the PE header is ----
        cmp eax, PE_DOS_LFANEW + 4
        jb .pe_bad
        mov eax, [ebp + PE_DOS_LFANEW]
        lea ecx, [eax + PE_COFF_OPT_SIZE]
        cmp ecx, [pe_file_len]
        ja .pe_bad
        cmp dword [ebp + eax], PE_SIG
        jne .pe_bad

        ; ---- the COFF header ----
        movzx ecx, word [ebp + eax + PE_COFF_MACHINE]
        mov [pe_machine], ecx
        cmp ecx, PE_MACHINE_I386
        jne .pe_machine
        movzx ecx, word [ebp + eax + PE_COFF_SECTIONS]
        test ecx, ecx
        jz .pe_bad
        cmp ecx, PE_MAX_SECTIONS
        ja .pe_bad
        mov [pe_sec_count], ecx
        movzx edx, word [ebp + eax + PE_COFF_OPT_SIZE]
        cmp edx, PE_OPT_MIN
        jb .pe_bad
        lea ecx, [eax + 24]                     ; optional header start
        mov [pe_opt], ecx
        lea ecx, [ecx + edx]                    ; section table start
        mov [pe_sec_table], ecx
        mov eax, [pe_sec_count]
        imul eax, eax, PE_SCN_SIZE
        add eax, ecx
        cmp eax, [pe_file_len]
        ja .pe_bad

        ; ---- the optional header ----
        mov esi, [pe_opt]
        movzx ecx, word [ebp + esi + PE_OPT_MAGIC]
        cmp ecx, PE_MAGIC_64
        je .pe_64
        cmp ecx, PE_MAGIC_32
        jne .pe_bad

        mov eax, [ebp + esi + PE_OPT_IMAGE_BASE]
        mov [pe_image_base], eax
        cmp eax, PROGRAM_BASE
        jne .pe_base

        mov eax, [ebp + esi + PE_OPT_SIZE_IMAGE]
        mov [pe_img_size], eax
        test eax, eax
        jz .pe_bad
        add eax, PROGRAM_BASE
        jc .pe_bad
        cmp eax, USER_STACK_BASE                ; the image must clear the stack
        ja .pe_bad

        ; DataDirectory[1] exists only if the header says so.
        cmp dword [ebp + esi + PE_OPT_NUM_DIRS], 2
        jb .pe_no_imports
        cmp dword [ebp + esi + PE_OPT_DIRS + PE_DIR_IMPORT], 0
        jne .pe_imports
.pe_no_imports:

        ; ---- the section table ----
        mov edi, [pe_sec_table]
        mov ecx, [pe_sec_count]
        mov dword [pe_prev_end], 0
.pe_section:
        mov [pe_cur], edi
        mov [pe_sec_left], ecx

        ; W^X, before anything else trusts this section's shape.
        mov eax, [ebp + edi + PE_SCN_CHARS]
        mov ecx, eax
        and eax, PE_SCN_EXECUTE
        jz .pe_scn_nowx
        and ecx, PE_SCN_WRITE
        jz .pe_scn_nowx
        mov esi, msg_pe_section
        call vga_print
        lea esi, [ebp + edi + PE_SCN_NAME]
        call hbx_print_section_name
        mov esi, msg_pe_wx_tail
        call vga_print
        jmp .pe_fail
.pe_scn_nowx:

        ; A section occupies as much of the image as its virtual size says,
        ; and at least as much as it carries bytes for.
        mov eax, [ebp + edi + PE_SCN_VA]
        mov [pe_va], eax
        cmp eax, [pe_img_size]
        ja .pe_bad
        mov ebx, [ebp + edi + PE_SCN_VSIZE]
        mov ecx, [ebp + edi + PE_SCN_RAWSIZE]
        cmp ebx, ecx
        jae .pe_scn_extent
        mov ebx, ecx
.pe_scn_extent:
        mov [pe_extent], ebx
        add eax, ebx
        jc .pe_bad
        cmp eax, [pe_img_size]
        ja .pe_bad

        ; Sections come in increasing virtual address order and do not overlap.
        mov eax, [pe_prev_end]
        cmp [pe_va], eax
        jb .pe_bad

        ; The raw data has to be inside the file.
        mov eax, [ebp + edi + PE_SCN_RAWPTR]
        mov ebx, [ebp + edi + PE_SCN_RAWSIZE]
        test ebx, ebx
        jz .pe_scn_no_raw
        add eax, ebx
        jc .pe_bad
        cmp eax, [pe_file_len]
        ja .pe_bad
.pe_scn_no_raw:

        ; Remember the highest virtual end, for the section that follows it.
        mov eax, [pe_va]
        add eax, [pe_extent]
        cmp eax, [pe_prev_end]
        jbe .pe_scn_end
        mov [pe_prev_end], eax
.pe_scn_end:
        mov edi, [pe_cur]
        add edi, PE_SCN_SIZE
        mov ecx, [pe_sec_left]
        dec ecx
        jnz .pe_section

        ; ---- the entry point has to be inside executable code ----
        mov esi, [pe_opt]
        mov eax, [ebp + esi + PE_OPT_ENTRY]
        mov [pe_entry], eax
        mov edi, [pe_sec_table]
        mov ecx, [pe_sec_count]
.pe_find_entry:
        mov eax, [ebp + edi + PE_SCN_CHARS]
        test eax, PE_SCN_EXECUTE
        jz .pe_find_next
        mov eax, [ebp + edi + PE_SCN_VA]
        mov ebx, [ebp + edi + PE_SCN_VSIZE]
        mov esi, [ebp + edi + PE_SCN_RAWSIZE]
        cmp ebx, esi
        jae .pe_entry_extent
        mov ebx, esi
.pe_entry_extent:
        add ebx, eax                            ; section end, exclusive
        mov esi, [pe_entry]
        cmp esi, eax
        jb .pe_find_next
        cmp esi, ebx
        jae .pe_find_next
        jmp .pe_entry_ok
.pe_find_next:
        add edi, PE_SCN_SIZE
        dec ecx
        jnz .pe_find_entry
        mov esi, msg_pe_entry
        call vga_print
        mov eax, [pe_entry]
        call vga_print_hex
        mov esi, msg_pe_entry_tail
        call vga_print
        jmp .pe_fail

.pe_entry_ok:
        ; ---- move the file out of the image's way ----
        ; The staging copy sits just below the stack guard and has to clear the
        ; image: an image and its file that do not both fit the window are
        ; refused rather than half copied.
        mov eax, [pe_file_len]
        add eax, 15
        and eax, ~15
        mov ebx, USER_STACK_BASE
        sub ebx, eax
        cmp ebx, PROGRAM_BASE
        jb .pe_nofit
        mov ecx, [pe_img_size]
        add ecx, PROGRAM_BASE
        cmp ebx, ecx
        jb .pe_nofit
        mov [pe_stage], ebx

        mov ecx, [pe_file_len]
        lea esi, [ebp + ecx - 1]                ; last byte of the file
        lea edi, [ebx + ecx - 1]                ; last byte of the staging copy
        std
        rep movsb
        cld
        mov ebp, [pe_stage]

        ; ---- clear the image, then copy the sections down into it ----
        mov edi, PROGRAM_BASE
        mov ecx, [pe_img_size]
        xor eax, eax
        cld
        rep stosb

        mov edi, [pe_sec_table]
        mov ecx, [pe_sec_count]
.pe_copy:
        mov [pe_cur], edi
        mov [pe_sec_left], ecx

        mov ecx, [ebp + edi + PE_SCN_RAWSIZE]
        test ecx, ecx
        jz .pe_copy_next
        mov esi, [ebp + edi + PE_SCN_RAWPTR]
        add esi, ebp
        mov edi, [ebp + edi + PE_SCN_VA]
        add edi, PROGRAM_BASE
        cld
        rep movsb
.pe_copy_next:
        mov edi, [pe_cur]
        add edi, PE_SCN_SIZE
        mov ecx, [pe_sec_left]
        dec ecx
        jnz .pe_copy

        ; ---- entry, size and the heap's start ----
        mov eax, [pe_img_size]
        mov [program_size], eax
        add eax, PROGRAM_BASE
        call set_program_brk_init
        mov eax, [pe_entry]
        add eax, PROGRAM_BASE
        clc
        jmp .pe_out

.pe_64:
        mov esi, msg_pe_64
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .pe_fail
.pe_machine:
        mov esi, msg_pe_machine
        call vga_print
        mov eax, [pe_machine]
        call vga_print_hex
        mov esi, msg_pe_machine_tail
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .pe_fail
.pe_base:
        mov esi, msg_pe_cannot_load
        call vga_print
        mov eax, [pe_image_base]
        call vga_print_hex
        mov esi, msg_pe_noreloc_tail
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .pe_fail
.pe_imports:
        mov esi, msg_pe_imports
        call vga_print
        mov eax, MV_DLL_NOT_FOUND
        jmp .pe_fail
.pe_nofit:
        mov esi, msg_pe_nofit
        call vga_print
        mov eax, MV_BAD_IMAGE
        jmp .pe_fail
.pe_bad:
        mov esi, msg_pe_bad
        call vga_print
        mov eax, MV_BAD_IMAGE
.pe_fail:
        stc
.pe_out:
        mov [esp + 28], eax                     ; EAX is the last pushad slot
        popad
        ret

pe_file_len:    dd 0
pe_machine:     dd 0
pe_opt:         dd 0
pe_sec_table:   dd 0
pe_sec_count:   dd 0
pe_image_base:  dd 0
pe_img_size:    dd 0
pe_entry:       dd 0
pe_prev_end:    dd 0
pe_cur:         dd 0
pe_sec_left:    dd 0
pe_va:          dd 0
pe_extent:      dd 0
pe_stage:       dd 0

msg_pe_bad:            db "pe: bad image in the header", 0x0A, 0
msg_pe_64:             db "pe: this system is 32-bit", 0x0A, 0
msg_pe_machine:        db "pe: machine 0x", 0
msg_pe_machine_tail:   db " is not i386", 0x0A, 0
msg_pe_cannot_load:    db "pe: cannot load at 0x", 0
msg_pe_noreloc_tail:   db ": no relocations", 0x0A, 0
msg_pe_section:        db "pe: section ", 0
msg_pe_wx_tail:        db " is writable and executable", 0x0A, 0
msg_pe_entry:          db "pe: entry 0x", 0
msg_pe_entry_tail:     db " is not executable", 0x0A, 0
msg_pe_imports:        db "pe: imports cannot be resolved yet", 0x0A, 0
msg_pe_nofit:          db "pe: image and file do not both fit the window", 0x0A, 0

;=======================================================================
; PHYSICAL MEMORY MANAGER (Bitmap Allocator)
;=======================================================================

pmm_init:
        pushad

        ; First, mark all memory as used
        mov edi, PMM_BITMAP
        mov ecx, 0x20000 / 4   ; 128KB bitmap = covers 4GB (128K * 8 * 4096)
        mov eax, 0xFFFFFFFF
        rep stosd

        ; Now parse E820 memory map and free usable regions
        mov ecx, [BOOTINFO_MMAP_CNT]
        mov esi, [BOOTINFO_MMAP_PTR]
        or ecx, ecx
        jz .no_map

.map_loop:
        ; E820 entry: base(8), length(8), type(4), [acpi(4)]
        mov eax, [esi + 16]     ; Type
        cmp eax, 1              ; Type 1 = usable
        jne .next

        ; Get base and length (use lower 32 bits only for now)
        mov eax, [esi]          ; Base low
        mov ebx, [esi + 8]     ; Length low

        ; Skip memory below 4MB (reserved for kernel/drivers)
        cmp eax, 0x00400000
        jge .free_region
        ; Adjust if region overlaps 4MB
        mov edx, 0x00400000
        sub edx, eax
        cmp edx, ebx
        jge .next              ; Entire region below 4MB
        sub ebx, edx
        mov eax, 0x00400000

.free_region:
        ; Free pages from EAX with length EBX
        call pmm_free_region

.next:
        add esi, 24
        loop .map_loop

.no_map:
        ; Count free pages
        call pmm_count_free
        mov [total_free_pages], eax

        popad
        ret

; Free a region: EAX = base address, EBX = length in bytes
pmm_free_region:
        pushad
        shr eax, 12             ; Convert to page number
        shr ebx, 12             ; Convert to page count
        or ebx, ebx
        jz .done

.loop:
        ; Clear bit in bitmap (page_num / 8 = byte, page_num % 8 = bit)
        mov ecx, eax
        shr ecx, 3              ; Byte offset
        mov edx, eax
        and edx, 7              ; Bit offset

        mov edi, PMM_BITMAP
        add edi, ecx

        ; Create mask to clear the bit
        mov cl, dl
        mov ch, 1
        shl ch, cl
        not ch
        and [edi], ch            ; Clear the bit = free

        inc eax
        dec ebx
        jnz .loop

.done:
        popad
        ret

; Allocate a physical page
; Returns: EAX = physical address (0 = out of memory)
pmm_alloc_page:
        pushad
        mov esi, PMM_BITMAP
        mov ecx, 0x20000        ; Scan 128KB of bitmap

.scan:
        mov al, [esi]
        cmp al, 0xFF            ; All used?
        je .next_byte
        ; Find first zero bit
        mov edx, 0
.find_bit:
        bt eax, edx
        jnc .found
        inc edx
        cmp edx, 8
        jl .find_bit

.next_byte:
        inc esi
        loop .scan

        ; Out of memory
        popad
        xor eax, eax
        ret

.found:
        ; Set the bit (mark as used)
        bts dword [esi], edx

        ; Calculate page address
        sub esi, PMM_BITMAP
        shl esi, 3              ; Byte offset * 8
        add esi, edx            ; + bit offset = page number
        shl esi, 12             ; * 4096 = physical address

        mov [esp + 28], esi     ; Return via EAX in pushad frame
        popad
        ret

; Free a physical page
; EAX = physical address
pmm_free_page:
        pushad
        shr eax, 12             ; Page number
        mov ecx, eax
        shr ecx, 3              ; Byte offset
        and eax, 7              ; Bit offset

        mov edi, PMM_BITMAP
        add edi, ecx
        mov cl, al
        mov ch, 1
        shl ch, cl
        not ch
        and [edi], ch            ; Clear bit = free

        popad
        ret

; Allocate N contiguous physical pages
; ECX = number of pages to allocate
; Returns: EAX = physical address of first page (0 = failure)
pmm_alloc_pages:
        pushad
        cmp ecx, 0
        je .ap_fail
        mov ebp, ecx            ; EBP = pages needed
        mov esi, PMM_BITMAP
        mov ecx, 0x20000        ; Scan 128KB bitmap
        xor ebx, ebx            ; EBX = current page number

.ap_scan:
        movzx eax, byte [esi]
        cmp al, 0xFF
        je .ap_skip_byte

        ; Check each bit in this byte
        xor edx, edx
.ap_check_bit:
        bt eax, edx
        jc .ap_bit_used

        ; This bit is free - check if we can get EBP contiguous from here
        push esi
        push edx
        push ebx
        mov ecx, ebp            ; Need this many contiguous
        ; Current page = (esi - PMM_BITMAP)*8 + edx
        sub esi, PMM_BITMAP
        shl esi, 3
        add esi, edx            ; ESI = starting page number
        mov edi, esi            ; EDI = starting page number

.ap_contig_check:
        cmp ecx, 0
        je .ap_found
        ; Check if page ESI is free
        mov eax, esi
        shr eax, 3              ; Byte index
        push ecx
        mov ecx, esi
        and ecx, 7              ; Bit index
        bt dword [PMM_BITMAP + eax], ecx
        pop ecx
        jc .ap_contig_fail
        inc esi
        dec ecx
        jmp .ap_contig_check

.ap_found:
        ; Mark all pages [EDI..EDI+EBP-1] as used
        mov ecx, ebp
        mov esi, edi
.ap_mark:
        mov eax, esi
        shr eax, 3
        push ecx
        mov ecx, esi
        and ecx, 7
        bts dword [PMM_BITMAP + eax], ecx
        pop ecx
        inc esi
        loop .ap_mark

        ; Return address = EDI * 4096
        shl edi, 12
        pop ebx
        pop edx
        pop esi
        mov [esp + 28], edi     ; Return via EAX in pushad frame
        popad
        ret

.ap_contig_fail:
        pop ebx
        pop edx
        pop esi

.ap_bit_used:
        inc edx
        cmp edx, 8
        jl .ap_check_bit

.ap_skip_byte:
        inc esi
        add ebx, 8
        dec ecx
        jnz .ap_scan

.ap_fail:
        popad
        xor eax, eax
        ret

; Free N contiguous physical pages
; EAX = base physical address, ECX = number of pages
pmm_free_pages:
        pushad
        shr eax, 12             ; Convert base to page number
        mov [.fp_page], eax
        mov [.fp_count], ecx
.fp_loop:
        cmp dword [.fp_count], 0
        je .fp_done
        mov eax, [.fp_page]
        mov ecx, eax
        shr ecx, 3              ; Byte offset in bitmap
        and eax, 7              ; Bit offset
        mov edi, PMM_BITMAP
        add edi, ecx
        mov cl, al
        mov ch, 1
        shl ch, cl
        not ch
        and [edi], ch            ; Clear bit = free
        inc dword [.fp_page]
        dec dword [.fp_count]
        jmp .fp_loop
.fp_done:
        popad
        ret
.fp_page:  dd 0
.fp_count: dd 0

; Count free pages
; Returns: EAX = number of free pages
pmm_count_free:
        push ecx
        push edx
        push esi
        xor eax, eax
        mov esi, PMM_BITMAP
        mov ecx, 0x20000        ; 128KB bitmap

.loop:
        movzx edx, byte [esi]
        not dl
        ; Count set bits in DL (inverted = free pages)
.count:
        test dl, dl
        jz .next
        mov dh, dl
        dec dh
        and dl, dh              ; Clear lowest set bit
        inc eax
        jmp .count

.next:
        inc esi
        loop .loop

        pop esi
        pop edx
        pop ecx
        ret

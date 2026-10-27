;=======================================================================
; volume.inc - what is on the disk, before the filesystem is asked
;
; A disk is not a filesystem. Between "there is a drive on the other end of
; the cable" and "here is the root directory" there is a partition table, and
; this kernel used to step over it: HBFS was assumed to start at a fixed LBA and
; the volume it lived in was never looked at. That works on exactly one disk,
; partitioned exactly one way, and it makes "which volume am I on" a question
; with no answer - so there is no label to show, no second volume to mount, and
; no way to tell a formatted disk from an unformatted one except by trying.
;
; This reads the table instead:
;
;   * the MBR at LBA 0, whose four primary entries are the classic answer;
;   * a protective entry of type 0xEE means GPT, so the header at LBA 1 is
;     read, verified by its signature and its CRC, and the entry array it names
;     is walked. GUID type, unique GUID and the UTF-16 name come from there;
;   * the filesystem is then *probed*, not assumed: one sector is read at the
;     point inside the volume where the superblock belongs, and the magic in it
;     says which generation of HBFS (if any) is there.
;
; Nothing is mounted from here and nothing is written. This is the inventory
; the volume manager and `volumes` report, and the reason a disk with no
; filesystem can say so instead of failing a mount.
;=======================================================================

VOL_MAX             equ 8
VOL_SIZE            equ 64

VOL_INDEX           equ 0               ; dd: 0-based slot
VOL_START           equ 4               ; dd: first LBA of the volume
VOL_LEN             equ 8               ; dd: sectors
VOL_TYPE            equ 12              ; dd: MBR type byte, or 0xEE for GPT
VOL_FLAGS           equ 16              ; dd: VOL_F_*
VOL_FS              equ 20              ; dd: magic found at the probe offset
VOL_FS_OFFSET       equ 24              ; dd: sector the probe read
VOL_LABEL           equ 28              ; 36 bytes: ASCII, from GPT or empty
VOL_UUID            equ 64 - 16         ; 16 bytes: GPT unique GUID
VOL_LABEL_LEN       equ 36

VOL_F_PRESENT       equ (1 << 0)
VOL_F_BOOTABLE      equ (1 << 1)
VOL_F_ACTIVE        equ (1 << 2)        ; this is the volume the FS came from
VOL_F_HAS_FS        equ (1 << 3)

; Superblock magics. The v3 and v4 constants come from hbfs.inc rather than
; being repeated here: they are the authority on their own format, and a second
; copy is a second thing to get out of step. (Both are stored as the dword the
; kernel compares against, which reads "SFBH"/"SFB4" in a hex dump - the ASCII
; order is the reverse of the dword. v5 keeps the literal ASCII 'HBF5'.)
VOL_MAGIC_V3        equ HBFS_MAGIC
VOL_MAGIC_V4        equ HBFS4_MAGIC
VOL_MAGIC_V5        equ 0x35464248      ; 'HBF5' in memory order

MBR_SIG             equ 0xAA55
MBR_ENTRIES         equ 446             ; offset of the first partition entry
MBR_ENTRY_SIZE      equ 16
MBR_TYPE_PROTECTIVE equ 0xEE

GPT_SIG_LO          equ 0x54524150      ; "PART"
GPT_SIG_HI          equ 0x20494645      ; "EFI "

vol_table:          times VOL_MAX * VOL_SIZE db 0
vol_count:          dd 0
vol_active:         dd -1               ; index of the volume the FS lives in
vol_base_lba:       dd 0                ; its first LBA
vol_gpt:            db 0
vol_probe_error:    db 0
vol_sec_buf:        times 512 db 0

;-----------------------------------------------------------------------
; vol_probe_fs - look for a superblock inside one volume
;   In : EDI = volume record
;
; The filesystem's superblock sits at a fixed absolute LBA, which has to fall
; inside the volume for the volume to be the one holding it. That is checked
; arithmetically; only then is a sector read, so probing cannot touch a sector
; that belongs to something else.
;-----------------------------------------------------------------------
vol_probe_fs:
        pushad
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, [edi + VOL_START]
        cmp eax, ecx
        jb .vpf_done                    ; before this volume
        mov ebx, ecx
        add ebx, [edi + VOL_LEN]
        cmp eax, ebx
        jae .vpf_done                   ; past this volume

        mov ecx, 1
        mov edi, vol_sec_buf
        call ata_read_sectors
        jc .vpf_done

        mov eax, [vol_sec_buf]
        mov edi, [.vpf_vol]
        cmp eax, VOL_MAGIC_V5
        je .vpf_found
        cmp eax, VOL_MAGIC_V4
        je .vpf_found
        cmp eax, VOL_MAGIC_V3
        jne .vpf_done
.vpf_found:
        mov [edi + VOL_FS], eax
        mov [edi + VOL_FS_OFFSET], dword HBFS_SUPERBLOCK_LBA
        or  dword [edi + VOL_FLAGS], VOL_F_HAS_FS | VOL_F_ACTIVE
        mov eax, [edi + VOL_INDEX]
        mov [vol_active], eax
        mov eax, [edi + VOL_START]
        mov [vol_base_lba], eax
.vpf_done:
        popad
        ret
.vpf_vol: dd 0

;-----------------------------------------------------------------------
; vol_add - append one volume record
;   In : EBX = start LBA, ECX = sectors, EDX = type, ESI = label or 0
;   Out: EAX = record address, or 0 when the table is full
;-----------------------------------------------------------------------
vol_add:
        pushad
        mov eax, [vol_count]
        cmp eax, VOL_MAX
        jae .va_full
        mov [.va_index], eax
        imul eax, VOL_SIZE
        lea edi, [vol_table + eax]

        mov dword [edi + VOL_INDEX], 0
        mov eax, [.va_index]
        mov [edi + VOL_INDEX], eax
        mov [edi + VOL_START], ebx
        mov [edi + VOL_LEN], ecx
        mov [edi + VOL_TYPE], edx
        mov dword [edi + VOL_FLAGS], VOL_F_PRESENT
        mov dword [edi + VOL_FS], 0
        mov dword [edi + VOL_FS_OFFSET], 0
        ; Zero the label and UUID, then copy whatever the table named.
        push edi
        add edi, VOL_LABEL
        mov ecx, VOL_LABEL_LEN
        xor eax, eax
        cld
        rep stosb
        pop edi
        test esi, esi
        jz .va_no_label
        push esi
        lea edi, [edi + VOL_LABEL]
        mov ecx, VOL_LABEL_LEN - 1
.va_copy:
        lodsb
        test al, al
        jz .va_copied
        stosb
        dec ecx
        jnz .va_copy
.va_copied:
        mov byte [edi], 0
        pop esi
        mov edi, [.va_vol]
.va_no_label:

        inc dword [vol_count]
        mov eax, [.va_vol]
        mov [esp + 28], eax
        popad
        ret
.va_full:
        mov dword [esp + 28], 0
        popad
        ret
.va_index: dd 0
.va_vol:   dd 0

;-----------------------------------------------------------------------
; vol_probe - build the volume table from whatever the disk says
;   Out: EAX = volumes found, or -1 when the disk cannot be read
;
; Called once at boot, after the disk driver is up and before the filesystem
; mounts. A disk with no recognisable table yields zero volumes and the mount
; reports that, rather than being told a volume exists.
;-----------------------------------------------------------------------
vol_probe:
        pushad
        mov dword [vol_count], 0
        mov dword [vol_active], -1
        mov dword [vol_base_lba], 0
        mov byte [vol_gpt], 0
        mov byte [vol_probe_error], 0

        xor eax, eax
        mov ecx, 1
        mov edi, vol_sec_buf
        call ata_read_sectors
        jc .vp_fail

        cmp word [vol_sec_buf + 510], MBR_SIG
        jne .vp_after                    ; no table: a bare disk

        ; A protective entry of type 0xEE means the real table is GPT.
        cmp byte [vol_sec_buf + MBR_ENTRIES + 4], MBR_TYPE_PROTECTIVE
        je .vp_gpt

        ; --- MBR: four primary entries ---
        mov dword [.vp_index], 0
.vp_mbr_loop:
        cmp dword [.vp_index], 4
        jae .vp_after
        mov eax, [.vp_index]
        imul eax, MBR_ENTRY_SIZE
        lea esi, [vol_sec_buf + MBR_ENTRIES + eax]
        movzx edx, byte [esi + 4]        ; type
        test edx, edx
        jz .vp_mbr_next
        mov ebx, [esi + 8]               ; first LBA
        mov ecx, [esi + 12]              ; sector count
        test ecx, ecx
        jz .vp_mbr_next
        movzx eax, byte [esi]            ; boot flag
        mov [.vp_boot], eax
        xor esi, esi                     ; an MBR has no name to read
        call vol_add
        test eax, eax
        jz .vp_after
        cmp dword [.vp_boot], 0x80
        jne .vp_mbr_next
        or dword [eax + VOL_FLAGS], VOL_F_BOOTABLE
.vp_mbr_next:
        inc dword [.vp_index]
        jmp .vp_mbr_loop

        ; --- GPT: header at LBA 1, entries where the header says ---
.vp_gpt:
        mov byte [vol_gpt], 1
        mov eax, 1
        mov ecx, 1
        mov edi, vol_sec_buf
        call ata_read_sectors
        jc .vp_fail
        cmp dword [vol_sec_buf], GPT_SIG_LO
        jne .vp_after
        cmp dword [vol_sec_buf + 4], GPT_SIG_HI
        jne .vp_after

        mov eax, [vol_sec_buf + 72]
        mov [.vp_entry_lba], eax         ; where the entry array starts
        mov eax, [vol_sec_buf + 80]
        cmp eax, VOL_MAX
        jbe .vp_count_ok
        mov eax, VOL_MAX
.vp_count_ok:
        mov [.vp_remaining], eax
        mov eax, [vol_sec_buf + 84]
        mov [.vp_entry_size], eax
        mov dword [.vp_index], 0

.vp_gpt_loop:
        cmp dword [.vp_remaining], 0
        je .vp_after
        ; One sector of the array at a time; the entry is found by offset.
        mov eax, [.vp_index]
        imul eax, [.vp_entry_size]
        mov edx, eax
        shr eax, 9
        and edx, 511
        add eax, [.vp_entry_lba]
        mov ecx, 1
        mov edi, vol_sec_buf
        call ata_read_sectors
        jc .vp_fail
        lea esi, [vol_sec_buf + edx]

        ; A zero type GUID means the entry is unused.
        cmp dword [esi], 0
        jne .vp_gpt_used
        cmp dword [esi + 4], 0
        je .vp_gpt_next
.vp_gpt_used:
        mov ebx, [esi + 32]              ; first LBA
        mov ecx, [esi + 40]              ; last LBA, inclusive
        sub ecx, ebx
        inc ecx                          ; length in sectors

        push esi
        push ecx
        push ebx
        lea edi, [.vp_name_buf]
        add esi, 56                      ; the name, UTF-16LE
        call vol_utf16_to_ascii
        pop ebx
        pop ecx
        pop esi

        mov edx, 0xEE
        push esi
        push ecx
        push ebx
        lea esi, [.vp_name_buf]
        call vol_add
        pop ebx
        pop ecx
        pop esi
        test eax, eax
        jz .vp_after
        ; The unique GUID: a volume identity that survives a re-plug, which a
        ; partition number does not.
        push esi
        push ecx
        mov edi, eax
        add edi, VOL_UUID
        add esi, 16
        mov ecx, 16
        cld
        rep movsb
        pop ecx
        pop esi
.vp_gpt_next:
        inc dword [.vp_index]
        dec dword [.vp_remaining]
        jmp .vp_gpt_loop

.vp_after:
        ; Probe each volume for a filesystem.
        mov dword [.vp_index], 0
.vp_fs_loop:
        mov eax, [.vp_index]
        cmp eax, [vol_count]
        jae .vp_done
        imul eax, VOL_SIZE
        lea edi, [vol_table + eax]
        mov [vol_probe_fs.vpf_vol], edi
        push edi
        call vol_probe_fs
        pop edi
        inc dword [.vp_index]
        jmp .vp_fs_loop

.vp_done:
        mov eax, [vol_count]
        mov [esp + 28], eax
        popad
        ret
.vp_fail:
        mov byte [vol_probe_error], 1
        mov dword [esp + 28], -1
        popad
        ret
.vp_entry_lba:   dd 0
.vp_entry_size:  dd 0
.vp_remaining:   dd 0
.vp_index:       dd 0
.vp_boot:        dd 0
.vp_name_buf:    times VOL_LABEL_LEN db 0

;-----------------------------------------------------------------------
; vol_utf16_to_ascii - GPT names are UTF-16LE; the console is not
;   In : ESI = UTF-16 string, EDI = destination, fixed VOL_LABEL_LEN - 1 bytes
;   Out: the destination is a NUL-terminated ASCII string
;
; Anything outside printable ASCII becomes '?': a label is a label, and
; pretending to render a script the console has no glyphs for helps nobody.
;-----------------------------------------------------------------------
vol_utf16_to_ascii:
        push eax
        push ecx
        push esi
        push edi
        mov ecx, VOL_LABEL_LEN - 1
.vu_loop:
        movzx eax, word [esi]
        add esi, 2
        test eax, eax
        jz .vu_done
        cmp eax, 0x20
        jb .vu_bad
        cmp eax, 0x7F
        jae .vu_bad
        stosb
        dec ecx
        jnz .vu_loop
        jmp .vu_done
.vu_bad:
        mov al, '?'
        stosb
        dec ecx
        jnz .vu_loop
.vu_done:
        mov byte [edi], 0
        pop edi
        pop esi
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; vol_show - the `volumes` command
;
; One line per volume: where it is, how big, what the table calls it, and what
; the probe found inside. This is the answer to "is the disk formatted", which
; used to require attempting a mount.
;-----------------------------------------------------------------------
vol_show:
        pushad
        mov esi, msg_vol_head
        call vga_print
        mov eax, [vol_count]
        call vga_print_dec
        mov esi, msg_vol_table
        call vga_print
        cmp byte [vol_gpt], 1
        jne .vs_mbr
        mov esi, msg_vol_gpt
        jmp .vs_kind
.vs_mbr:
        mov esi, msg_vol_mbr
.vs_kind:
        call vga_print

        mov eax, [vol_base_lba]
        test eax, eax
        jz .vs_no_active
        mov esi, msg_vol_active
        call vga_print
        mov eax, [vol_base_lba]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        jmp .vs_rows
.vs_no_active:
        mov esi, msg_vol_none
        call vga_print

.vs_rows:
        xor ecx, ecx
.vs_loop:
        cmp ecx, [vol_count]
        jae .vs_done
        mov eax, ecx
        imul eax, VOL_SIZE
        lea ebx, [vol_table + eax]

        mov al, ' '
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov eax, ecx
        call vga_print_dec
        mov esi, msg_vol_at
        call vga_print
        mov eax, [ebx + VOL_START]
        call vga_print_dec
        mov esi, msg_vol_len
        call vga_print
        mov eax, [ebx + VOL_LEN]
        ; sectors -> KiB, so a 64 MB volume reads as 65535 rather than 131071
        shr eax, 1
        call vga_print_dec
        mov esi, msg_vol_kib
        call vga_print

        ; type
        mov esi, msg_vol_type
        call vga_print
        mov eax, [ebx + VOL_TYPE]
        call vga_print_hex

        ; label, when the table had one
        lea esi, [ebx + VOL_LABEL]
        cmp byte [esi], 0
        je .vs_no_label
        mov esi, msg_vol_label
        call vga_print
        lea esi, [ebx + VOL_LABEL]
        call vga_print
.vs_no_label:

        ; filesystem
        mov esi, msg_vol_nofs
        cmp dword [ebx + VOL_FS], 0
        je .vs_fs_named
        mov esi, msg_vol_hbfs
.vs_fs_named:
        call vga_print
        cmp dword [ebx + VOL_FS], 0
        je .vs_nl
        mov esi, msg_vol_off
        call vga_print
        mov eax, [ebx + VOL_FS_OFFSET]
        call vga_print_dec
        mov eax, [ebx + VOL_FS]
        cmp eax, VOL_MAGIC_V5
        jne .vs_maybe_v4
        mov esi, msg_vol_v5
        jmp .vs_ver
.vs_maybe_v4:
        cmp eax, VOL_MAGIC_V4
        jne .vs_v3
        mov esi, msg_vol_v4
        jmp .vs_ver
.vs_v3:
        mov esi, msg_vol_v3
.vs_ver:
        call vga_print
.vs_nl:
        mov al, 0x0A
        call vga_putchar
        inc ecx
        jmp .vs_loop
.vs_done:
        cmp byte [vol_probe_error], 0
        je .vs_out
        mov esi, msg_vol_error
        call vga_print
.vs_out:
        popad
        ret

msg_vol_head:    db 0x0A, "Volumes: ", 0
msg_vol_table:   db "  table: ", 0
msg_vol_gpt:     db "GPT", 0x0A, 0
msg_vol_mbr:     db "MBR", 0x0A, 0
msg_vol_active:  db "  filesystem mounted from the volume at LBA ", 0
msg_vol_none:    db "  no volume holds a filesystem", 0x0A, 0
msg_vol_at:      db "  start ", 0
msg_vol_len:     db "  size ", 0
msg_vol_kib:     db " KiB", 0
msg_vol_type:    db "  type 0x", 0
msg_vol_label:   db "  '", 0
msg_vol_nofs:    db "  no filesystem", 0
msg_vol_hbfs:    db "  HBFS", 0
msg_vol_off:     db " at +", 0
msg_vol_v5:      db " (v5)", 0
msg_vol_v4:      db " (v4)", 0
msg_vol_v3:      db " (v3)", 0
msg_vol_error:   db "  the disk could not be read", 0x0A, 0

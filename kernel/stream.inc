;=======================================================================
; stream.inc - alternate data streams on HBFS
;
; A stream is a second name for second content: "report.txt:draft" is the
; draft stream of report.txt, with its own size, blocks, permissions and
; xattrs. It is stored as a directory entry of its own, type FTYPE_STREAM,
; named "<base>:<stream>" - the same namespace the path resolver already
; searches, so `cat report.txt:draft` works without the VFS knowing streams
; exist.
;
; That representation is what the reader and the journal already understand,
; and the cost it carries is paid here rather than in every reader:
;
;   * the entry is hidden from listings (`dir`, readdir), because a stream is
;     part of a file and not a sibling of it;
;   * deleting a base entry deletes every entry named "<base>:..." in the same
;     transaction, so a delete is all of the file or none of it;
;   * renaming a base entry renames them too, in the same transaction, and a
;     new name whose stream namespace is occupied is refused rather than
;     merged;
;   * an entry whose base is missing is an orphan; nothing here creates one,
;     and tools/fsck_hbfs.py reports it.
;
; Nothing on disk changed: a stream entry is a normal v3 entry with a type an
; older kernel would not know - which is why the type has to be taught to the
; checker and the repair walk as well as to this file.
;=======================================================================

STREAM_NAME_MAX  equ 64          ; a stream name is short by design
STREAM_BASE_MAX  equ 180         ; 180 + ':' + 64 + NUL fits HBFS's 252

;-----------------------------------------------------------------------
; stream_compose - build "base:stream" in stream_name_buf
;   In : ESI = base name, EDI = stream name
;   Out: CF = 0 and EAX = pointer to the composed name
;        CF = 1 if either name is unusable (empty, too long, or carrying a
;        character that would collide with the namespace: ':' or '/')
;-----------------------------------------------------------------------
stream_compose:
        pushad
        mov ebx, edi                    ; EBX = stream source

        ; --- stream name ---
        xor edx, edx
.sc_loop:
        mov al, [ebx + edx]
        test al, al
        jz .sc_done
        cmp al, 0x20
        jbe .bad                        ; control characters and space
        cmp al, 0x7F
        jae .bad
        cmp al, ':'
        je .bad
        cmp al, '/'
        je .bad
        inc edx
        cmp edx, STREAM_NAME_MAX
        ja .bad
        jmp .sc_loop
.sc_done:
        test edx, edx
        jz .bad

        ; --- base name ---
        xor ecx, ecx
.sb_loop:
        mov al, [esi + ecx]
        test al, al
        jz .sb_done
        cmp al, ':'
        je .bad
        inc ecx
        cmp ecx, STREAM_BASE_MAX
        ja .bad
        jmp .sb_loop
.sb_done:
        test ecx, ecx
        jz .bad

        ; --- base + ':' + stream ---
        mov edi, stream_name_buf
        rep movsb                       ; base; ESI now past its NUL
        mov al, ':'
        stosb
        mov esi, ebx
        mov ecx, edx
        rep movsb
        xor al, al
        stosb

        mov eax, stream_name_buf
        mov [esp + 28], eax             ; EAX in the pushad frame
        popad
        clc
        ret
.bad:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; stream_prefix - build "base:" in stream_name2_buf
;   In : ESI = base name
;   Out: CF = 0 with the prefix built, CF = 1 if the name cannot have streams
;-----------------------------------------------------------------------
stream_prefix:
        pushad
        mov edi, stream_name2_buf
        xor ecx, ecx
.sp_loop:
        mov al, [esi + ecx]
        test al, al
        jz .sp_done
        cmp al, ':'
        je .bad
        cmp ecx, STREAM_BASE_MAX
        ja .bad
        mov [edi + ecx], al
        inc ecx
        jmp .sp_loop
.sp_done:
        test ecx, ecx
        jz .bad
        mov byte [edi + ecx], ':'
        mov byte [edi + ecx + 1], 0
        popad
        clc
        ret
.bad:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; stream_prefix_match - does an entry name belong to a prefix?
;   In : ESI = entry name, EDI = prefix (ends in ':')
;   Out: CF = 0 when the name starts with the prefix and continues past it
;        CF = 1 otherwise (including a name that is exactly the prefix)
;
; ESI and EDI are preserved; the caller keeps scanning with them.
;-----------------------------------------------------------------------
stream_prefix_match:
        push esi
        push edi
.spm_loop:
        mov al, [edi]
        test al, al
        jz .spm_end
        cmp al, [esi]
        jne .no
        inc esi
        inc edi
        jmp .spm_loop
.spm_end:
        cmp byte [esi], 0
        je .no                          ; the prefix itself is not a stream
        pop edi
        pop esi
        clc
        ret
.no:
        pop edi
        pop esi
        stc
        ret

;-----------------------------------------------------------------------
; stream_base_ok - is there a file here that can carry streams?
;   In : ESI = base name
;   Out: CF = 0 if the entry exists and is not a directory or a stream
;-----------------------------------------------------------------------
stream_base_ok:
        pushad
        call hbfs_load_root_dir
        jc .bad
        call hbfs_find_file
        jc .bad
        movzx eax, byte [edi + HBFS_DE_TYPE]
        cmp eax, FTYPE_FREE
        je .bad
        cmp eax, FTYPE_DIR
        je .bad
        cmp eax, FTYPE_STREAM
        je .bad
        popad
        clc
        ret
.bad:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; stream_find - find one stream's directory entry
;   In : ESI = base name, EDI = stream name
;   Out: CF = 0 and EDI = pointer into the loaded directory buffer
;-----------------------------------------------------------------------
stream_find:
        pushad
        call stream_compose
        jc .bad
        mov esi, eax
        call hbfs_load_root_dir
        jc .bad
        call hbfs_find_file
        jc .bad
        mov [esp], edi                  ; EDI in the pushad frame
        popad
        clc
        ret
.bad:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; stream_create - create or replace a stream
;   In : ESI = base name, EDI = stream name, EDX = size, ECX = data
;   Out: CF = 0 written, CF = 1 refused (no base, bad name, write failure)
;
; Writing the stream is a write to the file, so the file's own write
; permission is what is asked for - not just the directory's.
;-----------------------------------------------------------------------
stream_create:
        pushad
        mov [.size], edx
        mov [.data], ecx
        call stream_base_ok
        jc .bad
        mov ebx, HBFS_ACC_WRITE
        call hbfs_access_current
        test eax, eax
        jnz .bad
        call stream_compose
        jc .bad
        mov esi, eax
        mov ecx, [.size]
        mov edi, [.data]
        mov edx, FTYPE_STREAM
        call hbfs_create_file
        jc .bad
        popad
        clc
        ret
.bad:
        popad
        stc
        ret
.size:  dd 0
.data:  dd 0

;-----------------------------------------------------------------------
; stream_delete - remove one stream
;   In : ESI = base name, EDI = stream name
;   Out: CF = 0 removed, CF = 1 missing or not removable
;-----------------------------------------------------------------------
stream_delete:
        pushad
        call stream_find
        jc .bad
        call hbfs_delete_file_entry
        jc .bad
        popad
        clc
        ret
.bad:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; stream_namespace_taken - does any stream of this name exist?
;   In : ESI = base name
;   Out: CF = 1 if a "<base>:..." entry is in the current directory
;
; Used by rename: taking a name whose stream namespace is occupied would put
; two files' streams under one base, and neither file could be deleted whole.
;-----------------------------------------------------------------------
stream_namespace_taken:
        pushad
        call stream_prefix
        jc .free
        call hbfs_load_root_dir
        jc .free
        mov esi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
.snt_scan:
        cmp byte [esi + HBFS_DE_TYPE], FTYPE_STREAM
        jne .snt_next
        mov edi, stream_name2_buf
        call stream_prefix_match
        jnc .taken
.snt_next:
        add esi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .snt_scan
.free:
        popad
        clc
        ret
.taken:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; stream_sweep_delete - delete every stream of the entry in EDI
;   In : EDI = base entry (in the loaded directory buffer)
;   Out: CF = 0 all gone (or none existed), CF = 1 one could not be removed
;
; Each stream is a normal delete, nested in the caller's transaction, so the
; base and its streams are one journal commit.
;-----------------------------------------------------------------------
stream_sweep_delete:
        pushad
        mov esi, edi                    ; the entry's name
        call stream_prefix
        jc .ok
        mov esi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
.ssd_scan:
        cmp byte [esi + HBFS_DE_TYPE], FTYPE_STREAM
        jne .ssd_next
        mov edi, stream_name2_buf
        call stream_prefix_match
        jc .ssd_next
        mov edi, esi
        call hbfs_delete_file_entry
        jc .fail
.ssd_next:
        add esi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .ssd_scan
.ok:
        popad
        clc
        ret
.fail:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; stream_sweep_rename - move every stream of a base to a new base name
;   In : ESI = old base name, EDI = new base name
;   Out: CF = 0 renamed, CF = 1 one could not be renamed
;
; One stream at a time, by name: after a rename the entry no longer matches
; the old prefix, so the next scan finds the next one and the loop ends when
; none is left. The caller holds the transaction, so the whole set moves or
; none of it does.
;-----------------------------------------------------------------------
stream_sweep_rename:
        pushad
        mov [.old], esi
        mov [.new], edi
.ssr_again:
        mov esi, [.old]
        call stream_prefix
        jc .ok
        call hbfs_load_root_dir
        jc .ok
        mov esi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
.ssr_scan:
        cmp byte [esi + HBFS_DE_TYPE], FTYPE_STREAM
        jne .ssr_next
        mov edi, stream_name2_buf
        call stream_prefix_match
        jc .ssr_next

        ; --- this one moves: full name and suffix ---
        mov edi, stream_name_buf
.ssr_copy_name:
        lodsb
        stosb
        test al, al
        jnz .ssr_copy_name

        ; The suffix begins after the old prefix, whose length is the base
        ; name plus the colon.
        mov esi, stream_name2_buf
        xor ecx, ecx
.ssr_prefix_len:
        cmp byte [esi + ecx], 0
        je .ssr_suffix
        inc ecx
        jmp .ssr_prefix_len
.ssr_suffix:
        mov esi, stream_name_buf
        add esi, ecx
        mov edi, stream_suffix_buf
        xor edx, edx
.ssr_suffix_copy:
        mov al, [esi + edx]
        mov [edi + edx], al
        test al, al
        jz .ssr_new_name
        inc edx
        cmp edx, STREAM_NAME_MAX
        ja .fail
        jmp .ssr_suffix_copy

.ssr_new_name:
        ; --- new name = new base + ':' + suffix ---
        mov esi, [.new]
        mov edi, stream_new_buf
        xor ecx, ecx
.ssr_newbase:
        mov al, [esi + ecx]
        test al, al
        jz .ssr_newcolon
        mov [edi + ecx], al
        inc ecx
        cmp ecx, STREAM_BASE_MAX
        ja .fail
        jmp .ssr_newbase
.ssr_newcolon:
        mov byte [edi + ecx], ':'
        inc ecx
        mov esi, stream_suffix_buf
        xor edx, edx
.ssr_newsuffix:
        mov al, [esi + edx]
        mov [edi + ecx], al
        test al, al
        jz .ssr_rename
        inc ecx
        inc edx
        cmp edx, STREAM_NAME_MAX
        ja .fail
        jmp .ssr_newsuffix
.ssr_rename:
        ; hbfs_rename_entry is recursive: it runs a sweep of its own, which
        ; rewrites this function's static slots. The base names the outer
        ; sweep is still walking live on the stack across the call.
        push dword [.old]
        push dword [.new]
        mov esi, stream_name_buf
        mov edi, stream_new_buf
        call hbfs_rename_entry
        pop dword [.new]
        pop dword [.old]
        jc .fail
        jmp .ssr_again

.ssr_next:
        add esi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .ssr_scan
.ok:
        popad
        clc
        ret
.fail:
        popad
        stc
        ret
.old:   dd 0
.new:   dd 0

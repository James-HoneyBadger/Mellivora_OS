; tree.asm - Recursive directory tree listing
; Usage: tree [DIR]
; Displays directory structure with box-drawing lines

%include "syscalls.inc"

MAX_DEPTH equ 8

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        ; Save current directory
        mov eax, SYS_GETCWD
        mov ebx, save_cwd
        int 0x80

        ; If argument given, cd to it first
        cmp eax, 0
        jle .no_arg
        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .no_arg

        ; Print the directory name as root
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B            ; Light cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; cd to argument directory
        mov eax, SYS_CHDIR
        int 0x80
        cmp eax, 0
        jne .print_root
        ; chdir failed
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C            ; Light red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_not_dir
        int 0x80
        jmp .exit
.no_arg:
        ; Print CWD as root
.print_root:
        mov eax, SYS_GETCWD
        mov ebx, cwd_buf
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B            ; Light cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, cwd_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; Start recursive listing at depth 0
        mov dword [dir_count], 0
        mov dword [file_count], 0
        xor eax, eax            ; depth = 0
        call list_tree

        ; Print summary
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, [dir_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_dirs
        int 0x80
        mov eax, [file_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_files
        int 0x80

        ; Restore original directory
        mov eax, SYS_CHDIR
        mov ebx, save_cwd
        int 0x80

.exit:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; list_tree - List directory at current depth
; EAX = current depth (0-based)
; Uses READDIR to enumerate entries
;---------------------------------------
list_tree:
        push rax
        push rbp
        mov ebp, eax            ; EBP = depth

        cmp ebp, MAX_DEPTH
        jge .lt_ret

        ; First pass: count total entries to know which is last
        xor ecx, ecx           ; index
        xor edx, edx           ; count
.count_loop:
        push rcx
        push rdx
        mov eax, SYS_READDIR
        mov ebx, entry_buf
        ; ECX = index (already set)
        int 0x80
        pop rdx
        pop rcx
        cmp eax, -1            ; -1 = end of directory
        je .count_done
        cmp eax, 0             ; 0 = free/empty slot
        je .count_skip
        inc edx
.count_skip:
        inc ecx
        cmp ecx, 512           ; safety limit
        jge .count_done
        jmp .count_loop
.count_done:
        mov [total_entries], edx

        ; Second pass: print entries
        xor ecx, ecx           ; index
        mov dword [printed], 0
.print_loop:
        push rcx
        mov eax, SYS_READDIR
        mov ebx, entry_buf
        int 0x80
        pop rcx
        cmp eax, -1
        je .lt_ret
        cmp eax, 0             ; free slot
        je .print_skip

        ; EAX = file type, ECX preserved as index
        push rcx
        push rax               ; save type

        inc dword [printed]

        ; Print tree prefix
        mov edx, ebp           ; depth
        xor ecx, ecx
.prefix_loop:
        cmp ecx, edx
        jge .prefix_done
        ; Check if this level's prefix should be bar or space
        ; Simple approach: always print bar for intermediate levels
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; Dark gray
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_bar
        int 0x80
        inc ecx
        jmp .prefix_loop
.prefix_done:

        ; Determine if this is the last entry
        mov eax, [printed]
        cmp eax, [total_entries]
        je .is_last

        ; Not last: print ├──
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; Dark gray
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_tee
        int 0x80
        jmp .print_name
.is_last:
        ; Last: print └──
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_elbow
        int 0x80

.print_name:
        ; Set color based on type
        pop rax                 ; type
        push rax
        cmp eax, 3              ; FTYPE_EXEC
        je .clr_exec
        cmp eax, 2              ; FTYPE_DIR  (readdir returns dirent type)
        je .clr_dir
        cmp eax, 4              ; FTYPE_BATCH
        je .clr_batch
        ; Default: text file
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp .do_print_name
.clr_dir:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; Light cyan
        int 0x80
        jmp .do_print_name
.clr_exec:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80
        jmp .do_print_name
.clr_batch:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0D           ; Magenta
        int 0x80

.do_print_name:
        mov eax, SYS_PRINT
        mov ebx, entry_buf
        int 0x80

        pop rax                 ; type again
        push rax

        ; If directory, append /
        cmp eax, 2
        jne .no_slash
        mov eax, SYS_PUTCHAR
        mov ebx, '/'
        int 0x80
.no_slash:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        ; If directory, recurse into it
        pop rax
        pop rcx                 ; restore index
        cmp eax, 2
        jne .not_dir

        inc dword [dir_count]

        ; Save CWD, cd into subdir, recurse, cd back
        push rcx
        mov eax, SYS_CHDIR
        mov ebx, entry_buf
        int 0x80
        cmp eax, 0
        jne .recurse_fail

        mov eax, ebp
        inc eax
        call list_tree

        ; cd ..
        mov eax, SYS_CHDIR
        mov ebx, str_dotdot
        int 0x80
.recurse_fail:
        pop rcx
        jmp .print_skip

.not_dir:
        inc dword [file_count]
        jmp .print_skip

.print_skip:
        inc ecx
        cmp ecx, 512
        jge .lt_ret
        jmp .print_loop

.lt_ret:
        pop rbp
        pop rax
        ret

;---------------------------------------
; skip_spaces - Advance ESI past spaces
;---------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

;=======================================================================
; DATA
;=======================================================================

str_tee:    db 0xC3, 0xC4, 0xC4, " ", 0  ; ├──
str_elbow:  db 0xC0, 0xC4, 0xC4, " ", 0  ; └──
str_bar:    db 0xB3, "   ", 0             ; │
str_dotdot: db "..", 0
str_dirs:   db " directories, ", 0
str_files:  db " files", 0x0A, 0
err_not_dir: db "tree: not a directory", 0x0A, 0

dir_count:      dd 0
file_count:     dd 0
total_entries:  dd 0
printed:        dd 0
arg_buf:        times 256 db 0
cwd_buf:        times 256 db 0
save_cwd:       times 256 db 0
entry_buf:      times 288 db 0

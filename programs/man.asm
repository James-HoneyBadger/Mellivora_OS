; man.asm - Manual page viewer for Mellivora OS
;
; Usage:
;   man              List all available topics
;   man <topic>      Display manual page for <topic>
;   man -l           List all available topics
;   man -k <kw>      Search topic names + summaries for <kw>
;
; Pages are baked into the binary so `man` works on a fresh boot with no
; filesystem installation. If a baked topic is not found, falls back to
; reading /docs/man/<topic>.txt from disk.

%include "syscalls.inc"

PAGE_LINES equ 22

;============================================================
; Entry
;============================================================
start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .list_topics

        ; Strip trailing whitespace / newline from argbuf
        mov esi, arg_buf
.trim:
        mov al, [esi]
        test al, al
        jz .trim_done
        cmp al, 10
        je .zterm
        cmp al, 13
        je .zterm
        inc esi
        jmp .trim
.zterm:
        mov byte [esi], 0
.trim_done:

        ; -l ?
        mov esi, arg_buf
        mov edi, opt_l
        call streq
        je .list_topics

        ; -k <kw> ?
        mov esi, arg_buf
        mov edi, opt_k
        call str_starts
        je .do_keyword

        ; Otherwise: treat first whitespace-delimited token as topic
        call truncate_at_space
        jmp .lookup_topic

;------------------------------------------------------------
; -k keyword search
;------------------------------------------------------------
.do_keyword:
        ; Skip past "-k" and following spaces
        mov esi, arg_buf
        add esi, 2              ; past "-k"
.k_skip:
        mov al, [esi]
        cmp al, ' '
        je .k_skip_inc
        cmp al, 9
        je .k_skip_inc
        jmp .k_have
.k_skip_inc:
        inc esi
        jmp .k_skip
.k_have:
        test byte [esi], 0xFF
        jz .list_topics         ; -k with no keyword == list

        ; Lower-case copy of keyword into kw_buf
        mov edi, kw_buf
        push esi
.k_lower:
        lodsb
        test al, al
        jz .k_lower_done
        cmp al, 'A'
        jb .k_store
        cmp al, 'Z'
        ja .k_store
        add al, 32
.k_store:
        stosb
        jmp .k_lower
.k_lower_done:
        mov byte [edi], 0
        pop esi

        ; Walk topic table; for each entry, do case-insensitive substring
        ; match against name + summary
        mov eax, SYS_PRINT
        mov ebx, msg_search_hdr
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, kw_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_search_hdr2
        int 0x80

        mov ebx, topic_table
.k_loop:
        mov esi, [ebx]          ; name
        test esi, esi
        jz .k_done
        ; Try name match
        push ebx
        mov edi, kw_buf
        call istr_contains
        pop ebx
        je .k_match
        ; Try summary match
        push ebx
        mov esi, [ebx + 8]      ; summary
        mov edi, kw_buf
        call istr_contains
        pop ebx
        je .k_match
        add ebx, 12
        jmp .k_loop
.k_match:
        push ebx
        call print_topic_row
        pop ebx
        add ebx, 12
        jmp .k_loop
.k_done:
        xor ebx, ebx
        jmp .exit

;------------------------------------------------------------
; List all topics
;------------------------------------------------------------
.list_topics:
        mov eax, SYS_PRINT
        mov ebx, msg_list_hdr
        int 0x80
        mov ebx, topic_table
.lt_loop:
        mov esi, [ebx]
        test esi, esi
        jz .lt_done
        push ebx
        call print_topic_row
        pop ebx
        add ebx, 12
        jmp .lt_loop
.lt_done:
        mov eax, SYS_PRINT
        mov ebx, msg_list_foot
        int 0x80
        xor ebx, ebx
        jmp .exit

;------------------------------------------------------------
; Look up baked topic; fall back to disk
;------------------------------------------------------------
.lookup_topic:
        mov ebx, topic_table
.find:
        mov esi, [ebx]          ; name in table
        test esi, esi
        jz .try_disk
        mov edi, arg_buf
        call streq_ci
        je .found
        add ebx, 12
        jmp .find

.found:
        mov esi, [ebx + 4]      ; body pointer
        ; Copy into file_buf so the pager can null-terminate it
        mov edi, file_buf
.copy:
        lodsb
        stosb
        test al, al
        jnz .copy
        jmp .display

;------------------------------------------------------------
; Disk fallback: /docs/man/<topic>.txt
;------------------------------------------------------------
.try_disk:
        mov esi, path_prefix
        mov edi, path_buf
.cp_pfx:
        lodsb
        test al, al
        jz .cp_pfx_done
        stosb
        jmp .cp_pfx
.cp_pfx_done:
        mov esi, arg_buf
.cp_topic:
        lodsb
        test al, al
        jz .cp_topic_done
        stosb
        jmp .cp_topic
.cp_topic_done:
        mov dword [edi], '.txt'
        mov byte [edi + 4], 0

        mov eax, SYS_FREAD
        mov ebx, path_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .not_found
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0
        jmp .display

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_no_entry
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, arg_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_try_list
        int 0x80
        mov ebx, 1
        jmp .exit

;------------------------------------------------------------
; Pager - prints file_buf with PAGE_LINES per screen
;------------------------------------------------------------
.display:
        mov esi, file_buf
        xor ecx, ecx
.page_loop:
        mov al, [esi]
        test al, al
        jz .eof
        cmp al, 0x0A
        je .newline

        push ecx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ecx
        inc esi
        jmp .page_loop

.newline:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop ecx
        inc esi
        inc ecx
        cmp ecx, PAGE_LINES
        jl .page_loop

        ; Pager prompt
        push esi
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_more
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        pop esi

        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0D
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_blank
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0D
        int 0x80
        pop eax

        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, 27
        je .quit
        cmp al, ' '
        je .next_page
        ; Anything else: scroll one line
        mov ecx, PAGE_LINES - 1
        jmp .page_loop
.next_page:
        xor ecx, ecx
        jmp .page_loop

.eof:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        xor ebx, ebx
.quit:
        xor ebx, ebx
.exit:
        mov eax, SYS_EXIT
        int 0x80

;============================================================
; Helpers
;============================================================

; truncate arg_buf at first whitespace
truncate_at_space:
        mov esi, arg_buf
.l:
        mov al, [esi]
        test al, al
        jz .d
        cmp al, ' '
        je .cut
        cmp al, 9
        je .cut
        inc esi
        jmp .l
.cut:
        mov byte [esi], 0
.d:
        ret

; print_topic_row - prints "  NAME    SUMMARY\n" given EBX = entry
;   entry layout: dd name, dd body, dd summary
print_topic_row:
        mov eax, SYS_PRINT
        mov ebx, [esp + 4]
        mov ebx, [ebx]
        push ebx
        mov eax, SYS_PRINT
        mov ebx, row_indent
        int 0x80
        pop ebx
        mov eax, SYS_PRINT
        int 0x80
        ; Pad to column 14
        mov esi, [esp + 4]
        mov esi, [esi]
        xor ecx, ecx
.pl:
        cmp byte [esi + ecx], 0
        je .pad
        inc ecx
        jmp .pl
.pad:
        mov edx, 14
        sub edx, ecx
        jle .summary
.psp:
        push edx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop edx
        dec edx
        jnz .psp
.summary:
        mov ebx, [esp + 4]
        mov ebx, [ebx + 8]      ; summary
        test ebx, ebx
        jz .nl
        mov eax, SYS_PRINT
        int 0x80
.nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        ret

; streq - ESI vs EDI null-terminated, ZF set if equal
streq:
        push esi
        push edi
.l:
        mov al, [esi]
        mov ah, [edi]
        cmp al, ah
        jne .ne
        test al, al
        jz .eq
        inc esi
        inc edi
        jmp .l
.eq:
        pop edi
        pop esi
        xor al, al
        ret
.ne:
        pop edi
        pop esi
        or al, 1
        cmp al, 0
        ret

; streq_ci - case-insensitive equality
streq_ci:
        push esi
        push edi
.l:
        mov al, [esi]
        mov ah, [edi]
        call to_lower_al
        xchg al, ah
        call to_lower_al
        xchg al, ah
        cmp al, ah
        jne .ne
        test al, al
        jz .eq
        inc esi
        inc edi
        jmp .l
.eq:
        pop edi
        pop esi
        xor al, al
        ret
.ne:
        pop edi
        pop esi
        or al, 1
        cmp al, 0
        ret

to_lower_al:
        cmp al, 'A'
        jb .x
        cmp al, 'Z'
        ja .x
        add al, 32
.x:
        ret

; str_starts - does ESI start with EDI? ZF set if yes
str_starts:
        push esi
        push edi
.l:
        mov al, [edi]
        test al, al
        jz .eq
        mov ah, [esi]
        cmp al, ah
        jne .ne
        inc esi
        inc edi
        jmp .l
.eq:
        pop edi
        pop esi
        xor al, al
        ret
.ne:
        pop edi
        pop esi
        or al, 1
        cmp al, 0
        ret

; istr_contains - does ESI (haystack) contain EDI (needle, lowercase)?
;   ZF set if yes. Case-insensitive on haystack only (needle assumed lower).
istr_contains:
        push esi
        push edi
        push ebx
.outer:
        mov al, [esi]
        test al, al
        jz .ne
        push esi
        push edi
.inner:
        mov ah, [edi]
        test ah, ah
        jz .hit
        mov al, [esi]
        test al, al
        jz .miss_pop
        cmp al, 'A'
        jb .ck
        cmp al, 'Z'
        ja .ck
        add al, 32
.ck:
        cmp al, ah
        jne .miss_pop
        inc esi
        inc edi
        jmp .inner
.miss_pop:
        pop edi
        pop esi
        inc esi
        jmp .outer
.hit:
        pop edi
        pop esi
        pop ebx
        pop edi
        pop esi
        xor al, al
        ret
.ne:
        pop ebx
        pop edi
        pop esi
        or al, 1
        cmp al, 0
        ret

;============================================================
; Strings
;============================================================
opt_l:          db "-l", 0
opt_k:          db "-k", 0
path_prefix:    db "/docs/man/", 0
row_indent:     db "  ", 0
msg_no_entry:   db "No manual entry for ", 0
msg_try_list:   db 0x0A, "(try 'man' for a list of available topics)", 0x0A, 0
msg_more:       db " -- MANUAL --  Space=page  Enter=line  q=quit ", 0
msg_blank:      db "                                                    ", 0
msg_list_hdr:
        db "Mellivora Manual - Available Topics", 0x0A
        db "===================================", 0x0A, 0
msg_list_foot:
        db 0x0A, "Use 'man <topic>' to read a page,", 0x0A
        db "or 'man -k <keyword>' to search.", 0x0A, 0
msg_search_hdr:
        db "Topics matching '", 0
msg_search_hdr2:
        db "':", 0x0A, 0

;============================================================
; Topic table  (name, body, summary)
;============================================================
%macro TOPIC 3
        dd %1, %2, %3
%endmacro

topic_table:
        TOPIC n_intro,   p_intro,   s_intro
        TOPIC n_man,     p_man,     s_man
        TOPIC n_shell,   p_shell,   s_shell
        TOPIC n_files,   p_files,   s_files
        TOPIC n_ls,      p_ls,      s_ls
        TOPIC n_cd,      p_cd,      s_cd
        TOPIC n_cp,      p_cp,      s_cp
        TOPIC n_mv,      p_mv,      s_mv
        TOPIC n_rm,      p_rm,      s_rm
        TOPIC n_cat,     p_cat,     s_cat
        TOPIC n_echo,    p_echo,    s_echo
        TOPIC n_grep,    p_grep,    s_grep
        TOPIC n_find,    p_find,    s_find
        TOPIC n_edit,    p_edit,    s_edit
        TOPIC n_bedit,   p_bedit,   s_bedit
        TOPIC n_clip,    p_clip,    s_clip
        TOPIC n_bdialog, p_bdialog, s_bdialog
        TOPIC n_asm,     p_asm,     s_asm
        TOPIC n_basic,   p_basic,   s_basic
        TOPIC n_dis,     p_dis,     s_dis
        TOPIC n_debug,   p_debug,   s_debug
        TOPIC n_ps,      p_ps,      s_ps
        TOPIC n_kill,    p_kill,    s_kill
        TOPIC n_chmod,   p_chmod,   s_chmod
        TOPIC n_df,      p_df,      s_df
        TOPIC n_du,      p_du,      s_du
        TOPIC n_net,     p_net,     s_net
        TOPIC n_wget,    p_wget,    s_wget
        TOPIC n_ping,    p_ping,    s_ping
        TOPIC n_burrows, p_burrows, s_burrows
        TOPIC n_syscalls,p_syscalls,s_syscalls
        TOPIC n_hbfs,    p_hbfs,    s_hbfs
        TOPIC n_keys,    p_keys,    s_keys
        TOPIC n_credits, p_credits, s_credits
        TOPIC n_meminfo, p_meminfo, s_meminfo
        TOPIC n_tag,     p_tag,     s_tag
        TOPIC n_histgrep,p_histgrep,s_histgrep
        TOPIC n_bnotify, p_bnotify, s_bnotify
        TOPIC n_bcal,    p_bcal,    s_bcal
        TOPIC n_plasma,  p_plasma,  s_plasma
        TOPIC n_nim,     p_nim,     s_nim
        TOPIC n_mkprog,  p_mkprog,  s_mkprog
        TOPIC n_dnslook, p_dnslook, s_dnslook
        TOPIC n_play,    p_play,    s_play
        TOPIC n_journal, p_journal, s_journal
        TOPIC n_theme,   p_theme,   s_theme
        TOPIC n_tutorial,p_tutorial,s_tutorial
        TOPIC n_pkginfo, p_pkginfo, s_pkginfo
        TOPIC n_more,    p_more,    s_more
        TOPIC n_sed,     p_sed,     s_sed
        TOPIC n_awk,     p_awk,     s_awk
        TOPIC n_top,     p_top,     s_top
        TOPIC n_tar,     p_tar,     s_tar
        TOPIC n_nm,      p_nm,      s_nm
        TOPIC n_pciinfo, p_pciinfo, s_pciinfo
        TOPIC n_audiodemo,p_audiodemo,s_audiodemo
        TOPIC n_blitdemo,p_blitdemo,s_blitdemo
        TOPIC n_gfxdemo, p_gfxdemo, s_gfxdemo
        TOPIC n_gfxplasma,p_gfxplasma,s_gfxplasma
        TOPIC n_soundviz,p_soundviz,s_soundviz
        TOPIC n_breakout,p_breakout,s_breakout
        TOPIC n_cube,    p_cube,    s_cube
        TOPIC n_julia,   p_julia,   s_julia
        TOPIC n_mandelbrot,p_mandelbrot,s_mandelbrot
        dd 0, 0, 0              ; sentinel

;------------------------------------------------------------
; Topic names
;------------------------------------------------------------
n_intro:    db "intro", 0
n_man:      db "man", 0
n_shell:    db "shell", 0
n_files:    db "files", 0
n_ls:       db "ls", 0
n_cd:       db "cd", 0
n_cp:       db "cp", 0
n_mv:       db "mv", 0
n_rm:       db "rm", 0
n_cat:      db "cat", 0
n_echo:     db "echo", 0
n_grep:     db "grep", 0
n_find:     db "find", 0
n_edit:     db "edit", 0
n_bedit:    db "bedit", 0
n_clip:     db "clip", 0
n_bdialog:  db "bdialog", 0
n_asm:      db "asm", 0
n_basic:    db "basic", 0
n_dis:      db "dis", 0
n_debug:    db "debug", 0
n_ps:       db "ps", 0
n_kill:     db "kill", 0
n_chmod:    db "chmod", 0
n_df:       db "df", 0
n_du:       db "du", 0
n_net:      db "net", 0
n_wget:     db "wget", 0
n_ping:     db "ping", 0
n_burrows:  db "burrows", 0
n_syscalls: db "syscalls", 0
n_hbfs:     db "hbfs", 0
n_keys:     db "keys", 0
n_credits:  db "credits", 0
n_meminfo:  db "meminfo", 0
n_tag:      db "tag", 0
n_histgrep: db "histgrep", 0
n_bnotify:  db "bnotify", 0
n_bcal:     db "bcal", 0
n_plasma:   db "plasma", 0
n_nim:      db "nim", 0
n_mkprog:   db "mkprog", 0
n_dnslook:  db "dnslook", 0
n_play:     db "play", 0
n_journal:  db "journal", 0
n_theme:    db "theme", 0
n_tutorial: db "tutorial", 0
n_pkginfo:  db "pkginfo", 0
n_more:     db "more", 0
n_sed:      db "sed", 0
n_awk:      db "awk", 0
n_top:      db "top", 0
n_tar:      db "tar", 0
n_nm:       db "nm", 0
n_pciinfo:  db "pciinfo", 0
n_audiodemo: db "audiodemo", 0
n_blitdemo: db "blitdemo", 0
n_gfxdemo:  db "gfxdemo", 0
n_gfxplasma: db "gfxplasma", 0
n_soundviz: db "soundviz", 0
n_breakout: db "breakout", 0
n_cube:     db "cube", 0
n_julia:    db "julia", 0
n_mandelbrot: db "mandelbrot", 0

;------------------------------------------------------------
; Summaries (one-line, shown by `man` and `man -k`)
;------------------------------------------------------------
s_intro:    db "Welcome to Mellivora OS - start here", 0
s_man:      db "Read manual pages (this command)", 0
s_shell:    db "Command shell, pipes, redirects, history", 0
s_files:    db "Filesystem layout and conventions", 0
s_ls:       db "List directory contents", 0
s_cd:       db "Change current directory", 0
s_cp:       db "Copy files", 0
s_mv:       db "Move or rename files", 0
s_rm:       db "Remove files", 0
s_cat:      db "Print files to stdout", 0
s_echo:     db "Print arguments", 0
s_grep:     db "Search files for a pattern", 0
s_find:     db "Locate files by name", 0
s_edit:     db "Modal text editor", 0
s_bedit:    db "Burrows graphical text editor", 0
s_clip:     db "Read/write the system clipboard", 0
s_bdialog:  db "Scriptable dialog boxes for shell scripts", 0
s_asm:      db "NASM-compatible assembler", 0
s_basic:    db "BASIC interpreter (basic) and compiler (basicc)", 0
s_dis:      db "Disassembler for .bin programs", 0
s_debug:    db "Interactive debugger", 0
s_ps:       db "List running processes", 0
s_kill:     db "Send a signal to a process", 0
s_chmod:    db "Change file permissions", 0
s_df:       db "Report filesystem disk usage", 0
s_du:       db "Estimate file space usage", 0
s_net:      db "Networking overview (drivers, sockets)", 0
s_wget:     db "Download files over HTTP", 0
s_ping:     db "Send ICMP echo requests", 0
s_burrows:  db "The Burrows graphical desktop", 0
s_syscalls: db "Mellivora system call ABI reference", 0
s_hbfs:     db "Honey Badger File System internals", 0
s_keys:     db "Global keyboard shortcuts", 0
s_credits:  db "Credits and license", 0
s_meminfo:  db "Show memory usage, uptime and PID", 0
s_tag:      db "Tag files with keywords and search by tag", 0
s_histgrep: db "Search shell command history", 0
s_bnotify:  db "Post a Burrows desktop notification", 0
s_bcal:     db "Calendar with personal events", 0
s_plasma:   db "Animated text-mode plasma demo", 0
s_nim:      db "Single-pile Nim game (vs perfect AI)", 0
s_mkprog:   db "Generate a new assembly program skeleton", 0
s_dnslook:  db "Resolve a hostname to an IPv4 address", 0
s_play:     db "Play a sequence of notes on the PC speaker", 0
s_journal:  db "Append timestamped entries to a personal journal", 0
s_theme:    db "Switch the system color theme", 0
s_tutorial: db "Interactive welcome tutorial for new users", 0
s_pkginfo:  db "Print Mellivora system identification info", 0
s_more:     db "Page through text files or piped input", 0
s_sed:      db "Stream editor for filtering and transforming text", 0
s_awk:      db "Pattern-action text processor", 0
s_top:      db "Real-time process and memory monitor", 0
s_tar:      db "Create, extract and list HBTAR archives", 0
s_nm:       db "List symbol table entries from ELF32 binaries", 0
s_pciinfo:  db "Display detected PCI hardware devices", 0
s_audiodemo: db "Demonstrate audio output capabilities", 0
s_blitdemo: db "Sprite blit animation demo (VBE)", 0
s_gfxdemo:  db "VBE graphics primitives demo", 0
s_gfxplasma: db "Animated plasma effect on VBE framebuffer", 0
s_soundviz: db "SB16 audio waveform visualizer (VBE)", 0
s_breakout: db "Breakout/Arkanoid clone (VBE, arrow keys)", 0
s_cube:     db "Rotating 3-D wireframe cube (VBE)", 0
s_julia:    db "Interactive Julia set renderer (VBE)", 0
s_mandelbrot: db "Mandelbrot set renderer (VBE fixed-point)", 0

;------------------------------------------------------------
; Page bodies
;------------------------------------------------------------
p_intro:
        db "INTRO(7)            Mellivora OS Manual            INTRO(7)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    intro - a quick tour of Mellivora OS", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Mellivora is a single-user, multitasking, x86 protected-mode", 0x0A
        db "    operating system written entirely in NASM assembly. It boots", 0x0A
        db "    in well under a second and runs comfortably on a 486 with 8MB.", 0x0A, 0x0A
        db "    Type commands at the '$' prompt. Try:", 0x0A
        db "        ls /bin                     list installed programs", 0x0A
        db "        man shell                   read about the shell", 0x0A
        db "        man -l                      list every manual topic", 0x0A
        db "        man -k network              search topics by keyword", 0x0A
        db "        burrows                     launch the graphical desktop", 0x0A, 0x0A
        db "    Press F1 from anywhere in Burrows for context help.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    shell, files, burrows, syscalls, keys", 0x0A, 0

p_man:
        db "MAN(1)              Mellivora OS Manual              MAN(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    man - display reference manual pages", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    man                       list all topics", 0x0A
        db "    man <topic>               display page for <topic>", 0x0A
        db "    man -l                    list all topics", 0x0A
        db "    man -k <keyword>          search topics for <keyword>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    The 'man' command shows reference documentation. Pages are", 0x0A
        db "    baked into the binary so 'man' works on a freshly booted", 0x0A
        db "    system with no installation. If a baked topic is not found,", 0x0A
        db "    man falls back to /docs/man/<topic>.txt on disk - drop your", 0x0A
        db "    own pages there to extend the system.", 0x0A, 0x0A
        db "PAGER KEYS", 0x0A
        db "    Space     advance one page", 0x0A
        db "    Enter     advance one line", 0x0A
        db "    q / Esc   quit immediately", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    man shell", 0x0A
        db "    man -k clipboard", 0x0A, 0

p_shell:
        db "SHELL(1)            Mellivora OS Manual            SHELL(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    shell - the Mellivora command interpreter", 0x0A, 0x0A
        db "PROMPT", 0x0A
        db "    /current/dir $", 0x0A, 0x0A
        db "BUILT-INS", 0x0A
        db "    cd <dir>          change directory", 0x0A
        db "    pwd               print working directory", 0x0A
        db "    set NAME=value    define an environment variable", 0x0A
        db "    unset NAME        remove an environment variable", 0x0A
        db "    export NAME       mark variable for child processes", 0x0A
        db "    history           show command history", 0x0A
        db "    alias name=cmd    create a command alias", 0x0A
        db "    exit              leave the shell", 0x0A, 0x0A
        db "EXTERNAL COMMANDS", 0x0A
        db "    The shell searches /bin for executables. Programs are", 0x0A
        db "    flat .bin files loaded at a fixed virtual address.", 0x0A, 0x0A
        db "REDIRECTION & PIPES", 0x0A
        db "    cmd > file        redirect stdout to file (truncate)", 0x0A
        db "    cmd >> file       append stdout to file", 0x0A
        db "    cmd < file        read stdin from file", 0x0A
        db "    a | b             pipe stdout of a into stdin of b", 0x0A, 0x0A
        db "EDITING", 0x0A
        db "    Up / Down         walk through command history", 0x0A
        db "    Ctrl-A / Ctrl-E   beginning / end of line", 0x0A
        db "    Tab               complete file or command name", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    files, edit, bedit, bdialog", 0x0A, 0

p_files:
        db "FILES(7)            Mellivora OS Manual           FILES(7)", 0x0A, 0x0A
        db "FILESYSTEM LAYOUT", 0x0A
        db "    /bin              system commands (your $PATH)", 0x0A
        db "    /Burrows          graphical applications", 0x0A
        db "    /games            games and demos", 0x0A
        db "    /docs             documentation, including man/", 0x0A
        db "    /samples          example source files (BASIC, C, Perl, asm)", 0x0A
        db "    /home             your personal files", 0x0A
        db "    /tmp              transient scratch space, cleared at boot", 0x0A, 0x0A
        db "PATHS", 0x0A
        db "    Absolute paths begin with '/'. Relative paths are resolved", 0x0A
        db "    against the current working directory. '.' and '..' refer", 0x0A
        db "    to the current and parent directory respectively.", 0x0A, 0x0A
        db "FILE NAMES", 0x0A
        db "    Up to 31 characters, case-sensitive, ASCII printable.", 0x0A
        db "    Common extensions: .asm .bin .lst .txt .bas .c .pl .doc", 0x0A, 0x0A
        db "PERMISSIONS", 0x0A
        db "    Three bits per file: read, write, execute. See chmod(1).", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    hbfs, ls, cp, mv, rm, chmod, df, du", 0x0A, 0

p_ls:
        db "LS(1)               Mellivora OS Manual               LS(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    ls - list directory contents", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    ls [-l] [-a] [path]", 0x0A, 0x0A
        db "OPTIONS", 0x0A
        db "    -l    long format: size, perms, type", 0x0A
        db "    -a    show hidden entries (names starting with '.')", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    ls               list current directory", 0x0A
        db "    ls -l /bin       long listing of /bin", 0x0A
        db "    ls /Burrows      list graphical apps", 0x0A, 0

p_cd:
        db "CD(1)               Mellivora OS Manual               CD(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    cd - change the current working directory", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    cd            go to /home", 0x0A
        db "    cd <path>     change directory", 0x0A
        db "    cd -          return to previous directory", 0x0A, 0x0A
        db "NOTES", 0x0A
        db "    'cd' is a shell built-in. The new directory is updated in", 0x0A
        db "    the prompt. '..' walks up one level.", 0x0A, 0

p_cp:
        db "CP(1)               Mellivora OS Manual               CP(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    cp - copy files", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    cp <source> <dest>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Copy <source> to <dest>. If <dest> exists it is overwritten.", 0x0A
        db "    Both arguments must currently be regular files (no recursive", 0x0A
        db "    directory copy yet).", 0x0A, 0

p_mv:
        db "MV(1)               Mellivora OS Manual               MV(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    mv - move or rename files", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    mv <source> <dest>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Renames <source> to <dest> in place when both reside on the", 0x0A
        db "    same filesystem. Otherwise copies and then removes.", 0x0A, 0

p_rm:
        db "RM(1)               Mellivora OS Manual               RM(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    rm - remove files", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    rm <file> [<file>...]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Deletes each named file. There is no recycle bin - the", 0x0A
        db "    blocks are returned to the free list immediately.", 0x0A, 0

p_cat:
        db "CAT(1)              Mellivora OS Manual              CAT(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    cat - concatenate files to stdout", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    cat <file> [<file>...]", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    cat /docs/readme.txt", 0x0A
        db "    cat a.txt b.txt > combined.txt", 0x0A, 0

p_echo:
        db "ECHO(1)             Mellivora OS Manual             ECHO(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    echo - print arguments separated by spaces", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    echo [args...]", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    echo hello, world", 0x0A
        db "    echo $PATH", 0x0A
        db "    echo done > status.txt", 0x0A, 0

p_grep:
        db "GREP(1)             Mellivora OS Manual             GREP(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    grep - search files for lines matching a pattern", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    grep [-i] [-n] <pattern> <file> [<file>...]", 0x0A, 0x0A
        db "OPTIONS", 0x0A
        db "    -i    case-insensitive match", 0x0A
        db "    -n    prefix matches with line numbers", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    grep TODO /docs/notes.txt", 0x0A
        db "    grep -in error log.txt", 0x0A, 0

p_find:
        db "FIND(1)             Mellivora OS Manual             FIND(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    find - locate files by name", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    find <path> [-name <pattern>]", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    find /                       list every file", 0x0A
        db "    find /bin -name 'b*'         all programs starting with b", 0x0A, 0

p_edit:
        db "EDIT(1)             Mellivora OS Manual             EDIT(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    edit - the modal text editor", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    edit [file]", 0x0A, 0x0A
        db "MODES", 0x0A
        db "    NORMAL  - cursor movement and commands", 0x0A
        db "    INSERT  - typing inserts text", 0x0A
        db "    COMMAND - ':' commands like :w :q :wq", 0x0A, 0x0A
        db "KEYS", 0x0A
        db "    i           enter INSERT mode", 0x0A
        db "    Esc         return to NORMAL mode", 0x0A
        db "    h j k l     move cursor", 0x0A
        db "    dd          delete current line", 0x0A
        db "    yy          yank current line", 0x0A
        db "    p           paste yanked line", 0x0A
        db "    /pattern    search forward", 0x0A
        db "    :w          save", 0x0A
        db "    :q          quit (use :q! to discard changes)", 0x0A, 0

p_bedit:
        db "BEDIT(1)            Mellivora OS Manual            BEDIT(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    bedit - Burrows graphical text editor", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    bedit [file]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Mouse-driven editor in the Burrows desktop. Has menus,", 0x0A
        db "    syntax-aware coloring for .asm and .bas, and integrates", 0x0A
        db "    with the system clipboard (see clip).", 0x0A, 0x0A
        db "SHORTCUTS", 0x0A
        db "    Ctrl-S      save              Ctrl-O    open", 0x0A
        db "    Ctrl-X      cut               Ctrl-C    copy", 0x0A
        db "    Ctrl-V      paste             Ctrl-Z    undo", 0x0A
        db "    Ctrl-F      find              Ctrl-G    go-to line", 0x0A, 0

p_clip:
        db "CLIP(1)             Mellivora OS Manual             CLIP(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    clip - read or write the system clipboard", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    clip copy <text>          copy text to clipboard", 0x0A
        db "    clip paste                print clipboard to stdout", 0x0A
        db "    cmd | clip                copy stdin to clipboard", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    ls /bin | clip", 0x0A
        db "    clip paste > saved.txt", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    bedit, bnotes, syscalls", 0x0A, 0

p_bdialog:
        db "BDIALOG(1)          Mellivora OS Manual          BDIALOG(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    bdialog - scriptable dialog boxes", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    bdialog msg <text>             show, wait for any key", 0x0A
        db "    bdialog yesno <q>              prompt y/n; exit 0=yes 1=no", 0x0A
        db "    bdialog input <prompt>         read a line, echo to stdout", 0x0A
        db "    bdialog notify <text>          fire a system notification", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    if bdialog yesno 'Format disk?'; then echo formatting; fi", 0x0A
        db "    name=$(bdialog input 'Your name:')", 0x0A, 0

p_asm:
        db "ASM(1)              Mellivora OS Manual              ASM(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    asm - NASM-compatible assembler", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    asm [-o out.bin] source.asm", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Assembles 32-bit x86 source into a flat .bin file ready to", 0x0A
        db "    load via the shell. Supports a useful subset of NASM:", 0x0A
        db "    labels, EQU, DB/DW/DD, TIMES, %include, %macro.", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    asm hello.asm                produces hello.bin", 0x0A
        db "    asm -o /bin/hello hello.asm  installs into /bin", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    dis, debug, syscalls", 0x0A, 0

p_basic:
        db "BASIC(1)            Mellivora OS Manual            BASIC(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    basic  - interactive BASIC interpreter", 0x0A
        db "    basicc - BASIC ahead-of-time compiler", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    basic [program.bas]", 0x0A
        db "    basicc <program.bas> [-o out.bin]", 0x0A, 0x0A
        db "LANGUAGE", 0x0A
        db "    Line numbers optional. Supports PRINT, INPUT, LET, IF/THEN,", 0x0A
        db "    FOR/NEXT, GOSUB/RETURN, DIM, PEEK/POKE, INT/STR/CHR/ASC,", 0x0A
        db "    string concatenation with '+', and integer math.", 0x0A, 0x0A
        db "EXAMPLE", 0x0A
        db '    10 PRINT "HI"', 0x0A
        db "    20 FOR I=1 TO 5", 0x0A
        db "    30 PRINT I", 0x0A
        db "    40 NEXT I", 0x0A, 0

p_dis:
        db "DIS(1)              Mellivora OS Manual              DIS(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    dis - disassemble a flat binary", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    dis <file.bin> [start] [count]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Prints x86 mnemonics for the instructions in <file.bin>.", 0x0A
        db "    Useful for inspecting the output of 'asm' or unfamiliar", 0x0A
        db "    binaries. Combine with 'debug' to step through.", 0x0A, 0

p_debug:
        db "DEBUG(1)            Mellivora OS Manual            DEBUG(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    debug - interactive debugger", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    debug <file.bin>", 0x0A, 0x0A
        db "COMMANDS", 0x0A
        db "    r           run until breakpoint or exit", 0x0A
        db "    s           single step", 0x0A
        db "    b <addr>    set breakpoint at hex address", 0x0A
        db "    p           print registers", 0x0A
        db "    d <addr>    dump memory", 0x0A
        db "    q           quit", 0x0A, 0

p_ps:
        db "PS(1)               Mellivora OS Manual               PS(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    ps - report a snapshot of running processes", 0x0A, 0x0A
        db "OUTPUT COLUMNS", 0x0A
        db "    PID    process id", 0x0A
        db "    STAT   R=running S=sleeping Z=zombie", 0x0A
        db "    PRI    scheduler priority (lower = sooner)", 0x0A
        db "    TIME   accumulated CPU ticks", 0x0A
        db "    CMD    command name", 0x0A, 0

p_kill:
        db "KILL(1)             Mellivora OS Manual             KILL(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    kill - send a signal to a process", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    kill [-sig] <pid>", 0x0A, 0x0A
        db "SIGNALS", 0x0A
        db "    -1 HUP    -2 INT    -9 KILL    -15 TERM (default)", 0x0A, 0

p_chmod:
        db "CHMOD(1)            Mellivora OS Manual            CHMOD(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    chmod - change file permissions", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    chmod <mode> <file>", 0x0A, 0x0A
        db "MODE", 0x0A
        db "    Three octal digits: read=4 write=2 execute=1.", 0x0A
        db "    Examples: 755 = rwx r-x r-x   600 = rw- --- ---", 0x0A, 0

p_df:
        db "DF(1)               Mellivora OS Manual               DF(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    df - report filesystem disk usage", 0x0A, 0x0A
        db "OUTPUT", 0x0A
        db "    Shows total, used, and free blocks for the HBFS volume.", 0x0A
        db "    Block size is 512 bytes.", 0x0A, 0

p_du:
        db "DU(1)               Mellivora OS Manual               DU(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    du - estimate file space usage", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    du [path]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Walks the named directory recursively and prints the size", 0x0A
        db "    of each entry, followed by a total.", 0x0A, 0

p_net:
        db "NET(7)              Mellivora OS Manual              NET(7)", 0x0A, 0x0A
        db "NETWORKING OVERVIEW", 0x0A
        db "    Mellivora ships drivers for ne2000 and rtl8139 adapters.", 0x0A
        db "    The TCP/IP stack supports IPv4, ICMP, UDP and TCP, with a", 0x0A
        db "    Berkeley-style sockets API exposed via the syscall layer.", 0x0A, 0x0A
        db "TOOLS", 0x0A
        db "    ifconfig, route, ping, dig, wget, chat, daytime, dmesg", 0x0A, 0x0A
        db "CONFIGURATION", 0x0A
        db "    DHCP runs at boot. Static config in /etc/network.cfg.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    wget, ping, syscalls", 0x0A, 0

p_wget:
        db "WGET(1)             Mellivora OS Manual             WGET(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    wget - retrieve files over HTTP", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    wget [-O outfile] <url>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Connects to the host in <url>, issues an HTTP/1.0 GET, and", 0x0A
        db "    writes the body to outfile (or to a name derived from the", 0x0A
        db "    URL path).", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    wget http://example.com/", 0x0A
        db "    wget -O index.html http://example.com/", 0x0A, 0

p_ping:
        db "PING(1)             Mellivora OS Manual             PING(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    ping - send ICMP ECHO_REQUEST packets", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    ping [-c count] <host>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Sends ICMP echo packets to <host> and reports round-trip", 0x0A
        db "    times. Default count is 4. Press Ctrl-C to stop early.", 0x0A, 0

p_burrows:
        db "BURROWS(1)          Mellivora OS Manual          BURROWS(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    burrows - the Mellivora graphical desktop", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Burrows is a window manager and application suite that runs", 0x0A
        db "    on top of VBE-detected graphics modes (typically 800x600 or", 0x0A
        db "    1024x768). Apps live in /Burrows and follow the b* naming", 0x0A
        db "    convention (bedit, bsheet, bnotes, bpaint, bplayer ...).", 0x0A, 0x0A
        db "GLOBAL KEYS", 0x0A
        db "    F1            help on the focused window", 0x0A
        db "    F2            launcher menu", 0x0A
        db "    Alt-Tab       switch windows", 0x0A
        db "    Alt-F4        close window", 0x0A
        db "    Win+L         lock screen / start screensaver", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    bedit, bsheet, bnotes, bpaint, bsysmon, bsettings", 0x0A, 0

p_syscalls:
        db "SYSCALLS(2)         Mellivora OS Manual         SYSCALLS(2)", 0x0A, 0x0A
        db "INVOCATION", 0x0A
        db "    INT 0x80   EAX = syscall number", 0x0A
        db "               args in EBX, ECX, EDX, ESI, EDI", 0x0A
        db "               return value in EAX (negative = error)", 0x0A, 0x0A
        db "SELECTED CALLS  (see programs/syscalls.inc for the full list)", 0x0A
        db "    1  PUTCHAR        EBX=ch", 0x0A
        db "    2  GETCHAR        -> EAX=ch", 0x0A
        db "    3  PRINT          EBX=ptr to NUL-terminated string", 0x0A
        db "   11  STAT           EBX=path ECX=statbuf", 0x0A
        db "   14  SETCURSOR      EBX=row ECX=col", 0x0A
        db "   17  CLEAR          (clear screen)", 0x0A
        db "   18  SETCOLOR       EBX=attr byte", 0x0A
        db "   30  FREAD          EBX=path ECX=buf -> EAX=bytes", 0x0A
        db "   32  GETARGS        EBX=buf -> EAX=len", 0x0A
        db "   34  STDIN_READ     EBX=buf -> EAX=bytes (pipe-aware)", 0x0A
        db "   55  CLIPBOARD_COPY  EBX=buf ECX=len", 0x0A
        db "   56  CLIPBOARD_PASTE EBX=buf ECX=max -> EAX=len", 0x0A
        db "   57  NOTIFY         EBX=msg EDX=color", 0x0A
        db "   60  EXIT           EBX=status", 0x0A, 0

p_hbfs:
        db "HBFS(5)             Mellivora OS Manual             HBFS(5)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    HBFS - Honey Badger File System", 0x0A, 0x0A
        db "ON-DISK LAYOUT", 0x0A
        db "    block 0      superblock + magic 'HBFS'", 0x0A
        db "    block 1..N   bitmap of free blocks", 0x0A
        db "    block N+1..  inode table (256-byte inodes)", 0x0A
        db "    rest         data blocks (512 bytes each)", 0x0A, 0x0A
        db "INODE", 0x0A
        db "    type, perms, owner, size, mtime, 12 direct + 1 indirect ptr", 0x0A, 0x0A
        db "LIMITS", 0x0A
        db "    Max file size:   ~ 6 MiB (with current indirect scheme)", 0x0A
        db "    Max name length: 31 chars", 0x0A
        db "    Max files:       limited by inode table size", 0x0A, 0

p_keys:
        db "KEYS(7)             Mellivora OS Manual             KEYS(7)", 0x0A, 0x0A
        db "GLOBAL", 0x0A
        db "    Ctrl-Alt-Del   reboot", 0x0A
        db "    Ctrl-Alt-F1    switch to text console 1", 0x0A
        db "    Ctrl-Alt-F2    switch to text console 2", 0x0A
        db "    Ctrl-Alt-G     toggle Burrows / shell", 0x0A, 0x0A
        db "SHELL", 0x0A
        db "    Up / Down      command history", 0x0A
        db "    Tab            completion", 0x0A
        db "    Ctrl-A / -E    line start / end", 0x0A
        db "    Ctrl-C         interrupt foreground job", 0x0A
        db "    Ctrl-D         end of input / exit shell", 0x0A
        db "    Ctrl-L         clear screen", 0x0A, 0x0A
        db "BURROWS", 0x0A
        db "    F1             contextual help", 0x0A
        db "    F2             launcher", 0x0A
        db "    Alt-Tab        cycle windows", 0x0A
        db "    Alt-F4         close window", 0x0A
        db "    Win+L          lock", 0x0A, 0

p_credits:
        db "CREDITS(7)          Mellivora OS Manual          CREDITS(7)", 0x0A, 0x0A
        db "Mellivora OS is written by James-HoneyBadger and contributors.", 0x0A, 0x0A
        db "Released under the terms in /LICENSE - see also license.txt.", 0x0A, 0x0A
        db "Built entirely in NASM x86 assembly, with build glue in GNU make", 0x0A
        db "and Python (populate.py). QEMU is the reference VM target.", 0x0A, 0x0A
        db "Stay curious. Be honey-badger about bugs.", 0x0A, 0

p_meminfo:
        db "MEMINFO(1)          Mellivora OS Manual          MEMINFO(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    meminfo - report memory usage, uptime and PID", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    meminfo", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Prints the amount of free physical memory, total memory", 0x0A
        db "    discovered at boot, kernel uptime in HH:MM:SS, and the", 0x0A
        db "    caller's process ID. Memory is reported in kilobytes;", 0x0A
        db "    each page is 4 KiB.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    df, du, ps, pkginfo", 0x0A, 0

p_tag:
        db "TAG(1)              Mellivora OS Manual              TAG(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    tag - tag files with keywords and search by tag", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    tag add  <file> <tag>     attach <tag> to <file>", 0x0A
        db "    tag list <file>           print all tags on <file>", 0x0A
        db "    tag find <tag>            list every file with <tag>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Tags are stored in /home/.tags.db, one association per line", 0x0A
        db "    in the form <file>:<tag>. The database is plain text and", 0x0A
        db "    safe to edit by hand.", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    tag add report.txt urgent", 0x0A
        db "    tag find urgent", 0x0A, 0

p_histgrep:
        db "HISTGREP(1)         Mellivora OS Manual         HISTGREP(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    histgrep - search shell command history", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    histgrep <pattern>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Performs a case-insensitive substring search across", 0x0A
        db "    /home/.history (or /etc/.history if no per-user file", 0x0A
        db "    exists). Matching lines are printed in their original order.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    grep, shell", 0x0A, 0

p_bnotify:
        db "BNOTIFY(1)          Mellivora OS Manual          BNOTIFY(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    bnotify - post a Burrows desktop notification", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    bnotify [-c <attr>] <message...>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Sends <message> to the Burrows notification daemon. The", 0x0A
        db "    optional -c flag selects a VGA text attribute byte (decimal", 0x0A
        db "    or 0x.. hex); the default is 0x0E (yellow on black).", 0x0A, 0x0A
        db "EXAMPLES", 0x0A
        db "    bnotify Build complete", 0x0A
        db "    bnotify -c 0x4F Disk almost full", 0x0A, 0

p_bcal:
        db "BCAL(1)             Mellivora OS Manual             BCAL(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    bcal - calendar with personal events", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    bcal                      show today + upcoming events", 0x0A
        db "    bcal add YYYY-MM-DD text  schedule an event", 0x0A
        db "    bcal list                 list every saved event", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Events are stored in /home/.events as plain text. bcal is", 0x0A
        db "    intentionally simple - it does not understand recurrence,", 0x0A
        db "    but it sorts cleanly because dates are ISO-formatted.", 0x0A, 0

p_plasma:
        db "PLASMA(1)           Mellivora OS Manual           PLASMA(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    plasma - animated text-mode plasma demo", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    plasma", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Renders an 80x25 plasma effect by writing directly to the", 0x0A
        db "    VGA text framebuffer at 0xB8000. Press any key to quit.", 0x0A
        db "    Used as a smoke test for the scheduler and timer.", 0x0A, 0

p_nim:
        db "NIM(6)              Mellivora OS Manual              NIM(6)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    nim - single-pile Nim game (last stick loses)", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    nim", 0x0A, 0x0A
        db "RULES", 0x0A
        db "    The pile starts with 21 sticks. Each turn you remove 1, 2", 0x0A
        db "    or 3 sticks. Whoever takes the last stick loses. The CPU", 0x0A
        db "    plays the optimal misere strategy, so first-mover wins are", 0x0A
        db "    rare but possible if you can force the right residue.", 0x0A, 0

p_mkprog:
        db "MKPROG(1)           Mellivora OS Manual           MKPROG(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    mkprog - generate a new assembly program skeleton", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    mkprog <name>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Writes a runnable <name>.asm template that includes", 0x0A
        db "    syscalls.inc, prints a greeting and exits cleanly. The", 0x0A
        db "    @@NAME@@ token in the template is replaced with <name>.", 0x0A, 0x0A
        db "EXAMPLE", 0x0A
        db "    mkprog hello && asm hello.asm", 0x0A, 0

p_dnslook:
        db "DNSLOOK(1)          Mellivora OS Manual          DNSLOOK(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    dnslook - resolve a hostname to an IPv4 address", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    dnslook <hostname>", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Wraps SYS_DNS (#46) and prints the result in dotted-quad", 0x0A
        db "    notation. Requires a configured network stack; see net.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    net, ping, wget, dig", 0x0A, 0

p_play:
        db "PLAY(1)             Mellivora OS Manual             PLAY(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    play - play a sequence of notes on the PC speaker", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    play <note> [<note>...]", 0x0A, 0x0A
        db "NOTE SYNTAX", 0x0A
        db "    Lower-case c d e f g a b   octave 4 (middle)", 0x0A
        db "    Upper-case C D E F G A B   octave 5", 0x0A
        db "    Suffix s                   sharp (e.g. fs, Cs)", 0x0A
        db "    Suffix /N                  duration 1/N of a quarter", 0x0A
        db "    Token 'r'                  rest", 0x0A, 0x0A
        db "EXAMPLE", 0x0A
        db "    play c e g C", 0x0A, 0

p_journal:
        db "JOURNAL(1)          Mellivora OS Manual          JOURNAL(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    journal - append timestamped entries to a personal log", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    journal <text...>         append a new entry", 0x0A
        db "    journal                   print the journal", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Each entry is written to /home/journal.txt prefixed with", 0x0A
        db "    YYYY-MM-DD HH:MM. The file is plain text - back it up like", 0x0A
        db "    any other document.", 0x0A, 0

p_theme:
        db "THEME(1)            Mellivora OS Manual            THEME(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    theme - switch the system color theme", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    theme                     show current theme + list", 0x0A
        db "    theme <name>              apply a named theme", 0x0A, 0x0A
        db "THEMES", 0x0A
        db "    classic amber green cyan pink inverse solar matrix", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Sets the default text attribute via SYS_SETCOLOR and clears", 0x0A
        db "    the screen. The chosen theme name is persisted to", 0x0A
        db "    /home/.theme so other apps can read it.", 0x0A, 0

p_tutorial:
        db "TUTORIAL(1)         Mellivora OS Manual         TUTORIAL(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    tutorial - interactive welcome tour", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    tutorial", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    An eight-page guided introduction covering the shell, the", 0x0A
        db "    manual system, the filesystem, pipelines, the Burrows", 0x0A
        db "    desktop, programming and useful next steps.", 0x0A, 0x0A
        db "KEYS", 0x0A
        db "    Enter advance, b back, q quit", 0x0A, 0

p_pkginfo:
        db "PKGINFO(1)          Mellivora OS Manual          PKGINFO(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    pkginfo - print Mellivora system identification info", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    pkginfo", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Prints the OS name, version, architecture, hostname, total", 0x0A
        db "    and free memory, and uptime. This is the canonical command", 0x0A
        db "    to include in bug reports.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    meminfo, df, credits", 0x0A, 0

p_more:
        db "MORE(1)             Mellivora OS Manual             MORE(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    more - page through text file or piped input", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    more [FILE]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Displays FILE (or stdin if omitted) one screen at a time.", 0x0A
        db "    Pause at each page; press SPACE for the next page,", 0x0A
        db "    ENTER for one more line, or Q to quit.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    cat, grep, head, tail", 0x0A, 0

p_sed:
        db "SED(1)              Mellivora OS Manual              SED(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    sed - stream editor for filtering and transforming text", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    sed [-n] [-e SCRIPT] [SCRIPT] [FILE]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Applies editing commands to each line of FILE (or stdin).", 0x0A, 0x0A
        db "COMMANDS", 0x0A
        db "    s/FIND/REPL/[g]  Substitute (g=global)", 0x0A
        db "    d                Delete line", 0x0A
        db "    p                Print line", 0x0A
        db "    =                Print line number", 0x0A
        db "    q                Quit", 0x0A
        db "    y/SRC/DST/       Transliterate characters", 0x0A, 0x0A
        db "ADDRESS FORMS", 0x0A
        db "    N        line number    $  last line", 0x0A
        db "    /regex/  regex match    N,M  range", 0x0A, 0x0A
        db "OPTIONS", 0x0A
        db "    -n   suppress automatic print", 0x0A
        db "    -e SCRIPT  specify script on command line", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    awk, grep", 0x0A, 0

p_awk:
        db "AWK(1)              Mellivora OS Manual              AWK(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    awk - pattern-action text processor", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    awk [-F SEP] 'PROGRAM' [FILE]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Reads lines from FILE (or stdin) and runs PROGRAM for each", 0x0A
        db "    line that matches the pattern.", 0x0A, 0x0A
        db "PATTERNS", 0x0A
        db "    BEGIN     run before first line", 0x0A
        db "    END       run after last line", 0x0A
        db "    /regex/   lines matching regex", 0x0A
        db "    NR==N     specific line number", 0x0A
        db "    (empty)   every line", 0x0A, 0x0A
        db "ACTIONS", 0x0A
        db "    print [EXPR,...]    print fields or strings", 0x0A
        db "    gsub(PAT,REPL)      global substitute on $0", 0x0A
        db "    sub(PAT,REPL)       single substitute on $0", 0x0A, 0x0A
        db "VARIABLES", 0x0A
        db "    $0    whole line  $N  Nth field  NR  line#  NF  field count", 0x0A, 0x0A
        db "OPTIONS", 0x0A
        db "    -F SEP  set field separator (default space)", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    sed, grep", 0x0A, 0

p_top:
        db "TOP(1)              Mellivora OS Manual              TOP(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    top - real-time process and memory monitor", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    top", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Displays a live-updating table of all running tasks.", 0x0A
        db "    Shows slot, PID, state, priority, and task name.", 0x0A
        db "    A memory usage bar shows used/total physical pages.", 0x0A
        db "    Refreshes every second.", 0x0A, 0x0A
        db "KEYS", 0x0A
        db "    q / ESC   quit", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    ps, meminfo", 0x0A, 0

p_tar:
        db "TAR(1)              Mellivora OS Manual              TAR(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    tar - create, extract, and list HBTAR archives", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    tar c ARCHIVE FILE [FILE ...]   create", 0x0A
        db "    tar x ARCHIVE                  extract", 0x0A
        db "    tar t ARCHIVE                  list contents", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Uses the HBTAR1.0 flat binary archive format.", 0x0A
        db "    Each entry stores a 256-byte padded name and file data.", 0x0A
        db "    Supports up to 64 files, 64 KB each.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    cp, mv", 0x0A, 0

p_nm:
        db "NM(1)               Mellivora OS Manual               NM(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    nm - list symbol table entries from ELF32 binaries", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    nm FILE [FILE ...]", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Reads the SHT_SYMTAB section of each ELF32 binary and", 0x0A
        db "    prints: address, type letter, and symbol name.", 0x0A, 0x0A
        db "TYPE LETTERS", 0x0A
        db "    T/t  text (code)   D/d  data   B/b  BSS", 0x0A
        db "    R/r  read-only     U    undefined   A  absolute", 0x0A
        db "    (uppercase = global, lowercase = local)", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    dis, debug", 0x0A, 0

p_pciinfo:
        db "PCIINFO(1)          Mellivora OS Manual          PCIINFO(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    pciinfo - display detected PCI hardware devices", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    pciinfo", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Queries the kernel PCI device table via SYS_PCI_FIND (101)", 0x0A
        db "    and lists known hardware (network cards, audio, IDE, etc.)", 0x0A
        db "    with their bus:device.function addresses.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    audiodemo, pkginfo", 0x0A, 0

p_audiodemo:
        db "AUDIODEMO(1)        Mellivora OS Manual        AUDIODEMO(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    audiodemo - demonstrate audio output capabilities", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    audiodemo", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Probes for audio hardware (SB16 and AC'97 via PCI),", 0x0A
        db "    plays a short melody on the PC speaker, then attempts", 0x0A
        db "    16-bit PCM playback via SYS_AUDIO_PLAY if SB16 is present.", 0x0A
        db "    Press any key to exit after the melody.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    soundviz, pciinfo, play", 0x0A, 0

p_blitdemo:
        db "BLITDEMO(1)         Mellivora OS Manual         BLITDEMO(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    blitdemo - sprite blit animation demo", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    blitdemo", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Demonstrates direct-write sprite blitting with color-key", 0x0A
        db "    transparency on the 640x480 VBE framebuffer.", 0x0A
        db "    A 16x16 diamond sprite bounces around the screen.", 0x0A
        db "    Press any key to exit.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    gfxdemo, gfxplasma, soundviz", 0x0A, 0

p_gfxdemo:
        db "GFXDEMO(1)          Mellivora OS Manual          GFXDEMO(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    gfxdemo - VBE graphics primitives demo", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    gfxdemo", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Renders a scrolling rainbow gradient background with a", 0x0A
        db "    bouncing sprite and title overlay using the 640x480 VBE", 0x0A
        db "    framebuffer shadow buffer.", 0x0A
        db "    Press any key to exit.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    blitdemo, gfxplasma, cube", 0x0A, 0

p_gfxplasma:
        db "GFXPLASMA(1)        Mellivora OS Manual        GFXPLASMA(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    gfxplasma - animated plasma effect on VBE framebuffer", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    gfxplasma", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Classic plasma: sum of sine waves evaluated per pixel and", 0x0A
        db "    mapped to a rotating rainbow palette. Renders 320x240 cells", 0x0A
        db "    as 2x2 blocks to fill 640x480. Loops continuously.", 0x0A
        db "    Press any key to exit.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    gfxdemo, blitdemo, julia", 0x0A, 0

p_soundviz:
        db "SOUNDVIZ(1)         Mellivora OS Manual         SOUNDVIZ(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    soundviz - SB16 audio waveform visualizer", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    soundviz", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Plays a three-note chord sequence (C4, E4, G4) via SB16", 0x0A
        db "    while drawing an animated waveform on the 640x480 VBE", 0x0A
        db "    framebuffer. Each note triggers a coloured expanding ring.", 0x0A
        db "    Falls back to PC-speaker beeps if SB16 is absent.", 0x0A
        db "    Press any key to exit.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    audiodemo, gfxdemo, play", 0x0A, 0

p_breakout:
        db "BREAKOUT(1)         Mellivora OS Manual         BREAKOUT(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    breakout - Breakout/Arkanoid clone", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    breakout", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Classic brick-breaking game on the 640x480 VBE framebuffer.", 0x0A
        db "    Five rows of ten bricks; ball bounces off walls, paddle,", 0x0A
        db "    and bricks. Three lives.", 0x0A, 0x0A
        db "CONTROLS", 0x0A
        db "    LEFT / RIGHT arrow keys  move paddle", 0x0A
        db "    Q / any non-arrow key    quit", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    cube, julia, galaga", 0x0A, 0

p_cube:
        db "CUBE(1)             Mellivora OS Manual             CUBE(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    cube - rotating 3-D wireframe cube", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    cube", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Renders a perspective-projected unit cube rotating around", 0x0A
        db "    the Y and X axes using 16.16 fixed-point sine/cosine tables.", 0x0A
        db "    Edges are drawn with SYS_DRAW_LINE into the shadow buffer.", 0x0A
        db "    Press any key to exit.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    julia, mandelbrot, gfxdemo", 0x0A, 0

p_julia:
        db "JULIA(1)            Mellivora OS Manual            JULIA(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    julia - interactive Julia set renderer", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    julia", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Renders the Julia set for z -> z^2 + c using 16.16", 0x0A
        db "    fixed-point arithmetic. Renders 320x240 scaled to 640x480.", 0x0A, 0x0A
        db "CONTROLS", 0x0A
        db "    Arrow keys   move Julia parameter c (re/im) by 0.02", 0x0A
        db "    + / -        zoom in / out", 0x0A
        db "    R            reset to default view", 0x0A
        db "    Any other    exit", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    mandelbrot, cube, gfxplasma", 0x0A, 0

p_mandelbrot:
        db "MANDELBROT(1)       Mellivora OS Manual       MANDELBROT(1)", 0x0A, 0x0A
        db "NAME", 0x0A
        db "    mandelbrot - Mandelbrot set renderer", 0x0A, 0x0A
        db "SYNOPSIS", 0x0A
        db "    mandelbrot", 0x0A, 0x0A
        db "DESCRIPTION", 0x0A
        db "    Renders the Mandelbrot set at 640x480x32bpp using direct", 0x0A
        db "    shadow-buffer writes and 16.16 fixed-point arithmetic.", 0x0A
        db "    Rainbow colour-escape palette; black interior. Renders", 0x0A
        db "    progressively (flushes every 32 rows).", 0x0A
        db "    Press any key to exit.", 0x0A, 0x0A
        db "SEE ALSO", 0x0A
        db "    julia, cube, gfxplasma", 0x0A, 0

;============================================================
; Buffers
;============================================================
arg_buf:        times 256 db 0
kw_buf:         times 256 db 0
path_buf:       times 280 db 0
file_buf:       times 32768 db 0

; typist.asm - Typing Tutor for Mellivora OS
; Text-mode typing practice with WPM and accuracy tracking.
; Displays sentences for the user to type, highlights errors in real time,
; and shows statistics after each round.

%include "syscalls.inc"

; Colors
COL_TITLE       equ 0x0E        ; yellow on black
COL_PROMPT      equ 0x0F        ; white
COL_CORRECT     equ 0x0A        ; green
COL_ERROR       equ 0x0C        ; red
COL_CURSOR      equ 0x0B        ; cyan
COL_STATS       equ 0x0E        ; yellow
COL_MENU        equ 0x07        ; grey
COL_HIGHLIGHT   equ 0x0F        ; bright white

NUM_LESSONS     equ 10
MAX_INPUT       equ 256

start:
        ; Initialize session totals
        mov dword [total_chars], 0
        mov dword [total_errors], 0
        mov dword [total_wpm], 0
        mov dword [lessons_done], 0

        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Show title
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title
        int 0x80

        ; Show menu
        mov eax, SYS_SETCOLOR
        mov ebx, COL_MENU
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu
        int 0x80

.menu_loop:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, '1'
        jb .menu_loop
        cmp al, '3'
        ja .menu_loop

        ; al = '1'-'3' -> difficulty
        sub al, '0'
        movzx eax, al
        mov [difficulty], eax

        ; Run all lessons for this difficulty
        mov dword [lesson_num], 0

.next_lesson:
        mov eax, [lesson_num]
        cmp eax, NUM_LESSONS
        jge .show_final

        call run_lesson

        inc dword [lesson_num]
        jmp .next_lesson

.show_final:
        ; Show final stats
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_final_header
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, COL_STATS
        int 0x80

        ; Total characters typed
        mov eax, SYS_PRINT
        mov ebx, str_total_chars
        int 0x80
        mov eax, [total_chars]
        call print_number
        call print_newline

        ; Total errors
        mov eax, SYS_PRINT
        mov ebx, str_total_errors
        int 0x80
        mov eax, [total_errors]
        call print_number
        call print_newline

        ; Overall accuracy
        mov eax, SYS_PRINT
        mov ebx, str_accuracy
        int 0x80
        mov eax, [total_chars]
        test eax, eax
        jz .no_acc
        sub eax, [total_errors]
        imul eax, 100
        xor edx, edx
        div dword [total_chars]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_percent
        int 0x80
        jmp .acc_done
.no_acc:
        mov eax, SYS_PRINT
        mov ebx, str_na
        int 0x80
.acc_done:
        call print_newline

        ; Average WPM
        mov eax, SYS_PRINT
        mov ebx, str_avg_wpm
        int 0x80
        mov eax, [total_wpm]
        mov ecx, [lessons_done]
        test ecx, ecx
        jz .no_wpm
        xor edx, edx
        div ecx
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_wpm_unit
        int 0x80
        jmp .wpm_done
.no_wpm:
        mov eax, SYS_PRINT
        mov ebx, str_na
        int 0x80
.wpm_done:
        call print_newline
        call print_newline

        mov eax, SYS_SETCOLOR
        mov ebx, COL_MENU
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_press_key
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        jmp start

.quit:
        mov eax, SYS_EXIT
        int 0x80


; ─── run_lesson ──────────────────────────────────────────────
; Runs a single typing lesson
; Uses [lesson_num] and [difficulty] to pick text
run_lesson:
        PUSHALL

        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Get lesson text pointer
        call get_lesson_text       ; -> ESI = text pointer

        ; Show lesson header
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_lesson
        int 0x80
        mov eax, [lesson_num]
        inc eax
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_of_ten
        int 0x80
        call print_newline
        call print_newline

        ; Display the text to type in white
        mov eax, SYS_SETCOLOR
        mov ebx, COL_PROMPT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        call print_newline
        call print_newline

        ; Draw underline separator
        mov eax, SYS_SETCOLOR
        mov ebx, COL_MENU
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80
        call print_newline

        ; Measure text length
        mov edi, esi                   ; edi = source text
        xor ecx, ecx
.len_loop:
        cmp byte [edi + ecx], 0
        je .len_done
        inc ecx
        jmp .len_loop
.len_done:
        mov [text_len], ecx

        ; Clear input buffer
        mov dword [input_pos], 0
        mov dword [error_count], 0

        ; Read start time (tick count at 100Hz)
        mov eax, SYS_GETTIME
        int 0x80
        mov [start_ticks], eax

        ; Input loop
        mov eax, SYS_SETCOLOR
        mov ebx, COL_CORRECT
        int 0x80

.input_loop:
        mov eax, SYS_GETCHAR
        int 0x80
        movzx ebx, al

        ; Escape = abort lesson
        cmp bl, 27
        je .lesson_abort

        ; Backspace
        cmp bl, 8
        je .backspace

        ; Only printable chars
        cmp bl, 32
        jb .input_loop
        cmp bl, 126
        ja .input_loop

        ; Check if we've reached the end
        mov ecx, [input_pos]
        cmp ecx, [text_len]
        jge .input_loop

        ; Store character
        mov [input_buf + ecx], bl

        ; Compare with expected
        cmp bl, [edi + ecx]
        je .char_correct

        ; Error
        inc dword [error_count]
        mov eax, SYS_SETCOLOR
        mov ebx, COL_ERROR
        int 0x80
        jmp .show_char

.char_correct:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_CORRECT
        int 0x80

.show_char:
        ; Print the typed character
        mov ecx, [input_pos]
        movzx ebx, byte [input_buf + ecx]
        mov eax, SYS_PUTCHAR
        int 0x80

        ; Reset to correct color
        mov eax, SYS_SETCOLOR
        mov ebx, COL_CORRECT
        int 0x80

        inc dword [input_pos]

        ; Check if finished
        mov ecx, [input_pos]
        cmp ecx, [text_len]
        jl .input_loop

        ; ─── Lesson complete ───
        ; Read end time
        mov eax, SYS_GETTIME
        int 0x80
        mov [end_ticks], eax

        ; Calculate elapsed ticks
        sub eax, [start_ticks]
        test eax, eax
        jnz .has_time
        mov eax, 1                     ; avoid /0
.has_time:
        mov [elapsed_ticks], eax

        ; Calculate WPM: (chars / 5) / (ticks / (100*60))
        ; = chars * 100 * 60 / (5 * ticks)
        ; = chars * 1200 / ticks
        mov eax, [text_len]
        imul eax, 1200
        xor edx, edx
        div dword [elapsed_ticks]
        mov [lesson_wpm], eax

        ; Calculate accuracy
        mov eax, [text_len]
        sub eax, [error_count]
        js .zero_acc
        imul eax, 100
        xor edx, edx
        div dword [text_len]
        mov [lesson_accuracy], eax
        jmp .show_results

.zero_acc:
        mov dword [lesson_accuracy], 0

.show_results:
        call print_newline
        call print_newline

        mov eax, SYS_SETCOLOR
        mov ebx, COL_STATS
        int 0x80

        ; Speed
        mov eax, SYS_PRINT
        mov ebx, str_speed
        int 0x80
        mov eax, [lesson_wpm]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_wpm_unit
        int 0x80
        call print_newline

        ; Accuracy
        mov eax, SYS_PRINT
        mov ebx, str_acc_label
        int 0x80
        mov eax, [lesson_accuracy]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_percent
        int 0x80
        call print_newline

        ; Errors
        mov eax, SYS_PRINT
        mov ebx, str_errors
        int 0x80
        mov eax, [error_count]
        call print_number
        call print_newline

        ; Grade
        mov eax, SYS_PRINT
        mov ebx, str_grade
        int 0x80
        mov eax, [lesson_accuracy]
        cmp eax, 95
        jge .grade_a
        cmp eax, 85
        jge .grade_b
        cmp eax, 70
        jge .grade_c
        jmp .grade_f

.grade_a:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_CORRECT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_grade_a
        int 0x80
        jmp .grade_done

.grade_b:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_HIGHLIGHT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_grade_b
        int 0x80
        jmp .grade_done

.grade_c:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_STATS
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_grade_c
        int 0x80
        jmp .grade_done

.grade_f:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_ERROR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_grade_f
        int 0x80

.grade_done:
        call print_newline

        ; Accumulate totals
        mov eax, [text_len]
        add [total_chars], eax
        mov eax, [error_count]
        add [total_errors], eax
        mov eax, [lesson_wpm]
        add [total_wpm], eax
        inc dword [lessons_done]

        ; Prompt to continue
        mov eax, SYS_SETCOLOR
        mov ebx, COL_MENU
        int 0x80
        call print_newline
        mov eax, SYS_PRINT
        mov ebx, str_continue
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80

        POPALL
        ret

.lesson_abort:
        POPALL
        ret

.backspace:
        mov ecx, [input_pos]
        test ecx, ecx
        jz .input_loop
        dec dword [input_pos]
        ; Print backspace sequence: BS, space, BS
        mov eax, SYS_PUTCHAR
        mov ebx, 8
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 8
        int 0x80
        jmp .input_loop


; ─── get_lesson_text ──────────────────────────────────────────
; Returns ESI = pointer to lesson text string
; Based on [lesson_num] (0-9) and [difficulty] (1-3)
get_lesson_text:
        mov eax, [difficulty]
        dec eax                        ; 0-2
        imul eax, NUM_LESSONS * 8      ; offset into difficulty table
        mov ecx, [lesson_num]
        lea rsi, [lesson_table + rax + rcx * 8]
        mov rsi, [rsi]
        ret


; ─── print_number ────────────────────────────────────────────
; Print unsigned integer in EAX as decimal
print_number:
        PUSHALL
        mov ecx, 0                     ; digit count
        mov ebx, 10
.pn_div:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .pn_div
.pn_print:
        pop rbx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pn_print
        POPALL
        ret


; ─── print_newline ───────────────────────────────────────────
print_newline:
        push rax
        push rbx
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop rbx
        pop rax
        ret


; ═════════════════════════════════════════════════════════════
; DATA SECTION
; ═════════════════════════════════════════════════════════════

str_title:      db '╔═══════════════════════════════════╗', 10
                db '║     M E L L I V O R A  T Y P E    ║', 10
                db '║        Typing Tutor v1.0          ║', 10
                db '╚═══════════════════════════════════╝', 10, 10, 0

str_menu:       db 'Choose difficulty:', 10, 10
                db '  1 - Beginner   (short words)', 10
                db '  2 - Intermediate (sentences)', 10
                db '  3 - Advanced   (paragraphs)', 10, 10
                db '  Q - Quit', 10, 10
                db '> ', 0

str_lesson:     db 'Lesson ', 0
str_of_ten:     db ' of 10', 0
str_separator:  db '----------------------------------------', 0
str_speed:      db 'Speed:    ', 0
str_wpm_unit:   db ' WPM', 0
str_acc_label:  db 'Accuracy: ', 0
str_percent:    db '%', 0
str_errors:     db 'Errors:   ', 0
str_grade:      db 'Grade:    ', 0
str_grade_a:    db 'A - Excellent!', 0
str_grade_b:    db 'B - Good job!', 0
str_grade_c:    db 'C - Keep practicing!', 0
str_grade_f:    db 'F - Needs work!', 0
str_continue:   db 'Press any key for next lesson...', 0
str_press_key:  db 'Press any key to return to menu...', 0

str_final_header: db 10, '══════ SESSION RESULTS ══════', 10, 10, 0
str_total_chars:  db 'Total characters: ', 0
str_total_errors: db 'Total errors:     ', 0
str_accuracy:     db 'Overall accuracy: ', 0
str_avg_wpm:      db 'Average speed:    ', 0
str_na:           db 'N/A', 0

; ─── Lesson texts ────────────────────────────────────────────

; Difficulty 1: Beginner - short words and home row focus
les_b1: db 'the cat sat on the mat', 0
les_b2: db 'a dog and a frog in a log', 0
les_b3: db 'she sells sea shells by the sea', 0
les_b4: db 'five red fish swim fast', 0
les_b5: db 'the quick fox ran over the hill', 0
les_b6: db 'jump high and land soft on sand', 0
les_b7: db 'asdf jkl; asdf jkl; asdf jkl;', 0
les_b8: db 'home row keys are for fast typing', 0
les_b9: db 'pack my box with five dozen eggs', 0
les_b10: db 'how vexingly quick daft zebras jump', 0

; Difficulty 2: Intermediate - full sentences
les_i1: db 'The quick brown fox jumps over the lazy dog.', 0
les_i2: db 'Programming is the art of telling a computer what to do.', 0
les_i3: db 'A journey of a thousand miles begins with a single step.', 0
les_i4: db 'To be or not to be, that is the question.', 0
les_i5: db 'All that glitters is not gold, but it sure looks nice.', 0
les_i6: db 'The honey badger is the most fearless animal in the world.', 0
les_i7: db 'In the beginning was the command line, and it was good.', 0
les_i8: db 'Mellivora OS runs on bare metal with no dependencies.', 0
les_i9: db 'Real programmers write in assembly language, close to the metal.', 0
les_i10: db 'Practice makes perfect, so keep typing every single day.', 0

; Difficulty 3: Advanced - longer texts with numbers and symbols
les_a1: db 'The 80486 processor (1989) brought 1.2 million transistors to x86.', 0
les_a2: db 'Port 0x220 is the default I/O base for Sound Blaster 16 cards.', 0
les_a3: db 'In NASM: mov eax, [ebx+ecx*4+8] uses SIB addressing mode.', 0
les_a4: db 'VBE mode 0x112 = 640x480x32bpp; pitch = width * (bpp/8) bytes.', 0
les_a5: db 'Interrupt 0x80 is used for system calls: EAX=num, EBX-ESI=args.', 0
les_a6: db 'The formula for WPM is: (characters / 5) / (minutes elapsed).', 0
les_a7: db 'A 16.16 fixed-point number stores 65536 (0x10000) as "1.0".', 0
les_a8: db 'DMA channel #1 (8-bit) transfers audio buffers to the SB16 DAC.', 0
les_a9: db 'HBFS uses 512-byte sectors; root dir at LBA 1, data at LBA 17+.', 0
les_a10: db 'gcc -m32 -ffreestanding -nostdlib -o kernel.bin kernel.c -T link.ld', 0

; Lesson pointer tables (10 entries per difficulty)
lesson_table:
        ; Beginner (difficulty 1)
        dq les_b1, les_b2, les_b3, les_b4, les_b5
        dq les_b6, les_b7, les_b8, les_b9, les_b10
        ; Intermediate (difficulty 2)
        dq les_i1, les_i2, les_i3, les_i4, les_i5
        dq les_i6, les_i7, les_i8, les_i9, les_i10
        ; Advanced (difficulty 3)
        dq les_a1, les_a2, les_a3, les_a4, les_a5
        dq les_a6, les_a7, les_a8, les_a9, les_a10

; ═════════════════════════════════════════════════════════════
; BSS SECTION
; ═════════════════════════════════════════════════════════════

section .bss

difficulty:     resd 1
lesson_num:     resd 1
input_pos:      resd 1
error_count:    resd 1
text_len:       resd 1
start_ticks:    resd 1
end_ticks:      resd 1
elapsed_ticks:  resd 1
lesson_wpm:     resd 1
lesson_accuracy: resd 1

total_chars:    resd 1
total_errors:   resd 1
total_wpm:      resd 1
lessons_done:   resd 1

input_buf:      resb MAX_INPUT

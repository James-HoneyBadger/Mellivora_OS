; figlet.asm - Large ASCII art text banner generator
; Usage: figlet TEXT
; Renders text using a built-in block font (5 lines tall)

%include "syscalls.inc"

FONT_HEIGHT equ 5
CHAR_WIDTH  equ 6               ; Max width per character including gap

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Print each of 5 rows
        xor ebp, ebp            ; Row counter (0-4)

.row_loop:
        cmp ebp, FONT_HEIGHT
        jge .done

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80

        ; Walk through each character in the argument
        mov esi, arg_buf
.char_loop:
        lodsb
        test al, al
        jz .row_end
        cmp al, ' '
        je .space_char

        ; Convert to uppercase
        cmp al, 'a'
        jb .not_lower
        cmp al, 'z'
        ja .not_lower
        sub al, 32
.not_lower:
        ; Map character to font index
        cmp al, 'A'
        jb .other_char
        cmp al, 'Z'
        ja .check_digit

        ; A-Z: index = char - 'A'
        sub al, 'A'
        movzx edx, al
        jmp .print_glyph

.check_digit:
        cmp al, '0'
        jb .other_char
        cmp al, '9'
        ja .other_char
        sub al, '0'
        add al, 26              ; After letters
        movzx edx, al
        jmp .print_glyph

.other_char:
        ; Punctuation: ! = 36, space = skip
        cmp al, '!'
        je .exclaim
        cmp al, '?'
        je .question
        cmp al, '.'
        je .period
        cmp al, '-'
        je .dash
        ; Default: space
.space_char:
        push esi
        mov eax, SYS_PRINT
        mov ebx, str_space
        int 0x80
        pop esi
        jmp .char_loop

.exclaim:
        mov edx, 36
        jmp .print_glyph
.question:
        mov edx, 37
        jmp .print_glyph
.period:
        mov edx, 38
        jmp .print_glyph
.dash:
        mov edx, 39
        jmp .print_glyph

.print_glyph:
        ; EDX = glyph index, EBP = row
        ; Address = font_data + (index * FONT_HEIGHT + row) * CHAR_WIDTH
        push esi
        mov eax, edx
        imul eax, FONT_HEIGHT
        add eax, ebp
        imul eax, CHAR_WIDTH
        lea ebx, [font_data + eax]
        mov eax, SYS_PRINT
        int 0x80
        pop esi
        jmp .char_loop

.row_end:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        inc ebp
        jmp .row_loop

.done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; DATA
;=======================================================================

usage_str: db "Usage: figlet TEXT", 0x0A, 0
str_space: db "      ", 0       ; 6 spaces

; Block font - 5 rows per glyph, 6 chars wide each
; Letters A-Z, digits 0-9, then ! ? . -
font_data:
; A
db " #### ", "#    #", "######", "#    #", "#    #"
; B
db "##### ", "#    #", "##### ", "#    #", "##### "
; C
db " #####", "#     ", "#     ", "#     ", " #####"
; D
db "####  ", "#   # ", "#    #", "#   # ", "####  "
; E
db "######", "#     ", "####  ", "#     ", "######"
; F
db "######", "#     ", "####  ", "#     ", "#     "
; G
db " #####", "#     ", "#  ###", "#    #", " #####"
; H
db "#    #", "#    #", "######", "#    #", "#    #"
; I
db " #### ", "  ##  ", "  ##  ", "  ##  ", " #### "
; J
db "   ###", "    # ", "    # ", "#   # ", " ### "
; K
db "#   # ", "#  #  ", "###   ", "#  #  ", "#   # "
; L
db "#     ", "#     ", "#     ", "#     ", "######"
; M
db "#    #", "##  ##", "# ## #", "#    #", "#    #"
; N
db "#    #", "##   #", "# #  #", "#  # #", "#   ##"
; O
db " #### ", "#    #", "#    #", "#    #", " #### "
; P
db "##### ", "#    #", "##### ", "#     ", "#     "
; Q
db " #### ", "#    #", "#  # #", "#   # ", " ## # "
; R
db "##### ", "#    #", "##### ", "#  #  ", "#   # "
; S
db " #####", "#     ", " #### ", "     #", "##### "
; T
db "######", "  ##  ", "  ##  ", "  ##  ", "  ##  "
; U
db "#    #", "#    #", "#    #", "#    #", " #### "
; V
db "#    #", "#    #", " #  # ", " #  # ", "  ##  "
; W
db "#    #", "#    #", "# ## #", "##  ##", "#    #"
; X
db "#    #", " #  # ", "  ##  ", " #  # ", "#    #"
; Y
db "#    #", " #  # ", "  ##  ", "  ##  ", "  ##  "
; Z
db "######", "    # ", "  ##  ", " #    ", "######"
; 0
db " #### ", "#   ##", "#  # #", "##   #", " #### "
; 1
db "  #   ", " ##   ", "  #   ", "  #   ", " ###  "
; 2
db " #### ", "#    #", "   ## ", " ##   ", "######"
; 3
db " #### ", "#    #", "  ### ", "#    #", " #### "
; 4
db "#    #", "#    #", "######", "     #", "     #"
; 5
db "######", "#     ", "##### ", "     #", "##### "
; 6
db " #### ", "#     ", "##### ", "#    #", " #### "
; 7
db "######", "    # ", "   #  ", "  #   ", "  #   "
; 8
db " #### ", "#    #", " #### ", "#    #", " #### "
; 9
db " #### ", "#    #", " #####", "     #", " #### "
; !
db "  ##  ", "  ##  ", "  ##  ", "      ", "  ##  "
; ?
db " #### ", "#    #", "   ## ", "      ", "  ##  "
; .
db "      ", "      ", "      ", "      ", "  ##  "
; -
db "      ", "      ", "######", "      ", "      "

arg_buf: times 256 db 0

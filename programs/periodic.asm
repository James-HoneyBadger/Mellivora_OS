; periodic.asm - Interactive Periodic Table of Elements for Mellivora OS
; Navigate with arrow keys, press Enter for detail, 'q' to quit.
%include "syscalls.inc"

SCREEN_W        equ 80
SCREEN_H        equ 25
TABLE_COLS      equ 18
TABLE_ROWS      equ 9           ; Main table rows

; Element categories (for coloring)
CAT_NONE        equ 0
CAT_ALKALI      equ 1           ; Red
CAT_ALKALINE    equ 2           ; Yellow
CAT_TRANS       equ 3           ; Cyan   (transition metals)
CAT_BASIC       equ 4           ; Blue
CAT_SEMIMETAL   equ 5           ; Green
CAT_NONMETAL    equ 6           ; White
CAT_HALOGEN     equ 7           ; Magenta
CAT_NOBLE       equ 8           ; Light blue
CAT_LANTHANIDE  equ 9           ; Orange-ish (brown)
CAT_ACTINIDE    equ 10          ; Light red

start:
        mov dword [cursor_x], 0
        mov dword [cursor_y], 0
        call draw_table

;=== Main loop ===
.main_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .idle

        cmp al, 'q'
        je .exit
        cmp al, 27
        je .exit

        cmp al, KEY_UP
        je .up
        cmp al, KEY_DOWN
        je .down
        cmp al, KEY_LEFT
        je .left
        cmp al, KEY_RIGHT
        je .right
        cmp al, 13             ; Enter
        je .detail
        jmp .idle

.up:
        cmp dword [cursor_y], 0
        je .idle
        dec dword [cursor_y]
        call draw_table
        jmp .idle
.down:
        mov eax, [cursor_y]
        cmp eax, TABLE_ROWS - 1
        jge .idle
        inc dword [cursor_y]
        call draw_table
        jmp .idle
.left:
        cmp dword [cursor_x], 0
        je .idle
        dec dword [cursor_x]
        call draw_table
        jmp .idle
.right:
        mov eax, [cursor_x]
        cmp eax, TABLE_COLS - 1
        jge .idle
        inc dword [cursor_x]
        call draw_table
        jmp .idle

.detail:
        call show_detail
        call draw_table
        jmp .idle

.idle:
        mov eax, SYS_SLEEP
        mov ebx, 5
        int 0x80
        jmp .main_loop

.exit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; draw_table - Render the periodic table
;=======================================
draw_table:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        ; Title
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80

        ; Draw each cell of the table grid
        xor edi, edi            ; row
.dt_row:
        cmp edi, TABLE_ROWS
        jge .dt_info

        xor esi, esi            ; col
.dt_col:
        cmp esi, TABLE_COLS
        jge .dt_next_row

        ; Look up element at (row=edi, col=esi)
        mov eax, edi
        imul eax, TABLE_COLS
        add eax, esi
        movzx eax, byte [table_layout + eax]   ; element number (0=empty)
        test eax, eax
        jz .dt_empty

        ; We have an element
        push rax
        dec eax                 ; 0-indexed for symbol lookup

        ; Get category color
        push rax
        inc eax
        call get_category_color ; returns AH = color attr
        mov dl, ah
        pop rax

        ; Check if cursor is on this cell
        cmp esi, [cursor_x]
        jne .dt_not_sel
        cmp edi, [cursor_y]
        jne .dt_not_sel
        ; Selected: invert color
        mov dl, 0xF0            ; Black on white
.dt_not_sel:

        ; Position: col*4+1, row*2+2
        push rdx
        mov eax, SYS_SETCURSOR
        mov ebx, esi
        shl ebx, 2
        inc ebx
        mov ecx, edi
        shl ecx, 1
        add ecx, 2
        int 0x80
        pop rdx

        ; Set color
        mov eax, SYS_SETCOLOR
        movzx ebx, dl
        int 0x80

        ; Print symbol (2 chars)
        pop rax                 ; element number (1-based)
        dec eax
        shl eax, 1             ; *2 for 2-char symbols
        lea ebx, [symbols + eax]
        push rbx
        ; Print first char
        movzx ebx, byte [ebx]
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rbx
        movzx ebx, byte [ebx + 1]
        cmp bl, ' '
        je .dt_skip_2nd
        mov eax, SYS_PUTCHAR
        int 0x80
.dt_skip_2nd:

        jmp .dt_next_col

.dt_empty:
        ; Just skip

.dt_next_col:
        inc esi
        jmp .dt_col

.dt_next_row:
        inc edi
        jmp .dt_row

.dt_info:
        ; Show info about selected element at bottom
        mov eax, [cursor_y]
        imul eax, TABLE_COLS
        add eax, [cursor_x]
        movzx eax, byte [table_layout + eax]
        test eax, eax
        jz .dt_no_info

        push rax
        ; Display element info bar (row 21)
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 21
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        pop rax
        push rax
        ; Print: "Element #N: Xx"
        mov eax, SYS_PRINT
        mov ebx, lbl_elem
        int 0x80
        pop rax
        push rax
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, lbl_colon
        int 0x80
        pop rax
        dec eax
        shl eax, 1
        lea ebx, [symbols + eax]
        mov eax, SYS_PRINT
        int 0x80

.dt_no_info:
        ; Legend (row 23)
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, 23
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, legend_str
        int 0x80

        ; Status (row 24)
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 24
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, status_str
        int 0x80

        POPALL
        ret

;---------------------------------------
; get_category_color: EAX = element# (1-based) -> AH = color attribute
;---------------------------------------
get_category_color:
        push rbx
        dec eax
        cmp eax, 118
        jge .gcc_def
        movzx eax, byte [elem_category + eax]

        cmp al, CAT_ALKALI
        je .gcc_alkali
        cmp al, CAT_ALKALINE
        je .gcc_alkaline
        cmp al, CAT_TRANS
        je .gcc_trans
        cmp al, CAT_BASIC
        je .gcc_basic
        cmp al, CAT_SEMIMETAL
        je .gcc_semi
        cmp al, CAT_NONMETAL
        je .gcc_nonmetal
        cmp al, CAT_HALOGEN
        je .gcc_halogen
        cmp al, CAT_NOBLE
        je .gcc_noble
        cmp al, CAT_LANTHANIDE
        je .gcc_lanth
        cmp al, CAT_ACTINIDE
        je .gcc_actin
.gcc_def:
        mov ah, 0x07
        pop rbx
        ret
.gcc_alkali:    mov ah, 0x0C    ; Light red
                pop rbx
                ret
.gcc_alkaline:  mov ah, 0x0E    ; Yellow
                pop rbx
                ret
.gcc_trans:     mov ah, 0x0B    ; Light cyan
                pop rbx
                ret
.gcc_basic:     mov ah, 0x09    ; Light blue
                pop rbx
                ret
.gcc_semi:      mov ah, 0x0A    ; Light green
                pop rbx
                ret
.gcc_nonmetal:  mov ah, 0x0F    ; Bright white
                pop rbx
                ret
.gcc_halogen:   mov ah, 0x0D    ; Light magenta
                pop rbx
                ret
.gcc_noble:     mov ah, 0x03    ; Cyan
                pop rbx
                ret
.gcc_lanth:     mov ah, 0x06    ; Brown/orange
                pop rbx
                ret
.gcc_actin:     mov ah, 0x04    ; Red
                pop rbx
                ret

;---------------------------------------
; show_detail: popup detail for selected element
;---------------------------------------
show_detail:
        PUSHALL
        mov eax, [cursor_y]
        imul eax, TABLE_COLS
        add eax, [cursor_x]
        movzx eax, byte [table_layout + eax]
        test eax, eax
        jz .sd_done

        mov [tmp_elem], eax

        ; Draw a detail box
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F
        int 0x80

        ; Box at (20, 6) to (60, 18)
        mov ecx, 6
.sd_box:
        cmp ecx, 18
        jge .sd_content
        mov eax, SYS_SETCURSOR
        mov ebx, 20
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_line
        int 0x80
        inc ecx
        jmp .sd_box

.sd_content:
        ; Element number
        mov eax, SYS_SETCURSOR
        mov ebx, 22
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_number
        int 0x80
        mov eax, [tmp_elem]
        call print_dec

        ; Symbol
        mov eax, SYS_SETCURSOR
        mov ebx, 22
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_symbol
        int 0x80
        mov eax, [tmp_elem]
        dec eax
        shl eax, 1
        lea ebx, [symbols + eax]
        mov eax, SYS_PRINT
        int 0x80

        ; Name
        mov eax, SYS_SETCURSOR
        mov ebx, 22
        mov ecx, 12
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_name
        int 0x80
        ; Look up name
        mov eax, [tmp_elem]
        dec eax
        call get_name_ptr
        mov eax, SYS_PRINT
        int 0x80

        ; Press any key
        mov eax, SYS_SETCURSOR
        mov ebx, 24
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, press_any
        int 0x80

.sd_wait:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .sd_wait
.sd_done:
        POPALL
        ret

;---------------------------------------
; get_name_ptr: EAX=index(0-based) -> EBX=ptr to name string
;---------------------------------------
get_name_ptr:
        push rcx
        mov ebx, elem_names
        mov ecx, eax
        test ecx, ecx
        jz .gnp_done
.gnp_loop:
        cmp byte [ebx], 0
        jne .gnp_next
        dec ecx
        jz .gnp_found
.gnp_next:
        inc ebx
        jmp .gnp_loop
.gnp_found:
        inc ebx
.gnp_done:
        pop rcx
        ret

; === Layout Data ===
; Standard periodic table layout (18 columns x 9 rows)
; Each byte = element number (0=empty)
table_layout:
        ; Row 0: H ... He
        db 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2
        ; Row 1: Li Be ... B C N O F Ne
        db 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 6, 7, 8, 9, 10
        ; Row 2: Na Mg ... Al Si P S Cl Ar
        db 11,12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,13,14,15,16,17,18
        ; Row 3: K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn Ga Ge As Se Br Kr
        db 19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36
        ; Row 4: Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe
        db 37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54
        ; Row 5: Cs Ba La* Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn
        db 55,56,57,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86
        ; Row 6: Fr Ra Ac** Rf Db Sg Bh Hs Mt Ds Rg Cn Nh Fl Mc Lv Ts Og
        db 87,88,89,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118
        ; Row 7: Lanthanides (Ce-Lu)
        db 0, 0, 58,59,60,61,62,63,64,65,66,67,68,69,70,71, 0, 0
        ; Row 8: Actinides (Th-Lr)
        db 0, 0, 90,91,92,93,94,95,96,97,98,99,100,101,102,103, 0, 0

; Element symbols (2 chars each, space-padded)
symbols:
        db "H ", "He", "Li", "Be", "B ", "C ", "N ", "O ", "F ", "Ne"  ; 1-10
        db "Na", "Mg", "Al", "Si", "P ", "S ", "Cl", "Ar"              ; 11-18
        db "K ", "Ca", "Sc", "Ti", "V ", "Cr", "Mn", "Fe", "Co", "Ni" ; 19-28
        db "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr"             ; 29-36
        db "Rb", "Sr", "Y ", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd"; 37-46
        db "Ag", "Cd", "In", "Sn", "Sb", "Te", "I ", "Xe"             ; 47-54
        db "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd"; 55-64
        db "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu"                   ; 65-71
        db "Hf", "Ta", "W ", "Re", "Os", "Ir", "Pt", "Au", "Hg"       ; 72-80
        db "Tl", "Pb", "Bi", "Po", "At", "Rn"                          ; 81-86
        db "Fr", "Ra", "Ac", "Th", "Pa", "U ", "Np", "Pu", "Am", "Cm" ; 87-96
        db "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr"                   ; 97-103
        db "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "Cn"      ;104-112
        db "Nh", "Fl", "Mc", "Lv", "Ts", "Og"                          ;113-118

; Element categories (1 byte per element, 1-indexed: elem_category[0] = cat of element 1)
elem_category:
        ;  H  He
        db CAT_NONMETAL, CAT_NOBLE
        ;  Li Be B  C  N  O  F  Ne
        db CAT_ALKALI, CAT_ALKALINE, CAT_SEMIMETAL, CAT_NONMETAL, CAT_NONMETAL, CAT_NONMETAL, CAT_HALOGEN, CAT_NOBLE
        ;  Na Mg Al Si P  S  Cl Ar
        db CAT_ALKALI, CAT_ALKALINE, CAT_BASIC, CAT_SEMIMETAL, CAT_NONMETAL, CAT_NONMETAL, CAT_HALOGEN, CAT_NOBLE
        ;  K  Ca Sc-Zn  (transition metals)
        db CAT_ALKALI, CAT_ALKALINE
        db CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS
        ;  Ga Ge As Se Br Kr
        db CAT_BASIC, CAT_SEMIMETAL, CAT_SEMIMETAL, CAT_NONMETAL, CAT_HALOGEN, CAT_NOBLE
        ;  Rb Sr Y-Cd
        db CAT_ALKALI, CAT_ALKALINE
        db CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS
        ;  In Sn Sb Te I  Xe
        db CAT_BASIC, CAT_BASIC, CAT_SEMIMETAL, CAT_SEMIMETAL, CAT_HALOGEN, CAT_NOBLE
        ;  Cs Ba La Ce-Lu (lanthanides)
        db CAT_ALKALI, CAT_ALKALINE
        db CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE
        db CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE
        db CAT_LANTHANIDE, CAT_LANTHANIDE, CAT_LANTHANIDE
        ;  Hf-Hg  (transition metals)
        db CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS
        ;  Tl Pb Bi Po At Rn
        db CAT_BASIC, CAT_BASIC, CAT_BASIC, CAT_SEMIMETAL, CAT_HALOGEN, CAT_NOBLE
        ;  Fr Ra Ac Th-Lr (actinides)
        db CAT_ALKALI, CAT_ALKALINE
        db CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE
        db CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE
        db CAT_ACTINIDE, CAT_ACTINIDE, CAT_ACTINIDE
        ;  Rf-Og (104-118: transition/post-transition, simplify as trans)
        db CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS
        db CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS, CAT_TRANS
        db CAT_TRANS, CAT_HALOGEN, CAT_NOBLE

; Element names (null-separated)
elem_names:
        db "Hydrogen", 0, "Helium", 0, "Lithium", 0, "Beryllium", 0
        db "Boron", 0, "Carbon", 0, "Nitrogen", 0, "Oxygen", 0
        db "Fluorine", 0, "Neon", 0, "Sodium", 0, "Magnesium", 0
        db "Aluminium", 0, "Silicon", 0, "Phosphorus", 0, "Sulfur", 0
        db "Chlorine", 0, "Argon", 0, "Potassium", 0, "Calcium", 0
        db "Scandium", 0, "Titanium", 0, "Vanadium", 0, "Chromium", 0
        db "Manganese", 0, "Iron", 0, "Cobalt", 0, "Nickel", 0
        db "Copper", 0, "Zinc", 0, "Gallium", 0, "Germanium", 0
        db "Arsenic", 0, "Selenium", 0, "Bromine", 0, "Krypton", 0
        db "Rubidium", 0, "Strontium", 0, "Yttrium", 0, "Zirconium", 0
        db "Niobium", 0, "Molybdenum", 0, "Technetium", 0, "Ruthenium", 0
        db "Rhodium", 0, "Palladium", 0, "Silver", 0, "Cadmium", 0
        db "Indium", 0, "Tin", 0, "Antimony", 0, "Tellurium", 0
        db "Iodine", 0, "Xenon", 0, "Cesium", 0, "Barium", 0
        db "Lanthanum", 0, "Cerium", 0, "Praseodymium", 0, "Neodymium", 0
        db "Promethium", 0, "Samarium", 0, "Europium", 0, "Gadolinium", 0
        db "Terbium", 0, "Dysprosium", 0, "Holmium", 0, "Erbium", 0
        db "Thulium", 0, "Ytterbium", 0, "Lutetium", 0
        db "Hafnium", 0, "Tantalum", 0, "Tungsten", 0, "Rhenium", 0
        db "Osmium", 0, "Iridium", 0, "Platinum", 0, "Gold", 0
        db "Mercury", 0, "Thallium", 0, "Lead", 0, "Bismuth", 0
        db "Polonium", 0, "Astatine", 0, "Radon", 0
        db "Francium", 0, "Radium", 0, "Actinium", 0, "Thorium", 0
        db "Protactinium", 0, "Uranium", 0, "Neptunium", 0, "Plutonium", 0
        db "Americium", 0, "Curium", 0, "Berkelium", 0, "Californium", 0
        db "Einsteinium", 0, "Fermium", 0, "Mendelevium", 0, "Nobelium", 0
        db "Lawrencium", 0
        db "Rutherfordium", 0, "Dubnium", 0, "Seaborgium", 0, "Bohrium", 0
        db "Hassium", 0, "Meitnerium", 0, "Darmstadtium", 0, "Roentgenium", 0
        db "Copernicium", 0, "Nihonium", 0, "Flerovium", 0, "Moscovium", 0
        db "Livermorium", 0, "Tennessine", 0, "Oganesson", 0

; Strings
title_str:      db " Periodic Table of the Elements                   Arrows:Move Enter:Detail", 0
lbl_elem:       db "Element #", 0
lbl_colon:      db ": ", 0
lbl_number:     db "Atomic Number: ", 0
lbl_symbol:     db "Symbol: ", 0
lbl_name:       db "Name: ", 0
box_line:       db "                                        ", 0
press_any:      db "Press any key...", 0
legend_str:     db "  Alkali  Alkaline  Transition  Post-Trans  Semimetal  Nonmetal  Halogen  Noble", 0
status_str:     db " Periodic Table v1.0  |  118 Elements  |  q=Quit                              ", 0

; BSS
cursor_x:       dd 0
cursor_y:       dd 0
tmp_elem:       dd 0

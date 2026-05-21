; chip8.asm — CHIP-8 emulator for Mellivora OS
;
; Emulates the original 1977 Cosmac VIP CHIP-8 instruction set.
;
; Usage:  chip8 [romfile.ch8]
;   If no file is given, a built-in digits demo plays.
;
; Controls (classic CHIP-8 hex keypad → PC keyboard):
;   1 2 3 4   →  0x1 0x2 0x3 0xC
;   Q W E R   →  0x4 0x5 0x6 0xD
;   A S D F   →  0x7 0x8 0x9 0xE
;   Z X C V   →  0xA 0x0 0xB 0xF
;   ESC       →  quit
;
; Display: 64×32 CHIP-8 pixels, scaled 8× → 512×256, centred on 640×480.

%include "syscalls.inc"

; ---- Screen ----------------------------------------------------------
SCR_W           equ 640
SCR_H           equ 480
C8_W            equ 64
C8_H            equ 32
SCALE           equ 8                           ; 64*8=512, 32*8=256
BOARD_X         equ (SCR_W - C8_W * SCALE) / 2 ; 64
BOARD_Y         equ (SCR_H - C8_H * SCALE) / 2 ; 112
BORDER          equ 2
COL_ON          equ 0x00FF88    ; lit pixel
COL_OFF         equ 0x001A1A    ; dark pixel
COL_BORDER      equ 0x444466
COL_BG          equ 0x000000

; ---- CHIP-8 constants ------------------------------------------------
C8_FONT_OFF     equ 0x000       ; built-in font at 0x000..0x04F
C8_ROM_BASE     equ 0x200       ; ROMs start here
C8_MEM_SZ       equ 4096
C8_STACK_DEPTH  equ 16
CYCLES_PER_TICK equ 12          ; opcodes per ~16ms frame

start:
        mov eax, SYS_TASKNAME
        mov ebx, .tname
        int 0x80

        ; Activate VBE 640×480×32
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .done

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr],  eax
        mov dword [fb_pitch], SCR_W * 4

        ; Seed RNG
        mov eax, SYS_GETTIME
        int 0x80
        mov [rng_st], eax

        ; Init CHIP-8 state (font, registers, screen)
        call c8_init

        ; Try to load a ROM from the command-line argument
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .load_builtin

        mov esi, arg_buf
.skip_sp:
        cmp byte [esi], ' '
        jne .try_load
        inc esi
        jmp .skip_sp
.try_load:
        cmp byte [esi], 0
        je .load_builtin

        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, c8_mem + C8_ROM_BASE
        int 0x80
        cmp eax, 1
        jge .rom_loaded

.load_builtin:
        ; Copy built-in test ROM into CHIP-8 memory at 0x200
        mov esi, builtin_rom
        mov edi, c8_mem + C8_ROM_BASE
        mov ecx, builtin_rom_sz
        rep movsb

.rom_loaded:
        ; Draw background + border once
        call draw_bg

.mainloop:
        ; Execute a batch of opcodes
        mov dword [cycle_ct], CYCLES_PER_TICK
.cyc:
        call c8_step
        cmp eax, -1
        je .quit
        dec dword [cycle_ct]
        jnz .cyc

        ; Decrement 60 Hz timers (we run ~60fps via 1-tick sleep)
        cmp byte [c8_delay], 0
        je .no_dt
        dec byte [c8_delay]
.no_dt:
        cmp byte [c8_sound], 0
        je .no_st
        dec byte [c8_sound]
.no_st:

        ; Redraw if DRW opcode set the flag
        cmp byte [draw_flag], 0
        je .no_draw
        call render_display
        mov byte [draw_flag], 0
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
.no_draw:

        ; Handle one keystroke (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        cmp eax, -1
        je .no_key
        cmp al, 27             ; ESC → quit
        je .quit
        call map_key           ; EAX=char → EAX=chip8 key (0..F) or -1
        cmp eax, -1
        je .no_key
        ; Store: key_last = the chip8 key; mark it pressed for 1 frame
        mov [key_last], al
        mov byte [key_ready], 1
        mov ebx, eax
        mov byte [c8_keys + ebx], 1
.no_key:

        ; Clear key state after one frame
        xor eax, eax
        mov ecx, 16
        mov edi, c8_keys
        rep stosb

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        jmp .mainloop

.quit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.tname: db "chip8", 0

;=======================================================================
; c8_init  —  reset all CHIP-8 state and install built-in font
;=======================================================================
c8_init:
        pushad
        ; Zero registers V0-VF
        mov edi, c8_V
        xor eax, eax
        mov ecx, 16
        rep stosb

        mov word [c8_I],     0
        mov word [c8_PC],    C8_ROM_BASE
        mov byte [c8_SP],    0
        mov byte [c8_delay], 0
        mov byte [c8_sound], 0
        mov byte [draw_flag],1
        mov byte [key_ready],0

        ; Zero screen
        mov edi, c8_scr
        xor eax, eax
        mov ecx, C8_W * C8_H / 4
        rep stosd

        ; Zero stack
        mov edi, c8_stack
        mov ecx, C8_STACK_DEPTH / 2
        rep stosd

        ; Zero keys
        mov edi, c8_keys
        mov ecx, 4
        rep stosd

        ; Zero memory, then copy font to 0x000
        mov edi, c8_mem
        mov ecx, C8_MEM_SZ / 4
        rep stosd

        mov esi, c8_font_data
        mov edi, c8_mem + C8_FONT_OFF
        mov ecx, 80             ; 16 chars × 5 bytes
        rep movsb

        popad
        ret

;=======================================================================
; c8_step  —  fetch, decode, execute one CHIP-8 opcode
; Returns EAX = 0 (normal)  or  -1 (halt / error)
;=======================================================================
c8_step:
        ; Fetch opcode (big-endian 16-bit)
        movzx ebx, word [c8_PC]
        cmp bx, C8_MEM_SZ - 1
        jae .halt
        movzx eax, byte [c8_mem + ebx]
        movzx ecx, byte [c8_mem + ebx + 1]
        shl eax, 8
        or  eax, ecx            ; EAX = full opcode
        add word [c8_PC], 2

        ; Extract nibbles  nib0=top, nib1, nib2, nib3=bottom
        mov [op_full], ax
        mov ebx, eax
        shr ebx, 12
        and ebx, 0xF            ; top nibble
        jmp [.dtab + ebx*4]

.dtab:  dd .n0, .n1, .n2, .n3, .n4, .n5, .n6, .n7
        dd .n8, .n9, .nA, .nB, .nC, .nD, .nE, .nF

; ------ 0x00E0 CLS  /  0x00EE RET  -----------------------------------
.n0:
        movzx eax, word [op_full]
        cmp ax, 0x00E0
        jne .n0_ret
        ; CLS
        mov edi, c8_scr
        xor eax, eax
        mov ecx, C8_W * C8_H / 4
        rep stosd
        mov byte [draw_flag], 1
        xor eax, eax
        ret
.n0_ret:
        cmp ax, 0x00EE
        jne .ok
        ; RET
        movzx eax, byte [c8_SP]
        test eax, eax
        jz .halt
        dec byte [c8_SP]
        movzx eax, byte [c8_SP]
        movzx eax, word [c8_stack + eax*2]
        mov [c8_PC], ax
        xor eax, eax
        ret

; ------ 0x1nnn JP nnn -------------------------------------------------
.n1:
        movzx eax, word [op_full]
        and eax, 0x0FFF
        mov [c8_PC], ax
        xor eax, eax
        ret

; ------ 0x2nnn CALL nnn -----------------------------------------------
.n2:
        movzx eax, byte [c8_SP]
        cmp eax, C8_STACK_DEPTH - 1
        jge .halt               ; stack overflow
        movzx ecx, word [c8_PC]
        mov [c8_stack + eax*2], cx
        inc byte [c8_SP]
        movzx eax, word [op_full]
        and eax, 0x0FFF
        mov [c8_PC], ax
        xor eax, eax
        ret

; ------ 0x3xkk SE Vx, kk ---------------------------------------------
.n3:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF
        movzx ecx, byte [c8_V + ebx]
        and eax, 0xFF
        cmp ecx, eax
        jne .ok
        add word [c8_PC], 2
        jmp .ok

; ------ 0x4xkk SNE Vx, kk -------------------------------------------
.n4:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF
        movzx ecx, byte [c8_V + ebx]
        and eax, 0xFF
        cmp ecx, eax
        je .ok
        add word [c8_PC], 2
        jmp .ok

; ------ 0x5xy0 SE Vx, Vy ---------------------------------------------
.n5:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF
        mov ecx, eax
        shr ecx, 4
        and ecx, 0xF
        movzx edx, byte [c8_V + ebx]
        movzx esi, byte [c8_V + ecx]
        cmp edx, esi
        jne .ok
        add word [c8_PC], 2
        jmp .ok

; ------ 0x6xkk LD Vx, kk ---------------------------------------------
.n6:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF
        and eax, 0xFF
        mov [c8_V + ebx], al
        jmp .ok

; ------ 0x7xkk ADD Vx, kk (no carry) ---------------------------------
.n7:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF
        and eax, 0xFF
        add [c8_V + ebx], al
        jmp .ok

; ------ 0x8xy? arithmetic / logic ------------------------------------
.n8:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF            ; x
        mov ecx, eax
        shr ecx, 4
        and ecx, 0xF            ; y
        and eax, 0xF            ; sub-op
        movzx esi, byte [c8_V + ebx]   ; Vx value
        movzx edi, byte [c8_V + ecx]   ; Vy value
        cmp eax, 0 ; LD
        je .a8_ld
        cmp eax, 1 ; OR
        je .a8_or
        cmp eax, 2 ; AND
        je .a8_and
        cmp eax, 3 ; XOR
        je .a8_xor
        cmp eax, 4 ; ADD (carry)
        je .a8_add
        cmp eax, 5 ; SUB (borrow)
        je .a8_sub
        cmp eax, 6 ; SHR
        je .a8_shr
        cmp eax, 7 ; SUBN
        je .a8_subn
        cmp eax, 0xE ; SHL
        je .a8_shl
        jmp .ok
.a8_ld:  mov eax, edi           ; Vx = Vy (edi = Vy value)
         mov [c8_V + ebx], al
         jmp .ok
.a8_or:  or esi, edi            ; Vx |= Vy
         and esi, 0xFF
         mov eax, esi
         mov [c8_V + ebx], al
         jmp .ok
.a8_and: and esi, edi
         and esi, 0xFF
         mov eax, esi
         mov [c8_V + ebx], al
         jmp .ok
.a8_xor: xor esi, edi
         and esi, 0xFF
         mov eax, esi
         mov [c8_V + ebx], al
         jmp .ok
.a8_add: add esi, edi
         mov eax, esi
         and eax, 0xFF
         mov [c8_V + ebx], al        ; low 8 bits
         mov byte [c8_V + 0xF], 0
         cmp esi, 256
         jb .ok
         mov byte [c8_V + 0xF], 1
         jmp .ok
.a8_sub: ; VF = 1 if Vx >= Vy (no borrow)
         mov byte [c8_V + 0xF], 0
         cmp esi, edi
         jb .a8_sub_nb
         mov byte [c8_V + 0xF], 1
.a8_sub_nb:
         sub esi, edi
         and esi, 0xFF
         mov eax, esi
         mov [c8_V + ebx], al
         jmp .ok
.a8_shr: ; VF = old LSB of Vx
         mov ecx, esi
         and ecx, 1
         mov [c8_V + 0xF], cl
         shr esi, 1
         and esi, 0xFF
         mov eax, esi
         mov [c8_V + ebx], al
         jmp .ok
.a8_subn: ; Vx = Vy - Vx,  VF = 1 if Vy >= Vx
         mov byte [c8_V + 0xF], 0
         cmp edi, esi
         jb .a8_subn_nb
         mov byte [c8_V + 0xF], 1
.a8_subn_nb:
         sub edi, esi
         and edi, 0xFF
         mov eax, edi
         mov [c8_V + ebx], al
         jmp .ok
.a8_shl: ; VF = old MSB (bit 7) of Vx
         mov ecx, esi
         shr ecx, 7
         and ecx, 1
         mov [c8_V + 0xF], cl
         shl esi, 1
         and esi, 0xFF
         mov eax, esi
         mov [c8_V + ebx], al
         jmp .ok

; ------ 0x9xy0 SNE Vx, Vy --------------------------------------------
.n9:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF
        mov ecx, eax
        shr ecx, 4
        and ecx, 0xF
        movzx edx, byte [c8_V + ebx]
        movzx esi, byte [c8_V + ecx]
        cmp edx, esi
        je .ok
        add word [c8_PC], 2
        jmp .ok

; ------ 0xAnnn LD I, nnn ---------------------------------------------
.nA:
        movzx eax, word [op_full]
        and eax, 0x0FFF
        mov [c8_I], ax
        jmp .ok

; ------ 0xBnnn JP V0, nnn -------------------------------------------
.nB:
        movzx eax, word [op_full]
        and eax, 0x0FFF
        movzx ecx, byte [c8_V]          ; V0
        add eax, ecx
        and eax, 0x0FFF
        mov [c8_PC], ax
        jmp .ok

; ------ 0xCxkk RND Vx, byte -----------------------------------------
.nC:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF            ; x
        and eax, 0xFF           ; mask kk
        push eax
        push ebx
        call rng_next           ; → EAX = random byte
        pop ebx
        pop ecx                 ; kk
        and eax, ecx
        mov [c8_V + ebx], al
        jmp .ok

; ------ 0xDxyn DRW Vx, Vy, nibble ------------------------------------
.nD:
        pushad
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF                    ; x-reg index
        movzx esi, byte [c8_V + ebx]   ; Vx = screen X
        and esi, 63                     ; wrap

        mov ebx, eax
        shr ebx, 4
        and ebx, 0xF                    ; y-reg index
        movzx edi, byte [c8_V + ebx]   ; Vy = screen Y
        and edi, 31                     ; wrap

        and eax, 0xF                    ; n = sprite height
        mov [drw_n], eax

        movzx edx, word [c8_I]          ; I = sprite address
        mov byte [c8_V + 0xF], 0        ; clear collision flag

        xor ecx, ecx                    ; row = 0
.drw_row:
        cmp ecx, [drw_n]
        jge .drw_done

        movzx ebx, byte [c8_mem + edx + ecx]  ; sprite byte for this row

        ; screen_y = (Vy + row) & 31
        mov eax, edi
        add eax, ecx
        and eax, 31
        imul eax, C8_W                  ; offset into c8_scr
        mov [drw_y_off], eax

        ; iterate over 8 bits in the sprite byte
        mov [drw_row_bits], ebx
        xor ebx, ebx                    ; col = 0
.drw_col:
        cmp ebx, 8
        jge .drw_next_row

        ; get bit (7-col) of sprite byte
        mov eax, [drw_row_bits]
        mov ecx, 7
        sub ecx, ebx
        shr eax, cl
        and eax, 1
        jz .drw_skip_pixel              ; bit is 0

        ; screen_x = (Vx + col) & 63
        mov ecx, esi
        add ecx, ebx
        and ecx, 63

        ; screen index
        mov eax, [drw_y_off]
        add eax, ecx                    ; row_offset + col

        ; XOR pixel
        xor byte [c8_scr + eax], 1
        ; if result = 0, collision
        cmp byte [c8_scr + eax], 0
        jne .drw_skip_pixel
        mov byte [c8_V + 0xF], 1

.drw_skip_pixel:
        inc ebx
        jmp .drw_col
.drw_next_row:
        inc ecx
        jmp .drw_row
.drw_done:
        mov byte [draw_flag], 1
        popad
        jmp .ok

; ------ 0xEx9E / 0xExA1 skip if key pressed / not pressed -----------
.nE:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF
        movzx ecx, byte [c8_V + ebx]   ; key number
        and ecx, 0xF
        and eax, 0xFF
        cmp eax, 0x9E                   ; SKP Vx
        je .ne_skp
        cmp eax, 0xA1                   ; SKNP Vx
        je .ne_sknp
        jmp .ok
.ne_skp:
        cmp byte [c8_keys + ecx], 1
        jne .ok
        add word [c8_PC], 2
        jmp .ok
.ne_sknp:
        cmp byte [c8_keys + ecx], 0
        jne .ok
        add word [c8_PC], 2
        jmp .ok

; ------ 0xFx?? misc --------------------------------------------------
.nF:
        movzx eax, word [op_full]
        mov ebx, eax
        shr ebx, 8
        and ebx, 0xF            ; x
        and eax, 0xFF           ; sub-op byte

        cmp eax, 0x07           ; LD Vx, DT
        je .nf_getdt
        cmp eax, 0x0A           ; LD Vx, K  (wait for key)
        je .nf_waitk
        cmp eax, 0x15           ; LD DT, Vx
        je .nf_setdt
        cmp eax, 0x18           ; LD ST, Vx
        je .nf_setst
        cmp eax, 0x1E           ; ADD I, Vx
        je .nf_addi
        cmp eax, 0x29           ; LD F, Vx (set I to font)
        je .nf_font
        cmp eax, 0x33           ; LD B, Vx (BCD)
        je .nf_bcd
        cmp eax, 0x55           ; LD [I], V0..Vx
        je .nf_store
        cmp eax, 0x65           ; LD V0..Vx, [I]
        je .nf_load
        jmp .ok

.nf_getdt:
        movzx eax, byte [c8_delay]
        mov [c8_V + ebx], al
        jmp .ok
.nf_waitk:
        ; If a key was pressed this frame, consume it
        cmp byte [key_ready], 0
        jz .wait_block
        mov byte [key_ready], 0
        movzx eax, byte [key_last]
        mov [c8_V + ebx], al
        jmp .ok
.wait_block:
        ; Re-execute this opcode next cycle
        sub word [c8_PC], 2
        jmp .ok
.nf_setdt:
        movzx eax, byte [c8_V + ebx]
        mov [c8_delay], al
        jmp .ok
.nf_setst:
        movzx eax, byte [c8_V + ebx]
        mov [c8_sound], al
        jmp .ok
.nf_addi:
        movzx eax, byte [c8_V + ebx]
        movzx ecx, word [c8_I]
        add ecx, eax
        mov [c8_I], cx
        jmp .ok
.nf_font:
        movzx eax, byte [c8_V + ebx]
        and eax, 0xF            ; 0..15
        imul eax, 5             ; each font char = 5 bytes
        add eax, C8_FONT_OFF
        mov [c8_I], ax
        jmp .ok
.nf_bcd:
        movzx eax, byte [c8_V + ebx]   ; 0..255
        movzx edi, word [c8_I]
        ; hundreds
        xor edx, edx
        mov ecx, 100
        div ecx
        mov [c8_mem + edi], al
        ; tens
        mov eax, edx
        xor edx, edx
        mov ecx, 10
        div ecx
        mov [c8_mem + edi + 1], al
        ; ones
        mov [c8_mem + edi + 2], dl
        jmp .ok
.nf_store:
        ; Store V0..Vx in memory starting at I
        movzx edi, word [c8_I]
        xor ecx, ecx
.nfs_lp:
        cmp ecx, ebx
        jg .ok
        movzx eax, byte [c8_V + ecx]
        mov [c8_mem + edi + ecx], al
        inc ecx
        jmp .nfs_lp
.nf_load:
        ; Load V0..Vx from memory starting at I
        movzx edi, word [c8_I]
        xor ecx, ecx
.nfl_lp:
        cmp ecx, ebx
        jg .ok
        movzx eax, byte [c8_mem + edi + ecx]
        mov [c8_V + ecx], al
        inc ecx
        jmp .nfl_lp

.halt:
        mov eax, -1
        ret
.ok:
        xor eax, eax
        ret

;=======================================================================
; render_display — blit c8_scr to framebuffer (SCALE × blocks)
;=======================================================================
render_display:
        pushad
        xor edx, edx            ; row
.rd_row:
        cmp edx, C8_H
        jge .rd_done
        xor ecx, ecx            ; col
.rd_col:
        cmp ecx, C8_W
        jge .rd_next_row

        ; pixel state
        mov eax, edx
        imul eax, C8_W
        add eax, ecx
        movzx eax, byte [c8_scr + eax]
        test eax, eax
        jz .rd_off
        mov edi, COL_ON
        jmp .rd_blit
.rd_off:
        mov edi, COL_OFF

.rd_blit:
        ; fill SCALE×SCALE block at (BOARD_X + col*SCALE, BOARD_Y + row*SCALE)
        push ecx
        push edx
        mov ebx, ecx
        imul ebx, SCALE
        add ebx, BOARD_X
        mov ecx, edx
        imul ecx, SCALE
        add ecx, BOARD_Y
        mov edx, SCALE
        mov esi, SCALE
        call fb_fill_rect
        pop edx
        pop ecx

        inc ecx
        jmp .rd_col
.rd_next_row:
        inc edx
        jmp .rd_row
.rd_done:
        popad
        ret

;=======================================================================
; draw_bg — fill background and border
;=======================================================================
draw_bg:
        pushad
        ; Full screen black
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCR_W
        mov esi, SCR_H
        mov edi, COL_BG
        call fb_fill_rect
        ; Border rectangle
        mov ebx, BOARD_X - BORDER
        mov ecx, BOARD_Y - BORDER
        mov edx, C8_W * SCALE + BORDER * 2
        mov esi, C8_H * SCALE + BORDER * 2
        mov edi, COL_BORDER
        call fb_fill_rect
        ; Status text
        mov ebx, BOARD_X
        mov ecx, BOARD_Y + C8_H * SCALE + 6
        mov esi, str_ctrl
        mov edi, 0x888888
        call fb_draw_text
        popad
        ret

;=======================================================================
; map_key  — translate ASCII → CHIP-8 key (0..F)
; Input: EAX = ASCII char     Output: EAX = key index or -1
;=======================================================================
map_key:
        push ebx
        lea ebx, [key_map]
.mk_loop:
        movzx ecx, byte [ebx]           ; ASCII char
        test ecx, ecx
        jz .mk_none
        cmp ecx, eax
        jne .mk_next
        movzx eax, byte [ebx + 1]       ; chip8 key
        pop ebx
        ret
.mk_next:
        add ebx, 2
        jmp .mk_loop
.mk_none:
        mov eax, -1
        pop ebx
        ret

;=======================================================================
; rng_next  — simple 32-bit LCG, returns low 8 bits in EAX
;=======================================================================
rng_next:
        mov eax, [rng_st]
        imul eax, 1664525
        add eax, 1013904223
        mov [rng_st], eax
        and eax, 0xFF
        ret

;=======================================================================
; fb_fill_rect  —  EBX=x ECX=y EDX=w ESI=h EDI=colour
;=======================================================================
fb_fill_rect:
        pushad
        test edx, edx
        jz .done
        test esi, esi
        jz .done
        mov eax, ecx
        imul eax, [fb_pitch]
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]
.row:
        push eax
        push edx
        mov ecx, edx
.col:
        mov [eax], edi
        add eax, 4
        dec ecx
        jnz .col
        pop edx
        pop eax
        add eax, [fb_pitch]
        dec esi
        jnz .row
.done:
        popad
        ret

;=======================================================================
; fb_draw_text  —  EBX=x ECX=y ESI=str_ptr EDI=colour
;=======================================================================
fb_draw_text:
        pushad
        mov edx, ecx
        mov ecx, ebx
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        int 0x80
        popad
        ret

;=======================================================================
; install_test_rom  —  copy built-in ROM bytes into c8_mem
;=======================================================================
install_test_rom:
        mov esi, builtin_rom
        mov edi, c8_mem + C8_ROM_BASE
        mov ecx, builtin_rom_sz
        rep movsb
        ret

;=======================================================================
; DATA
;=======================================================================
; Built-in font data: 16 chars × 5 bytes (bit-packed, high 4 bits used)
c8_font_data:
        db 0xF0,0x90,0x90,0x90,0xF0 ; 0
        db 0x20,0x60,0x20,0x20,0x70 ; 1
        db 0xF0,0x10,0xF0,0x80,0xF0 ; 2
        db 0xF0,0x10,0xF0,0x10,0xF0 ; 3
        db 0x90,0x90,0xF0,0x10,0x10 ; 4
        db 0xF0,0x80,0xF0,0x10,0xF0 ; 5
        db 0xF0,0x80,0xF0,0x90,0xF0 ; 6
        db 0xF0,0x10,0x20,0x40,0x40 ; 7
        db 0xF0,0x90,0xF0,0x90,0xF0 ; 8
        db 0xF0,0x90,0xF0,0x10,0xF0 ; 9
        db 0xF0,0x90,0xF0,0x90,0x90 ; A
        db 0xE0,0x90,0xE0,0x90,0xE0 ; B
        db 0xF0,0x80,0x80,0x80,0xF0 ; C
        db 0xE0,0x90,0x90,0x90,0xE0 ; D
        db 0xF0,0x80,0xF0,0x80,0xF0 ; E
        db 0xF0,0x80,0xF0,0x80,0x80 ; F

; Key map: pairs of (ascii, chip8_key), terminated by 0
key_map:
        db '1',0x1, '2',0x2, '3',0x3, '4',0xC
        db 'q',0x4, 'w',0x5, 'e',0x6, 'r',0xD
        db 'Q',0x4, 'W',0x5, 'E',0x6, 'R',0xD
        db 'a',0x7, 's',0x8, 'd',0x9, 'f',0xE
        db 'A',0x7, 'S',0x8, 'D',0x9, 'F',0xE
        db 'z',0xA, 'x',0x0, 'c',0xB, 'v',0xF
        db 'Z',0xA, 'X',0x0, 'C',0xB, 'V',0xF
        db 0                    ; terminator

; Built-in demo ROM: draws digits 1-5 using the font, then halts
; Each opcode is 2 bytes, big-endian.
builtin_rom:
        ; LD V0, 1         (first digit to display)
        db 0x60, 0x01
        ; LD V1, 2         (x position)
        db 0x61, 0x02
        ; LD V2, 10        (y position)
        db 0x62, 0x0A
        ; LD F, V0         (I = font sprite for V0)
        db 0xF0, 0x29
        ; DRW V1, V2, 5   (draw 5-row sprite)
        db 0xD1, 0x25
        ; ADD V1, 10       (advance x by 10 pixels)
        db 0x71, 0x0A
        ; ADD V0, 1        (next digit)
        db 0x70, 0x01
        ; SE V0, 6         (skip if we've drawn 0..5)
        db 0x30, 0x06
        ; JP 0x206         (loop back to LD F)
        db 0x12, 0x06
        ; Spin-halt: JP self
        db 0x12, 0x12
builtin_rom_sz equ $ - builtin_rom

str_ctrl: db "1-4/Q-R/A-F/Z-V = keys  |  ESC = quit", 0
err_load: db "chip8: error loading ROM, using demo", 0x0A, 0

;=======================================================================
; BSS
;=======================================================================
section .bss
; Framebuffer
fb_addr:        resd 1
fb_pitch:       resd 1

; RNG state
rng_st:         resd 1

; Decode temporaries
op_full:        resw 1
drw_n:          resd 1
drw_y_off:      resd 1
drw_row_bits:   resd 1

; Loop counter
cycle_ct:       resd 1

; Key state
key_last:       resb 1
key_ready:      resb 1

; Draw request flag
draw_flag:      resb 1

; Command-line arg buffer
arg_buf:        resb 128

; CHIP-8 state
c8_mem:         resb C8_MEM_SZ          ; 4 KB address space
c8_V:           resb 16                 ; V0..VF registers
c8_I:           resw 1                  ; I register
c8_PC:          resw 1                  ; program counter
c8_SP:          resb 1                  ; stack pointer (0..15)
c8_delay:       resb 1                  ; delay timer
c8_sound:       resb 1                  ; sound timer
c8_stack:       resw C8_STACK_DEPTH     ; return address stack
c8_keys:        resb 16                 ; key pressed state (0/1)
c8_scr:         resb C8_W * C8_H       ; 64×32 pixel display

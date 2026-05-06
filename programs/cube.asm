; cube.asm — Rotating 3D wireframe cube for Mellivora OS
;
; Renders a perspective-projected unit cube rotating around Y and X axes.
; Uses 16.16 fixed-point sine/cosine tables (256 entries = full circle).
; Draws edges with SYS_DRAW_LINE into the shadow buffer, then flips.
; Press any key to exit.

%include "syscalls.inc"

SCR_W    equ 640
SCR_H    equ 480
PITCH    equ SCR_W * 4
SCR_CX   equ SCR_W / 2         ; screen centre X
SCR_CY   equ SCR_H / 2         ; screen centre Y
SCALE    equ 180                ; projection scale (pixels per unit after /D)
PERSP    equ 3                  ; perspective distance (in cube units)
PERSP_FP equ PERSP * 65536      ; 3.0 in 16.16

; Cube vertex coords in 16.16 (±1.0 = ±65536)
VONE     equ 65536

start:
        ; ---- VBE mode ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .novbe

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb], eax

        ; Init rotation angles
        mov dword [rot_y], 0       ; yaw (Y-axis)
        mov dword [rot_x], 43      ; slight initial pitch

.frame:
        ; Key check (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; ---- Project all 8 vertices ----
        ; Load rotation sin/cos
        movzx eax, byte [rot_y]
        mov ecx, [sin_fp + eax * 4]
        mov [sy], ecx           ; sin(ay)
        mov ecx, [cos_fp + eax * 4]
        mov [cy_r], ecx         ; cos(ay)

        movzx eax, byte [rot_x]
        mov ecx, [sin_fp + eax * 4]
        mov [sx], ecx           ; sin(ax)
        mov ecx, [cos_fp + eax * 4]
        mov [cx_r], ecx         ; cos(ax)

        ; Project each vertex
        mov esi, vert_src       ; source vertices
        mov edi, vert_proj      ; projected screen (x,y) pairs
        mov ecx, 8
.proj_loop:
        push ecx
        ; Read world vertex (vx, vy, vz) from vert_src
        mov eax, [esi]          ; vx (16.16)
        mov ebx, [esi+4]        ; vy
        mov edx, [esi+8]        ; vz

        ; Rotate around Y axis:
        ; rx = vx*cos(ay) - vz*sin(ay)
        ; rz = vx*sin(ay) + vz*cos(ay)
        push eax
        push ebx
        push edx

        ; rx = vx*cy - vz*sy
        mov eax, [esp+8]        ; vx
        imul dword [cy_r]
        shrd eax, edx, 16
        mov [tmp0], eax         ; rx (partial)

        mov eax, [esp]          ; vz
        imul dword [sy]
        shrd eax, edx, 16
        sub [tmp0], eax         ; rx = vx*cy - vz*sy

        ; rz = vx*sy + vz*cy
        mov eax, [esp+8]        ; vx
        imul dword [sy]
        shrd eax, edx, 16
        mov [tmp1], eax

        mov eax, [esp]          ; vz
        imul dword [cy_r]
        shrd eax, edx, 16
        add [tmp1], eax         ; rz = vx*sy + vz*cy

        ; ry = vy (Y-rotation doesn't touch Y)
        mov eax, [esp+4]
        mov [tmp2], eax         ; ry = vy

        pop edx                 ; cleanup stack
        pop ebx
        pop eax

        ; Rotate around X axis:
        ; final_y  = ry*cos(ax) - rz*sin(ax)
        ; final_z  = ry*sin(ax) + rz*cos(ax)
        ; final_x  = rx

        mov eax, [tmp2]         ; ry
        imul dword [cx_r]
        shrd eax, edx, 16
        mov [tmp3], eax

        mov eax, [tmp1]         ; rz
        imul dword [sx]
        shrd eax, edx, 16
        sub [tmp3], eax         ; final_y = ry*cx - rz*sx

        mov eax, [tmp2]         ; ry
        imul dword [sx]
        shrd eax, edx, 16
        mov [tmp4], eax

        mov eax, [tmp1]         ; rz
        imul dword [cx_r]
        shrd eax, edx, 16
        add [tmp4], eax         ; final_z = ry*sx + rz*cx

        ; final_x = rx = [tmp0]

        ; Perspective divide: screen_x = final_x / (final_z + D) * SCALE + CX
        ; denom = final_z + D_FP
        mov eax, [tmp4]         ; final_z
        add eax, PERSP_FP       ; + 3.0

        ; Clamp denom to avoid division by zero
        cmp eax, 32768          ; 0.5 in 16.16
        jge .denom_ok
        mov eax, 32768
.denom_ok:
        mov [tmp5], eax         ; denom

        ; screen_x = (final_x * SCALE) / denom + CX
        ; All in 16.16: final_x is 16.16, denom is 16.16, result should be pixels
        ; sx_fp = (final_x << 16) / denom  → 16.16 screen coord / SCALE
        ; Simpler: screen_x = final_x * SCALE / denom  (integers after >>16 twice)
        mov eax, [tmp0]         ; final_x (16.16)
        imul eax, SCALE
        cdq
        idiv dword [tmp5]       ; → pixels in 16.16 space (numerator was 16.16 * int, denom 16.16 → int)
        sar eax, 0              ; already in pixels after imul SCALE / denom(16.16) ≈ pixels
        add eax, SCR_CX
        mov [edi], eax          ; store screen_x

        mov eax, [tmp3]         ; final_y (16.16)
        imul eax, SCALE
        cdq
        idiv dword [tmp5]
        neg eax                 ; Y axis: positive Y is down on screen
        add eax, SCR_CY
        mov [edi+4], eax        ; store screen_y

        add esi, 12             ; next source vertex (3 dwords)
        add edi, 8              ; next projected vertex (2 dwords)
        pop ecx
        dec ecx
        jnz .proj_loop

        ; ---- Clear shadow buffer to black ----
        mov edi, [fb]
        xor eax, eax
        mov ecx, SCR_W * SCR_H
        rep stosd

        ; ---- Draw 12 edges ----
        mov esi, edges
        mov ecx, 12
.edge_loop:
        push ecx
        movzx eax, byte [esi]   ; vertex A index
        shl eax, 3              ; × 8 (2 dwords per projected vertex)
        mov ebx, [vert_proj + eax]      ; x0
        mov ecx, [vert_proj + eax + 4]  ; y0

        movzx eax, byte [esi+1] ; vertex B index
        shl eax, 3
        mov edx, [vert_proj + eax]      ; x1
        mov edi, [vert_proj + eax + 4]  ; y1

        ; draw line: SYS_DRAW_LINE — EBX=x0 ECX=y0 EDX=x1 ESI=y1 EDI=color
        push esi                ; save edge pointer
        mov esi, edi            ; y1
        mov edi, 0x00FFFFFF     ; white
        mov eax, SYS_DRAW_LINE
        int 0x80
        pop esi

        add esi, 2              ; next edge pair
        pop ecx
        dec ecx
        jnz .edge_loop

        ; ---- Blit ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; ---- Advance rotation ----
        inc byte [rot_y]
        mov al, byte [rot_x]       ; slow pitch wobble
        add al, 1
        cmp al, 30
        jl .no_ax_clamp
        mov al, 30
.no_ax_clamp:
        ; Actually let it oscillate: use a separate counter
        ; Just increment ay continuously, ax stays fixed
        ; (Remove above pitch logic - keep ax constant at 43)

        mov eax, SYS_SLEEP
        mov ebx, 2              ; ~20ms per frame
        int 0x80
        jmp .frame

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        jmp .done

.novbe:
        mov eax, SYS_PRINT
        mov ebx, msg_novbe
        int 0x80
.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; Data
;=======================================================================

; Cube vertices in 16.16 fixed-point: 8 × (vx, vy, vz)
vert_src:
        dd -VONE, -VONE, -VONE  ; 0: LBN
        dd  VONE, -VONE, -VONE  ; 1: RBN
        dd -VONE,  VONE, -VONE  ; 2: LTN
        dd  VONE,  VONE, -VONE  ; 3: RTN
        dd -VONE, -VONE,  VONE  ; 4: LBF
        dd  VONE, -VONE,  VONE  ; 5: RBF
        dd -VONE,  VONE,  VONE  ; 6: LTF
        dd  VONE,  VONE,  VONE  ; 7: RTF

; 12 edges: each is 2 vertex indices
edges:
        db 0,1  ; bottom near
        db 1,3  ; right near
        db 3,2  ; top near
        db 2,0  ; left near
        db 4,5  ; bottom far
        db 5,7  ; right far
        db 7,6  ; top far
        db 6,4  ; left far
        db 0,4  ; bottom left
        db 1,5  ; bottom right
        db 2,6  ; top left
        db 3,7  ; top right

; Projected vertices storage: 8 × (sx, sy) dwords
vert_proj: times 8*2 dd 0

; Rotation state
rot_y: db 0                     ; Y-axis angle (0..255)
rot_x: db 43                    ; X-axis tilt

; Sin/Cos LUT in 16.16 fixed-point (256 entries for 0..255 = 0..2π)
; sin_fp[i] = round(sin(i/256*2π) * 65536)
align 4
sin_fp:
        dd      0,  1608,  3215,  4821,  6423,  8022,  9616, 11204
        dd  12785, 14359, 15923, 17479, 19023, 20557, 22077, 23583
        dd  25073, 26547, 28004, 29444, 30865, 32266, 33646, 35004
        dd  36340, 37651, 38937, 40197, 41429, 42634, 43809, 44953
        dd  46065, 47144, 48189, 49198, 50172, 51109, 52007, 52867
        dd  53687, 54466, 55205, 55900, 56553, 57162, 57727, 58247
        dd  58721, 59150, 59532, 59867, 60155, 60395, 60587, 60731
        dd  60827, 60874, 60872, 60822, 60722, 60574, 60376, 60130
        dd  59835, 59491, 59099, 58658, 58169, 57631, 57046, 56414
        dd  55734, 55007, 54234, 53414, 52548, 51637, 50681, 49681
        dd  48637, 47550, 46420, 45249, 44036, 42782, 41490, 40159
        dd  38791, 37387, 35947, 34473, 32966, 31427, 29857, 28258
        dd  26630, 24974, 23292, 21583, 19851, 18096, 16320, 14523
        dd  12706, 10872,  9021,  7154,  5274,  3381,  1479,  -430
        dd  -2339, -4245, -6147, -8044, -9935,-11817,-13690,-15551
        dd -17400,-19234,-21052,-22853,-24635,-26396,-28134,-29849
        dd -31538,-33200,-34832,-36435,-38005,-39542,-41044,-42509
        dd -43936,-45323,-46669,-47973,-49233,-50447,-51615,-52735
        dd -53805,-54825,-55793,-56708,-57569,-58375,-59125,-59818
        dd -60453,-61029,-61546,-62003,-62399,-62734,-63007,-63218
        dd -63366,-63452,-63475,-63435,-63332,-63166,-62938,-62648
        dd -62296,-61882,-61408,-60873,-60278,-59624,-58912,-58143
        dd -57317,-56435,-55498,-54508,-53464,-52369,-51224,-50029
        dd -48786,-47496,-46161,-44782,-43360,-41896,-40393,-38851
        dd -37272,-35658,-34010,-32330,-30619,-28879,-27113,-25321
        dd -23505,-21668,-19810,-17933,-16039,-14130,-12207,-10272
        dd  -8327, -6373, -4413, -2448,  -480,  1488,  3454,  5417
        dd   7374,  9325, 11268, 13201, 15123, 17031, 18924, 20800
        dd  22656, 24491, 26302, 28088, 29846, 31574, 33270, 34933
        dd  36560, 38150, 39700, 41209, 42675, 44097, 45472, 46799
        dd  48077, 49303, 50476, 51596, 52660, 53666, 54613, 55501
        dd  56327, 57091, 57791, 58427, 58997, 59501, 59938, 60307

align 4
cos_fp:
        dd  65536, 65527, 65501, 65457, 65395, 65315, 65218, 65103
        dd  64970, 64820, 64652, 64467, 64264, 64044, 63807, 63553
        dd  63282, 62994, 62689, 62368, 62030, 61676, 61306, 60920
        dd  60518, 60100, 59667, 59218, 58754, 58275, 57781, 57272
        dd  56749, 56212, 55661, 55096, 54518, 53927, 53322, 52705
        dd  52076, 51434, 50780, 50115, 49438, 48750, 48050, 47340
        dd  46620, 45889, 45148, 44398, 43639, 42870, 42092, 41306
        dd  40512, 39710, 38900, 38083, 37259, 36429, 35592, 34749
        dd  33901, 33047, 32188, 31325, 30457, 29585, 28709, 27829
        dd  26946, 26060, 25172, 24281, 23388, 22493, 21596, 20699
        dd  19800, 18900, 18000, 17099, 16199, 15298, 14399, 13500
        dd  12603, 11707, 10813,  9921,  9031,  8144,  7260,  6379
        dd   5502,  4628,  3759,  2893,  2032,  1175,   323,  -525
        dd  -1368, -2207, -3042, -3871, -4695, -5513, -6325, -7130
        dd  -7927, -8717, -9499,-10272,-11036,-11790,-12534,-13268
        dd -13991,-14703,-15403,-16091,-16767,-17430,-18079,-18715
        dd -19337,-19944,-20536,-21113,-21674,-22219,-22747,-23259
        dd -23753,-24230,-24689,-25130,-25552,-25956,-26340,-26706
        dd -27052,-27378,-27684,-27970,-28236,-28481,-28705,-28909
        dd -29091,-29252,-29392,-29510,-29607,-29682,-29736,-29769
        dd -29780,-29770,-29738,-29685,-29610,-29514,-29397,-29259
        dd -29100,-28920,-28720,-28499,-28258,-27997,-27716,-27416
        dd -27097,-26759,-26403,-26029,-25637,-25228,-24802,-24359
        dd -23901,-23428,-22939,-22436,-21918,-21387,-20843,-20287
        dd -19719,-19139,-18549,-17948,-17337,-16717,-16089,-15453
        dd -14810,-14160,-13504,-12842,-12175,-11504,-10830,-10152
        dd  -9472, -8790, -8107, -7423, -6739, -6055, -5372, -4691
        dd  -4011, -3334, -2660, -1989, -1323,  -660,    0,   657
        dd   1310,  1958,  2601,  3239,  3869,  4493,  5109,  5718
        dd   6317,  6908,  7489,  8061,  8622,  9172,  9712, 10240
        dd  10757, 11262, 11755, 12234, 12701, 13155, 13595, 14020
        dd  14432, 14829, 15211, 15579, 15931, 16269, 16591, 16897

; Working variables
fb:    dd 0
sy:    dd 0    ; sin(ay)
cy_r:  dd 0    ; cos(ay)
sx:    dd 0    ; sin(ax)
cx_r:  dd 0    ; cos(ax)
tmp0:  dd 0
tmp1:  dd 0
tmp2:  dd 0
tmp3:  dd 0
tmp4:  dd 0
tmp5:  dd 0

msg_novbe: db "cube: VBE not available", 0x0A, 0

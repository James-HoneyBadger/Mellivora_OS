;=======================================================================
; galaga_sprites.inc — Pixel-art sprite data for galaga.asm
;
; Sprite dimensions match the existing game constants:
;   Player ship:  24 x 16  (PLR_W x PLR_H)
;   Bug enemy:    20 x 12  (ENE_W x ENE_H)
;   Moth enemy:   20 x 12
;   Boss enemy:   20 x 12
;   Bullet:        3 x 12  (BLT_W x BLT_H)
;
; Format: dd width, dd height, then width*height pixels (0xAARRGGBB).
;   Alpha = 0x00 → transparent cell (background shows through)
;   Alpha = 0xFF → opaque pixel
;
; Colour palette (matching game constants where possible):
;   C_PLR_BODY  = 0x44AAFF  (blue-white hull)
;   C_PLR_ENG   = 0xFFDD44  (yellow engine glow)
;   C_BUG       = 0x22FF44  (green)
;   C_MOTH      = 0xFF44CC  (magenta-pink)
;   C_BOSS      = 0xFF4422  (red-orange)
;   C_BULLET    = 0xFFFF44  (yellow)
;=======================================================================

; Convenience colour aliases (with full alpha byte)
%define _TR  0x00000000   ; transparent
%define _PB  0xFF44AAFF   ; player body (blue)
%define _PL  0xFF6ECCFF   ; player body highlight
%define _PE  0xFFFFDD44   ; player engine glow
%define _PD  0xFF2277CC   ; player dark detail
%define _PW  0xFFFFFFFF   ; white detail
%define _BG  0xFF22FF44   ; bug green
%define _BD  0xFF11BB33   ; bug dark
%define _BE  0xFFAAFFBB   ; bug eye
%define _MG  0xFFFF44CC   ; moth magenta
%define _MD  0xFFBB1188   ; moth dark
%define _ME  0xFFFFCCEE   ; moth highlight
%define _RD  0xFFFF4422   ; boss red
%define _RH  0xFFFF8866   ; boss highlight
%define _RE  0xFFFFFF00   ; boss eye
%define _BU  0xFFFFFF44   ; bullet yellow
%define _BC  0xFFFFFFAA   ; bullet core (bright white-yellow)

;=======================================================================
; spr_player  (24 x 16)
; A classic arrow-shaped fighter viewed from above.
;=======================================================================
spr_player:
        dd 24, 16
        ; row 0  — nose tip
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PW,_PW,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 1
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PB,_PL,_PL,_PB,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 2
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PB,_PL,_PL,_PL,_PL,_PB,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 3
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PB,_PL,_PB,_PL,_PL,_PB,_PL,_PB,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 4 — cockpit canopy
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_PB,_PL,_PD,_PD,_PL,_PL,_PD,_PD,_PL,_PB,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 5
        dd _TR,_TR,_TR,_TR,_TR,_TR,_PB,_PL,_PD,_PW,_PW,_PL,_PL,_PW,_PW,_PD,_PL,_PB,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 6
        dd _TR,_TR,_TR,_TR,_TR,_PB,_PB,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PB,_PB,_TR,_TR,_TR,_TR,_TR
        ; row 7 — widest wing section
        dd _TR,_TR,_TR,_TR,_PB,_PL,_PB,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PL,_PB,_PL,_PB,_TR,_TR,_TR,_TR
        ; row 8
        dd _TR,_TR,_TR,_PB,_PL,_PL,_PB,_PL,_PB,_PL,_PL,_PL,_PL,_PL,_PL,_PB,_PL,_PB,_PL,_PL,_PB,_TR,_TR,_TR
        ; row 9
        dd _TR,_TR,_PB,_PL,_PL,_PL,_PB,_PB,_PB,_PL,_PL,_PL,_PL,_PL,_PL,_PB,_PB,_PB,_PL,_PL,_PL,_PB,_TR,_TR
        ; row 10
        dd _TR,_PB,_PL,_PL,_PL,_PL,_TR,_TR,_PB,_PL,_PL,_PL,_PL,_PL,_PL,_PB,_TR,_TR,_PL,_PL,_PL,_PL,_PB,_TR
        ; row 11
        dd _PB,_PL,_PL,_PD,_PD,_TR,_TR,_TR,_TR,_PB,_PB,_PB,_PB,_PB,_PB,_TR,_TR,_TR,_TR,_PD,_PD,_PL,_PL,_PB
        ; row 12 — engine row
        dd _PB,_PL,_PE,_PE,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PE,_PE,_PL,_PB
        ; row 13
        dd _TR,_PB,_PE,_PE,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PE,_PE,_PB,_TR
        ; row 14
        dd _TR,_TR,_PB,_PE,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PE,_PB,_TR,_TR
        ; row 15
        dd _TR,_TR,_TR,_PB,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_PB,_TR,_TR,_TR

;=======================================================================
; spr_bug  (20 x 12) — green insect-style enemy
;=======================================================================
spr_bug:
        dd 20, 12
        ; row 0
        dd _TR,_TR,_BD,_BG,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_BG,_BD,_TR,_TR
        ; row 1
        dd _TR,_BD,_BG,_BG,_BD,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_BD,_BG,_BG,_BD,_TR
        ; row 2 — antennae + eyes
        dd _BD,_BG,_BE,_BG,_BG,_BD,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_BD,_BG,_BG,_BE,_BG,_BD
        ; row 3
        dd _BG,_BG,_BG,_BG,_BG,_BG,_BD,_TR,_TR,_TR,_TR,_TR,_TR,_BD,_BG,_BG,_BG,_BG,_BG,_BG
        ; row 4 — body widest
        dd _TR,_BD,_BG,_BG,_BG,_BG,_BG,_BD,_BD,_BG,_BG,_BG,_BD,_BD,_BG,_BG,_BG,_BG,_BD,_TR
        ; row 5
        dd _TR,_TR,_BD,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BD,_TR,_TR
        ; row 6
        dd _TR,_TR,_TR,_BD,_BG,_BG,_BG,_BE,_BG,_BG,_BG,_BG,_BE,_BG,_BG,_BG,_BD,_TR,_TR,_TR
        ; row 7
        dd _TR,_TR,_BD,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BD,_TR,_TR
        ; row 8 — legs
        dd _TR,_BD,_BG,_BG,_BD,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BD,_BG,_BG,_BD,_TR
        ; row 9
        dd _BD,_BG,_TR,_TR,_BD,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BD,_TR,_TR,_BG,_BD,_TR
        ; row 10
        dd _TR,_TR,_TR,_TR,_TR,_BD,_BG,_BG,_BG,_BG,_BG,_BG,_BG,_BD,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 11
        dd _TR,_TR,_TR,_TR,_TR,_TR,_BD,_BG,_BG,_BD,_BD,_BG,_BD,_TR,_TR,_TR,_TR,_TR,_TR,_TR

;=======================================================================
; spr_moth  (20 x 12) — magenta butterfly-style enemy
;=======================================================================
spr_moth:
        dd 20, 12
        ; row 0 — wing tips
        dd _MG,_MD,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_MD,_MG
        ; row 1
        dd _MG,_MG,_MD,_TR,_TR,_TR,_TR,_TR,_MG,_MG,_MG,_MG,_TR,_TR,_TR,_TR,_TR,_MD,_MG,_MG
        ; row 2
        dd _ME,_MG,_MG,_MD,_TR,_TR,_TR,_MG,_ME,_MG,_MG,_ME,_MG,_TR,_TR,_TR,_MD,_MG,_MG,_ME
        ; row 3
        dd _TR,_ME,_MG,_MG,_MD,_TR,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_TR,_MD,_MG,_MG,_ME,_TR
        ; row 4 — body centre
        dd _TR,_TR,_ME,_MG,_MG,_MG,_MG,_MG,_MD,_MG,_MG,_MD,_MG,_MG,_MG,_MG,_MG,_ME,_TR,_TR
        ; row 5
        dd _TR,_TR,_TR,_MD,_MG,_MG,_MG,_MD,_MG,_ME,_ME,_MG,_MD,_MG,_MG,_MG,_MD,_TR,_TR,_TR
        ; row 6
        dd _TR,_TR,_TR,_TR,_MD,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_MD,_TR,_TR,_TR,_TR
        ; row 7
        dd _TR,_TR,_MD,_MG,_MG,_MG,_MD,_MG,_MG,_MG,_MG,_MG,_MG,_MD,_MG,_MG,_MG,_MD,_TR,_TR
        ; row 8
        dd _TR,_MD,_MG,_MG,_TR,_MD,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_MG,_MD,_TR,_MG,_MG,_MD,_TR
        ; row 9
        dd _MD,_MG,_TR,_TR,_TR,_TR,_MD,_MG,_MG,_MG,_MG,_MG,_MG,_MD,_TR,_TR,_TR,_TR,_MG,_MD
        ; row 10
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_MD,_MG,_MD,_MD,_MG,_MD,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 11
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_MD,_TR,_TR,_MD,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR

;=======================================================================
; spr_boss  (20 x 12) — red flagship / command ship
;=======================================================================
spr_boss:
        dd 20, 12
        ; row 0
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_RD,_RH,_RH,_RD,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 1
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_RD,_RH,_RH,_RH,_RH,_RD,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 2
        dd _TR,_TR,_TR,_TR,_TR,_RD,_RD,_RH,_RE,_RH,_RH,_RE,_RH,_RD,_RD,_TR,_TR,_TR,_TR,_TR
        ; row 3
        dd _TR,_TR,_TR,_TR,_RD,_RH,_RH,_RH,_RH,_RD,_RD,_RH,_RH,_RH,_RH,_RD,_TR,_TR,_TR,_TR
        ; row 4
        dd _TR,_TR,_RD,_RD,_RH,_RH,_RD,_RH,_RH,_RH,_RH,_RH,_RH,_RD,_RH,_RH,_RD,_RD,_TR,_TR
        ; row 5
        dd _TR,_RD,_RH,_RH,_RH,_RD,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RD,_RH,_RH,_RH,_RD,_TR
        ; row 6
        dd _RD,_RH,_RH,_RD,_RH,_RH,_RH,_RE,_RH,_RD,_RD,_RH,_RE,_RH,_RH,_RH,_RD,_RH,_RH,_RD
        ; row 7
        dd _RD,_RH,_RD,_TR,_RD,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RD,_TR,_RD,_RH,_RD
        ; row 8
        dd _TR,_RD,_TR,_TR,_TR,_RD,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RH,_RD,_TR,_TR,_TR,_RD,_TR
        ; row 9
        dd _TR,_TR,_TR,_TR,_TR,_TR,_RD,_RH,_RD,_RH,_RH,_RD,_RH,_RD,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 10
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_RD,_TR,_RD,_RD,_TR,_RD,_TR,_TR,_TR,_TR,_TR,_TR,_TR
        ; row 11
        dd _TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_RD,_TR,_TR,_RD,_TR,_TR,_TR,_TR,_TR,_TR,_TR,_TR

;=======================================================================
; spr_bullet  (3 x 12) — yellow energy bolt
;=======================================================================
spr_bullet:
        dd 3, 12
        dd _TR, _BC, _TR   ; row 0  — tip glow
        dd _BU, _BC, _BU   ; row 1
        dd _BU, _BC, _BU   ; row 2
        dd _BU, _BC, _BU   ; row 3
        dd _BU, _BC, _BU   ; row 4
        dd _BU, _BC, _BU   ; row 5
        dd _BU, _BC, _BU   ; row 6
        dd _BU, _BC, _BU   ; row 7
        dd _BU, _BC, _BU   ; row 8
        dd _BU, _BC, _BU   ; row 9
        dd _BU, _BU, _BU   ; row 10
        dd _TR, _BU, _TR   ; row 11 — tail

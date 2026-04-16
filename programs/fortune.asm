; fortune.asm - Display a random fortune / quote
; Usage: fortune

%include "syscalls.inc"

NUM_FORTUNES    equ 30

start:
        ; Pick random fortune
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, NUM_FORTUNES
        div ecx
        ; EDX = index, save the fortune pointer
        mov rsi, [fortune_ptrs + rdx * 8]

        ; Print opening quote mark
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_open
        int 0x80

        ; Print the fortune text
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80

        ; Print closing quote mark
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_close
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; Data
;=======================================
str_open:       db 10, "  ", 22, " ", 0    ; 22 = double-quote-ish
str_close:      db " ", 22, 10, 10, 0

fortune_ptrs:
        dq f0, f1, f2, f3, f4, f5, f6, f7, f8, f9
        dq f10, f11, f12, f13, f14, f15, f16, f17, f18, f19
        dq f20, f21, f22, f23, f24, f25, f26, f27, f28, f29

f0:     db "The best way to predict the future is to invent it. -- Alan Kay", 0
f1:     db "Simplicity is the ultimate sophistication. -- Leonardo da Vinci", 0
f2:     db "Any sufficiently advanced technology is indistinguishable from magic. -- Arthur C. Clarke", 0
f3:     db "Talk is cheap. Show me the code. -- Linus Torvalds", 0
f4:     db "The only way to do great work is to love what you do. -- Steve Jobs", 0
f5:     db "First, solve the problem. Then, write the code. -- John Johnson", 0
f6:     db "Programs must be written for people to read. -- Abelson & Sussman", 0
f7:     db "Perfection is achieved not when there is nothing more to add, but nothing left to take away. -- Antoine de Saint-Exupery", 0
f8:     db "In theory, there is no difference between theory and practice. In practice, there is. -- Yogi Berra", 0
f9:     db "It works on my machine.", 0
f10:    db "There are only two hard things in CS: cache invalidation and naming things.", 0
f11:    db "The honey badger is the most fearless animal in the world. -- Guinness Book of Records", 0
f12:    db "A computer once beat me at chess, but it was no match for me at kickboxing. -- Emo Philips", 0
f13:    db "Real programmers count from zero.", 0
f14:    db "There is no place like 127.0.0.1.", 0
f15:    db "To understand recursion, you must first understand recursion.", 0
f16:    db "Programming today is a race between software engineers striving to build bigger and better idiot-proof programs, and the Universe trying to produce bigger and better idiots. So far, the Universe is winning. -- Rick Cook", 0
f17:    db "The best thing about a boolean is even if you're wrong, you're only off by a bit.", 0
f18:    db "Debugging is twice as hard as writing the code in the first place. -- Brian Kernighan", 0
f19:    db "Unix is user-friendly. It's just particular about who its friends are.", 0
f20:    db "A good programmer is someone who always looks both ways before crossing a one-way street. -- Doug Linder", 0
f21:    db "Measuring programming progress by lines of code is like measuring aircraft building progress by weight. -- Bill Gates", 0
f22:    db "Always code as if the guy who ends up maintaining your code will be a violent psychopath who knows where you live. -- John Woods", 0
f23:    db "Before software should be reusable, it first has to be usable. -- Ralph Johnson", 0
f24:    db "The most disastrous thing you can ever learn is your first programming language. -- Alan Kay", 0
f25:    db "Software is like entropy: it is difficult to grasp, weighs nothing, and obeys the Second Law of Thermodynamics; i.e., it always increases.", 0
f26:    db "Hardware: the parts of a computer that can be kicked.", 0
f27:    db "Mellivora capensis: small in size, enormous in attitude.", 0
f28:    db "640K ought to be enough for anybody.", 0
f29:    db "May your code compile on the first try.", 0

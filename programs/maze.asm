; maze.asm - Random maze generator and solver for Mellivora OS
; Uses recursive backtracking to generate, then BFS to solve
%include "syscalls.inc"

MAZE_W  equ 39         ; Must be odd
MAZE_H  equ 21         ; Must be odd
MAZE_SZ equ MAZE_W * MAZE_H

WALL    equ '#'
PATH    equ ' '
VISITED equ '.'
SOLVE   equ '*'
START   equ 'S'
GOAL    equ 'E'

start:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_generating
        int 0x80

        call generate_maze
        call solve_maze
        call draw_maze

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_press_key
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Wait for key
        mov eax, SYS_GETCHAR
        int 0x80

        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; Draw the maze
;---------------------------------------
draw_maze:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80
        xor edx, edx
.dm_row:
        xor ecx, ecx
.dm_col:
        mov eax, edx
        imul eax, MAZE_W
        add eax, ecx
        movzx ebx, byte [maze + eax]

        cmp bl, WALL
        je .dm_wall
        cmp bl, SOLVE
        je .dm_solve
        cmp bl, START
        je .dm_start
        cmp bl, GOAL
        je .dm_goal
        ; Normal path
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        jmp .dm_next

.dm_wall:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70           ; White on black reversed
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        jmp .dm_next

.dm_solve:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; Green
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xF9           ; middle dot
        int 0x80
        jmp .dm_next

.dm_start:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; Red
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'S'
        int 0x80
        jmp .dm_next

.dm_goal:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'E'
        int 0x80

.dm_next:
        inc ecx
        cmp ecx, MAZE_W
        jl .dm_col
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        inc edx
        cmp edx, MAZE_H
        jl .dm_row
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        POPALL
        ret

;---------------------------------------
; Generate maze using iterative backtracking
; Uses stack-based DFS with PRNG for direction shuffling
;---------------------------------------
generate_maze:
        PUSHALL
        ; Fill with walls
        mov edi, maze
        mov ecx, MAZE_SZ
        mov al, WALL
        rep stosb

        ; Start carving at (1,1)
        mov dword [stack_ptr], 0
        mov eax, 1*MAZE_W + 1
        mov byte [maze + eax], PATH
        push rax               ; push starting cell

        ; Push to DFS stack
        mov edi, [stack_ptr]
        mov [dfs_stack + edi*4], eax
        inc dword [stack_ptr]

.gen_loop:
        mov ecx, [stack_ptr]
        cmp ecx, 0
        je .gen_done

        ; Peek current cell
        dec ecx
        mov eax, [dfs_stack + ecx*4]
        ; Convert to row/col
        xor edx, edx
        mov ebx, MAZE_W
        div ebx
        ; eax = row, edx = col
        mov [.cur_row], eax
        mov [.cur_col], edx

        ; Find unvisited neighbors (2 cells away)
        mov dword [.n_count], 0

        ; Up
        mov eax, [.cur_row]
        sub eax, 2
        cmp eax, 0
        jl .gen_no_up
        mov ebx, eax
        imul ebx, MAZE_W
        add ebx, [.cur_col]
        cmp byte [maze + ebx], WALL
        jne .gen_no_up
        mov ecx, [.n_count]
        mov [.neighbors + ecx*4], ebx
        mov dword [.n_dir + ecx*4], 0  ; direction 0=up
        inc dword [.n_count]
.gen_no_up:
        ; Down
        mov eax, [.cur_row]
        add eax, 2
        cmp eax, MAZE_H
        jge .gen_no_down
        mov ebx, eax
        imul ebx, MAZE_W
        add ebx, [.cur_col]
        cmp byte [maze + ebx], WALL
        jne .gen_no_down
        mov ecx, [.n_count]
        mov [.neighbors + ecx*4], ebx
        mov dword [.n_dir + ecx*4], 1
        inc dword [.n_count]
.gen_no_down:
        ; Left
        mov eax, [.cur_col]
        sub eax, 2
        cmp eax, 0
        jl .gen_no_left
        mov ebx, [.cur_row]
        imul ebx, MAZE_W
        add ebx, eax
        cmp byte [maze + ebx], WALL
        jne .gen_no_left
        mov ecx, [.n_count]
        mov [.neighbors + ecx*4], ebx
        mov dword [.n_dir + ecx*4], 2
        inc dword [.n_count]
.gen_no_left:
        ; Right
        mov eax, [.cur_col]
        add eax, 2
        cmp eax, MAZE_W
        jge .gen_no_right
        mov ebx, [.cur_row]
        imul ebx, MAZE_W
        add ebx, eax
        cmp byte [maze + ebx], WALL
        jne .gen_no_right
        mov ecx, [.n_count]
        mov [.neighbors + ecx*4], ebx
        mov dword [.n_dir + ecx*4], 3
        inc dword [.n_count]
.gen_no_right:

        cmp dword [.n_count], 0
        je .gen_backtrack

        ; Pick random neighbor
        call prng
        xor edx, edx
        div dword [.n_count]
        ; edx = random index
        mov eax, [.neighbors + edx*4]
        mov ecx, [.n_dir + edx*4]

        ; Carve the wall between current and chosen
        mov byte [maze + eax], PATH
        ; Find wall cell (midpoint)
        push rax
        mov ebx, [.cur_row]
        imul ebx, MAZE_W
        add ebx, [.cur_col]
        add ebx, eax
        shr ebx, 1              ; midpoint index
        mov byte [maze + ebx], PATH
        pop rax

        ; Push chosen onto stack
        mov ecx, [stack_ptr]
        mov [dfs_stack + ecx*4], eax
        inc dword [stack_ptr]
        jmp .gen_loop

.gen_backtrack:
        dec dword [stack_ptr]
        jmp .gen_loop

.gen_done:
        ; Set start and end
        mov byte [maze + 1*MAZE_W + 1], START
        mov eax, (MAZE_H-2)*MAZE_W + (MAZE_W-2)
        mov byte [maze + eax], GOAL
        pop rax
        POPALL
        ret

.cur_row:   dd 0
.cur_col:   dd 0
.n_count:   dd 0
.neighbors: dd 0, 0, 0, 0
.n_dir:     dd 0, 0, 0, 0

;---------------------------------------
; Solve maze using BFS
;---------------------------------------
solve_maze:
        PUSHALL
        ; Clear visited
        mov edi, visited
        mov ecx, MAZE_SZ
        xor al, al
        rep stosb
        ; Clear parent
        mov edi, parent
        mov ecx, MAZE_SZ
        mov eax, -1
.sp_fill:
        mov [edi], eax
        add edi, 4
        dec ecx
        jnz .sp_fill

        ; BFS from start (1,1)
        mov dword [q_head], 0
        mov dword [q_tail], 0
        mov eax, 1*MAZE_W + 1
        mov byte [visited + eax], 1
        mov ecx, [q_tail]
        mov [bfs_queue + ecx*4], eax
        inc dword [q_tail]

        mov ebx, (MAZE_H-2)*MAZE_W + (MAZE_W-2)  ; goal index

.bfs_loop:
        mov ecx, [q_head]
        cmp ecx, [q_tail]
        jge .bfs_done

        mov eax, [bfs_queue + ecx*4]
        inc dword [q_head]

        cmp eax, ebx
        je .bfs_found

        ; Try 4 neighbors
        ; Up
        mov edx, eax
        sub edx, MAZE_W
        cmp edx, 0
        jl .bfs_no_up
        call .bfs_try
.bfs_no_up:
        ; Down
        mov edx, eax
        add edx, MAZE_W
        cmp edx, MAZE_SZ
        jge .bfs_no_down
        call .bfs_try
.bfs_no_down:
        ; Left
        mov edx, eax
        dec edx
        call .bfs_try
        ; Right
        mov edx, eax
        inc edx
        call .bfs_try

        jmp .bfs_loop

.bfs_try:
        cmp byte [visited + edx], 0
        jne .bfs_tr
        cmp byte [maze + edx], WALL
        je .bfs_tr
        mov byte [visited + edx], 1
        mov [parent + edx*4], eax
        push rcx
        mov ecx, [q_tail]
        mov [bfs_queue + ecx*4], edx
        inc dword [q_tail]
        pop rcx
.bfs_tr:
        ret

.bfs_found:
        ; Trace back from goal
        mov eax, ebx
.bfs_trace:
        cmp byte [maze + eax], START
        je .bfs_done
        cmp byte [maze + eax], GOAL
        je .bfs_trace_skip
        mov byte [maze + eax], SOLVE
.bfs_trace_skip:
        mov eax, [parent + eax*4]
        cmp eax, -1
        jne .bfs_trace

.bfs_done:
        POPALL
        ret

;---------------------------------------
; Simple PRNG (LCG)
;---------------------------------------
prng:
        push rbx
        mov eax, [prng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [prng_state], eax
        shr eax, 16
        and eax, 0x7FFF
        pop rbx
        ret

;---------------------------------------
; Data
;---------------------------------------
msg_generating: db "Generating maze...", 0x0A, 0
msg_press_key:  db 0x0A, "Press any key to exit.", 0x0A, 0

; Seed PRNG from a constant (will be varied by timing)
prng_state: dd 31337

;---------------------------------------
; BSS - must come after data
;---------------------------------------
maze:       times MAZE_SZ db 0
visited:    times MAZE_SZ db 0
parent:     times MAZE_SZ dd 0
dfs_stack:  times MAZE_SZ dd 0
stack_ptr:  dd 0
bfs_queue:  times MAZE_SZ dd 0
q_head:     dd 0
q_tail:     dd 0

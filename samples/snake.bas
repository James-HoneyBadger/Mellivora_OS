10  REM Snake - Mellivora OS BASIC
20  REM Arrows/WASD to steer, Q to quit
30  REM Board: 30 wide x 18 tall, top-left corner at screen row 3, col 5
40  DIM X(200)
50  DIM Y(200)
60  LET BX = 5
70  LET BY = 3
80  LET BW = 30
90  LET BH = 18
100 LET SEED = 99991
110 CLS
120 PRINT "SNAKE - Arrows or WASD to move, Q to quit"
130 PRINT
140 GOSUB 9000
150 REM --- Init snake (length 3, facing right, at centre) ---
160 LET LEN = 3
170 LET HEAD = 1
180 LET DX = 1
190 LET DY = 0
200 LET X(1) = 15
210 LET Y(1) = 9
220 LET X(2) = 14
230 LET Y(2) = 9
240 LET X(3) = 13
250 LET Y(3) = 9
260 GOSUB 8000
270 REM --- Spawn first food ---
280 GOSUB 7000
290 REM --- Draw initial snake ---
300 LET I = 1
310 WHILE I <= LEN
320   LOCATE BY + Y(I), BX + X(I)
330   PRINT "O";
340   LET I = I + 1
350 WEND
360 LOCATE BY + FY, BX + FX
370 PRINT "*";
380 LOCATE BY + BH + 1, 1
390 PRINT "Score: 0     ";
400 LET SC = 0
410 REM === Main game loop ===
420 LET ALIVE = 1
430 WHILE ALIVE = 1
440   REM Read key (non-blocking)
450   LET K$ = INKEY$
460   IF K$ = "q" OR K$ = "Q" THEN LET ALIVE = 0: GOTO 900
470   IF K$ = "a" OR K$ = "A" THEN IF DX = 0 THEN LET DX = -1: LET DY = 0
480   IF K$ = "d" OR K$ = "D" THEN IF DX = 0 THEN LET DX = 1: LET DY = 0
490   IF K$ = "w" OR K$ = "W" THEN IF DY = 0 THEN LET DX = 0: LET DY = -1
500   IF K$ = "s" OR K$ = "S" THEN IF DY = 0 THEN LET DX = 0: LET DY = 1
510   REM Compute new head position
520   LET NX = X(HEAD) + DX
530   LET NY = Y(HEAD) + DY
540   REM Wall collision
550   IF NX < 1 OR NX > BW OR NY < 1 OR NY > BH THEN LET ALIVE = 0: GOTO 900
560   REM Self collision
570   LET I = 1
580   WHILE I < LEN
590     IF NX = X(I) AND NY = Y(I) THEN LET ALIVE = 0: GOTO 900
600     LET I = I + 1
610   WEND
620   REM Erase tail
630   LET TI = LEN
640   LOCATE BY + Y(TI), BX + X(TI)
650   PRINT " ";
660   REM Shift body backwards in ring (simple shift-down)
670   LET I = LEN
680   WHILE I > 1
690     LET X(I) = X(I-1)
700     LET Y(I) = Y(I-1)
710     LET I = I - 1
720   WEND
730   LET X(1) = NX
740   LET Y(1) = NY
750   REM Draw new head
760   LOCATE BY + NY, BX + NX
770   PRINT "O";
780   REM Food eaten?
790   IF NX = FX AND NY = FY THEN GOSUB 6000
800   SLEEP 5
810 WEND
900 LOCATE BY + BH + 2, 1
910 PRINT "GAME OVER!  Final score: "; SC
920 END
6000 REM Eat food: grow snake, spawn new food, update score
6010 LET LEN = LEN + 1
6020 LET X(LEN) = X(LEN-1)
6030 LET Y(LEN) = Y(LEN-1)
6040 LET SC = SC + 10
6050 LOCATE BY + BH + 1, 1
6060 PRINT "Score: "; SC; "     ";
6070 GOSUB 7000
6080 LOCATE BY + FY, BX + FX
6090 PRINT "*";
6100 RETURN
7000 REM Spawn food at random empty cell
7010 LET SEED = ABS(SEED * 1103515245 + 12345)
7020 LET FX = (SEED - (SEED / BW) * BW) + 1
7030 LET SEED = ABS(SEED * 1103515245 + 12345)
7040 LET FY = (SEED - (SEED / BH) * BH) + 1
7050 RETURN
8000 REM Draw board border
8010 LOCATE BY, BX
8020 PRINT "+";
8030 LET I = 1
8040 WHILE I <= BW
8050   PRINT "-";
8060   LET I = I + 1
8070 WEND
8080 PRINT "+";
8090 LET J = 1
8100 WHILE J <= BH
8110   LOCATE BY + J, BX
8120   PRINT "|";
8130   LOCATE BY + J, BX + BW + 1
8140   PRINT "|";
8150   LET J = J + 1
8160 WEND
8170 LOCATE BY + BH + 1, BX
8180 PRINT "+";
8190 LET I = 1
8200 WHILE I <= BW
8210   PRINT "-";
8220   LET I = I + 1
8230 WEND
8240 PRINT "+";
8250 RETURN
9000 REM Hide cursor and set colors
9010 COLOR 10, 0
9020 RETURN

10 REM Mandelbrot Set - ASCII art for Mellivora OS BASIC
20 REM Uses integer fixed-point arithmetic (scale = 100)
30 REM Plots 60 columns x 22 rows; '*' = in set, ' ' = escaped
40 PRINT "Mandelbrot Set"
50 PRINT "--------------"
60 FOR R = 0 TO 21
70   FOR C = 0 TO 59
80     REM Map pixel to complex plane: real -2.0..1.0, imag -1.1..1.1
90     LET CR = (C * 300 / 59) - 200
100    LET CI = (R * 220 / 21) - 110
110    LET ZR = 0
120    LET ZI = 0
130    LET N = 0
140    LET ESC = 0
150    WHILE N < 30 AND ESC = 0
160      LET T = ZR * ZR / 100 - ZI * ZI / 100 + CR
170      LET ZI = 2 * ZR * ZI / 100 + CI
180      LET ZR = T
190      IF ZR * ZR / 100 + ZI * ZI / 100 > 400 THEN LET ESC = 1
200      LET N = N + 1
210    WEND
220    IF ESC = 0 THEN PRINT "*"; ELSE PRINT " ";
230  NEXT C
240  PRINT
250 NEXT R
260 END

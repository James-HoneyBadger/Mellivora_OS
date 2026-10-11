10 REM Temperature conversion and classification
20 PRINT "Temperature tool"
30 PRINT "Celsius: 20"
40 C = 20
50 F = (C * 9 / 5) + 32
60 PRINT "Fahrenheit: "; F
70 IF C < 0 THEN GOTO 110
80 IF C < 25 THEN GOTO 130
90 PRINT "Status: warm"
100 GOTO 140
110 PRINT "Status: freezing"
120 GOTO 140
130 PRINT "Status: mild"
140 END

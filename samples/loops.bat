REM loops.bat - for, while and arithmetic
for X in alpha beta gamma; do echo item $X; done
set N 1
while test $N -le 3; do echo count $N; set N $((N + 1)); done
echo loops done

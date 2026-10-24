REM conditions.bat - if/then/else and case, one construct per line
set I 2
if test $I -eq 1; then echo one; else echo not-one; fi
if test $I -gt 5; then echo big; else echo small; fi
case $I in 1) echo chosen-one;; 2) echo chosen-two;; esac
if test $I -ge 1; then echo positive; fi
echo conditions done

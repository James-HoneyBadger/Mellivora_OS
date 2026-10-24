REM menu.bat - read a choice and dispatch it with case
echo Choose: 1=hello 2=date 3=quit
read CHOICE
case $CHOICE in 1) echo you chose hello;; 2) echo you chose date;; 3) echo bye;; esac

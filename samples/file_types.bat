REM file_types.bat - which program opens which kind of file
echo Registered file types (your own handlers first, then the machine's):
assoc
echo
echo Choose your own handler (sign in first):   assoc .txt edit
echo Open a file with its handler:              open notes.txt
echo Forget your choice again:                  assoc .txt delete

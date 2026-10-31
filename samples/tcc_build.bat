REM tcc_build.bat - the in-OS toolchain, end to end
REM tcc compiles a C source to a flat binary at PROGRAM_BASE, and the shell then
REM runs what it produced. Both steps are the point: a compiler that writes a
REM file nobody can execute proves nothing. The output goes beside the source
REM directory because /tmp is a virtual directory whose names cannot be exec'd.
tcc /samples/tcc_hello.c /samples/tcc_hello.bin
/samples/tcc_hello.bin

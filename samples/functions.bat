REM functions.bat - functions, positional parameters and shift
function greet { echo Hello, $1! You passed $# arguments. }
greet world one
function first { echo first arg is $1; shift; echo after shift: $1; }
first alpha beta

/* tcc_hello.c - a program the in-OS compiler can build and the shell can run
 *
 * M5's exit criterion asks for an application built with the SDK *and* the
 * in-OS toolchain. The SDK's half is mvapp; this is tcc's, and it is written in
 * the subset tcc documents at the top of programs/tcc.asm: no preprocessor, no
 * headers, one main(), and the builtins tcc emits calls for.
 */
int main()
{
    puts("TCC_BUILD_OK");
    return 0;
}

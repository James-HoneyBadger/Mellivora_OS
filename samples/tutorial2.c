// tutorial2.c - Tutorial: Hello World progression (Step 2: Colors)
// Demonstrates using syscalls for color text output.
// Compile: tcc tutorial2.c tutorial2
// Run:     tutorial2

int i;

int sys_print(char *s) {
    asm("mov eax, 3");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_setcolor(int color) {
    asm("mov eax, 18");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_clear() {
    asm("mov eax, 17");
    asm("int 0x80");
}

int main() {
    sys_clear();

    // Print in different colors (VGA 16-color palette)
    // 0x01=Blue, 0x02=Green, 0x04=Red, 0x0E=Yellow, 0x0F=White
    sys_setcolor(0x0F);
    sys_print("=== Color Demo ===\n\n");

    sys_setcolor(0x09);
    sys_print("  This is bright blue\n");

    sys_setcolor(0x0A);
    sys_print("  This is bright green\n");

    sys_setcolor(0x0C);
    sys_print("  This is bright red\n");

    sys_setcolor(0x0E);
    sys_print("  This is yellow\n");

    sys_setcolor(0x0D);
    sys_print("  This is magenta\n");

    sys_setcolor(0x0B);
    sys_print("  This is cyan\n");

    // Rainbow line
    sys_print("\n");
    i = 1;
    while (i < 16) {
        sys_setcolor(i);
        sys_print("##");
        i = i + 1;
    }

    sys_setcolor(0x07);
    sys_print("\n\nNext step: tutorial3 adds GUI windows\n");
    return 0;
}

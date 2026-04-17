// tutorial3.c - Tutorial: Hello World progression (Step 3: GUI Window)
// Demonstrates creating a GUI window on the Burrows desktop.
// Compile: tcc tutorial3.c tutorial3
// Run:     tutorial3  (from Burrows desktop)

int win;
int evt;
int running;

// GUI sub-functions
int GUI_CREATE    = 0;
int GUI_DESTROY   = 1;
int GUI_FILL_RECT = 2;
int GUI_DRAW_TEXT = 3;
int GUI_POLL      = 4;

// Events
int EVT_NONE      = 0;
int EVT_CLOSE     = 5;

int sys_gui(int sub, int c, int d, int s, int di) {
    asm("mov eax, 38");
    asm("mov ebx, [ebp+8]");
    asm("mov ecx, [ebp+12]");
    asm("mov edx, [ebp+16]");
    asm("mov esi, [ebp+20]");
    asm("mov edi, [ebp+24]");
    asm("int 0x80");
}

int sys_sleep(int ms) {
    asm("mov eax, 16");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_exit(int code) {
    asm("mov eax, 0");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int main() {
    // Create a 200x120 window at position (100,80)
    // ECX = x | (y << 16), EDX = w | (h << 16)
    win = sys_gui(0, (80 << 16) | 100, (120 << 16) | 200, "Hello GUI!", 0);
    if (win < 0) {
        sys_exit(1);
    }

    // Fill background with dark blue
    sys_gui(2, win, (0 << 16) | 0, (120 << 16) | 200, 0x1A1A5E);

    // Draw greeting text
    sys_gui(3, win, (30 << 16) | 20, "Hello from", 0xFFFFFF);
    sys_gui(3, win, (25 << 16) | 45, "Mellivora OS!", 0x4FC3F7);
    sys_gui(3, win, (15 << 16) | 80, "Close to exit", 0x808080);

    // Event loop
    running = 1;
    while (running) {
        evt = sys_gui(4, 0, 0, 0, 0);
        if (evt == 5) {
            running = 0;
        }
        sys_sleep(50);
    }

    sys_gui(1, win, 0, 0, 0);
    return 0;
}

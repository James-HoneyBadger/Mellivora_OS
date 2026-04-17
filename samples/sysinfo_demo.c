// sysinfo_demo.c - System Programming Demo for Mellivora OS TCC
// Demonstrates: process info, memory, clipboard, signals, pipes
// Compile: tcc sysinfo_demo.c sysinfo_demo
// Run:     sysinfo_demo

int pid;
int free_pages;
int boot_pages;
int ret;
int pipe_id;
char buf[256];
char msg[64];
int i;

int sys_putchar(int ch) {
    asm("mov eax, 1");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_print(char *s) {
    asm("mov eax, 3");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_getpid() {
    asm("mov eax, 54");
    asm("int 0x80");
}

int sys_meminfo() {
    // Returns EAX=free_pages, EBX=boot_pages
    asm("mov eax, 67");
    asm("int 0x80");
    // Only captures EAX return
}

int sys_pipe_create() {
    asm("mov eax, 60");
    asm("int 0x80");
}

int sys_pipe_write(int id, char *buf, int len) {
    asm("mov eax, 61");
    asm("mov ebx, [ebp+8]");
    asm("mov ecx, [ebp+12]");
    asm("mov edx, [ebp+16]");
    asm("int 0x80");
}

int sys_pipe_read(int id, char *buf, int max) {
    asm("mov eax, 62");
    asm("mov ebx, [ebp+8]");
    asm("mov ecx, [ebp+12]");
    asm("mov edx, [ebp+16]");
    asm("int 0x80");
}

int sys_pipe_close(int id) {
    asm("mov eax, 63");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

void print_num(int n) {
    char digits[12];
    int idx;
    int neg;
    neg = 0;
    idx = 0;
    if (n == 0) {
        sys_putchar('0');
        return;
    }
    if (n < 0) { neg = 1; n = 0 - n; }
    while (n > 0) {
        digits[idx] = '0' + (n % 10);
        n = n / 10;
        idx = idx + 1;
    }
    if (neg) sys_putchar('-');
    while (idx > 0) {
        idx = idx - 1;
        sys_putchar(digits[idx]);
    }
}

int main() {
    sys_print("=== Mellivora System Programming Demo ===\n\n");

    // 1. Process ID
    pid = sys_getpid();
    sys_print("My PID: ");
    print_num(pid);
    sys_print("\n");

    // 2. Memory info
    free_pages = sys_meminfo();
    sys_print("Free memory pages: ");
    print_num(free_pages);
    sys_print(" (each 4KB = ");
    print_num(free_pages * 4);
    sys_print(" KB free)\n");

    // 3. IPC: Pipe demonstration
    sys_print("\n--- Pipe IPC Demo ---\n");
    pipe_id = sys_pipe_create();
    if (pipe_id >= 0) {
        sys_print("Created pipe #");
        print_num(pipe_id);
        sys_print("\n");

        // Write a message
        ret = sys_pipe_write(pipe_id, "Hello from pipe!", 16);
        sys_print("Wrote ");
        print_num(ret);
        sys_print(" bytes\n");

        // Read it back
        ret = sys_pipe_read(pipe_id, buf, 255);
        if (ret > 0) {
            buf[ret] = 0;
            sys_print("Read back: ");
            sys_print(buf);
            sys_print("\n");
        }

        sys_pipe_close(pipe_id);
        sys_print("Pipe closed\n");
    }

    sys_print("\nDemo complete.\n");
    return 0;
}

// httpclient.c - Simple HTTP client for Mellivora OS TCC
// Demonstrates: socket, connect, send, recv, DNS resolution
// Compile: tcc httpclient.c httpclient
// Run:     httpclient
//
// This fetches a page from the local httpd server.

int fd;
int ret;
int port;
int ip;
int i;
int len;
char buf[2048];
char req[256];

// Syscall wrappers
int sys_print(char *s) {
    asm("mov eax, 3");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_socket(int type) {
    asm("mov eax, 39");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_connect(int fd, int ip, int port) {
    asm("mov eax, 40");
    asm("mov ebx, [ebp+8]");
    asm("mov ecx, [ebp+12]");
    asm("mov edx, [ebp+16]");
    asm("int 0x80");
}

int sys_send(int fd, char *buf, int len) {
    asm("mov eax, 41");
    asm("mov ebx, [ebp+8]");
    asm("mov ecx, [ebp+12]");
    asm("mov edx, [ebp+16]");
    asm("int 0x80");
}

int sys_recv(int fd, char *buf, int len) {
    asm("mov eax, 42");
    asm("mov ebx, [ebp+8]");
    asm("mov ecx, [ebp+12]");
    asm("mov edx, [ebp+16]");
    asm("int 0x80");
}

int sys_sockclose(int fd) {
    asm("mov eax, 47");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int sys_exit(int code) {
    asm("mov eax, 0");
    asm("mov ebx, [ebp+8]");
    asm("int 0x80");
}

int main() {
    sys_print("HTTP Client Demo\n");
    sys_print("Connecting to 127.0.0.1:8080...\n");

    // IP: 127.0.0.1 = 0x0100007F (little-endian)
    ip = 0x0100007F;
    port = 8080;

    // Create TCP socket (type 1 = TCP)
    fd = sys_socket(1);
    if (fd < 0) {
        sys_print("Error: socket creation failed\n");
        sys_exit(1);
    }

    // Connect
    ret = sys_connect(fd, ip, port);
    if (ret < 0) {
        sys_print("Error: connect failed (is httpd running?)\n");
        sys_sockclose(fd);
        sys_exit(1);
    }

    // Send HTTP GET request
    sys_print("Sending GET / ...\n");
    ret = sys_send(fd, "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", 54);
    if (ret < 0) {
        sys_print("Error: send failed\n");
        sys_sockclose(fd);
        sys_exit(1);
    }

    // Receive response
    sys_print("Response:\n---\n");
    ret = sys_recv(fd, buf, 2047);
    if (ret > 0) {
        buf[ret] = 0;
        sys_print(buf);
    }
    sys_print("\n---\nDone.\n");

    sys_sockclose(fd);
    return 0;
}

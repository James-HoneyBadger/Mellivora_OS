// echo_server.c -- TCP echo server demo for Mellivora OS TCC
//
// Binds to port 8080, accepts one client at a time, and echoes back
// every byte it receives.  Send an empty line (just Enter) to close
// the connection and wait for the next client.  Run 'chat' or 'nc'
// to connect.
//
// Syscall wrappers use GCC/TCC inline assembly (__asm__ volatile).
//
// Compile: tcc echo_server.c echo_server
// Run:     echo_server
// Test:    chat 127.0.0.1 8080

// ---- Syscall numbers ------------------------------------------------
// (from programs/syscalls.inc)
#define SYS_EXIT      0
#define SYS_PUTCHAR   1
#define SYS_PRINT     3
#define SYS_SOCKET    39    // EBX=type(1=TCP 2=UDP) -> EAX=fd
#define SYS_CONNECT   40    // EBX=fd ECX=ip EDX=port -> EAX=0/-1
#define SYS_SEND      41    // EBX=fd ECX=buf EDX=len -> EAX=sent
#define SYS_RECV      42    // EBX=fd ECX=buf EDX=max -> EAX=bytes
#define SYS_BIND      43    // EBX=fd ECX=port -> EAX=0/-1
#define SYS_LISTEN    44    // EBX=fd -> EAX=0/-1
#define SYS_ACCEPT    45    // EBX=fd -> EAX=client_fd
#define SYS_SOCKCLOSE 47    // EBX=fd -> EAX=0

// ---- Inline syscall wrappers ----------------------------------------

int sys_socket(int type) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SOCKET), "b"(type)
        : "memory"
    );
    return ret;
}

int sys_bind(int fd, int port) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_BIND), "b"(fd), "c"(port)
        : "memory"
    );
    return ret;
}

int sys_listen(int fd) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_LISTEN), "b"(fd)
        : "memory"
    );
    return ret;
}

int sys_accept(int fd) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_ACCEPT), "b"(fd)
        : "memory"
    );
    return ret;
}

int sys_recv(int fd, char *buf, int maxlen) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_RECV), "b"(fd), "c"(buf), "d"(maxlen)
        : "memory"
    );
    return ret;
}

int sys_send(int fd, char *buf, int len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SEND), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

int sys_sockclose(int fd) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SOCKCLOSE), "b"(fd)
        : "memory"
    );
    return ret;
}

void sys_print(char *s) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(SYS_PRINT), "b"(s)
        : "memory"
    );
}

// ---- Globals --------------------------------------------------------
int server_fd;
int client_fd;
int n;
char recv_buf[512];
char msg_buf[64];

// ---- Helpers --------------------------------------------------------
int msg_i;

void print_str(char *s) {
    sys_print(s);
}

void print_int_nl(char *prefix, int v) {
    // Builds "prefix<v>\n" into msg_buf and prints it
    int i;
    int tmp;
    int dlen;
    char digits[12];
    msg_i = 0;
    i = 0;
    while (prefix[i] != 0) {
        msg_buf[msg_i] = prefix[i];
        msg_i = msg_i + 1;
        i = i + 1;
    }
    // Convert v to decimal
    if (v < 0) {
        msg_buf[msg_i] = '-'; msg_i = msg_i + 1;
        v = 0 - v;
    }
    dlen = 0;
    tmp = v;
    if (tmp == 0) { digits[0] = '0'; dlen = 1; }
    while (tmp > 0) {
        digits[dlen] = (tmp - (tmp / 10) * 10) + 48;
        tmp = tmp / 10;
        dlen = dlen + 1;
    }
    // Reverse digits
    i = dlen - 1;
    while (i >= 0) {
        msg_buf[msg_i] = digits[i];
        msg_i = msg_i + 1;
        i = i - 1;
    }
    msg_buf[msg_i] = 10;
    msg_i = msg_i + 1;
    msg_buf[msg_i] = 0;
    sys_print(msg_buf);
}

// ---- Session handler ------------------------------------------------
void handle_client() {
    int got;
    int i;
    int only_newline;

    print_str("echo_server: client connected\n");

    while (1) {
        got = sys_recv(client_fd, recv_buf, 511);
        if (got <= 0) {
            break;
        }
        recv_buf[got] = 0;

        // Echo back
        sys_send(client_fd, recv_buf, got);

        // Close on empty line (bare CR/LF or LF)
        only_newline = 1;
        i = 0;
        while (i < got) {
            if (recv_buf[i] != 10 && recv_buf[i] != 13) {
                only_newline = 0;
            }
            i = i + 1;
        }
        if (only_newline == 1) {
            break;
        }
    }

    sys_sockclose(client_fd);
    print_str("echo_server: client disconnected\n");
}

// ---- Main -----------------------------------------------------------
int main() {
    print_str("echo_server: starting on port 8080\n");

    // Create TCP socket
    server_fd = sys_socket(1);          // 1 = TCP
    if (server_fd < 0) {
        print_str("echo_server: socket() failed\n");
        return 1;
    }
    print_int_nl("echo_server: server_fd=", server_fd);

    // Bind to port 8080
    n = sys_bind(server_fd, 8080);
    if (n < 0) {
        print_str("echo_server: bind() failed\n");
        sys_sockclose(server_fd);
        return 1;
    }

    // Listen
    n = sys_listen(server_fd);
    if (n < 0) {
        print_str("echo_server: listen() failed\n");
        sys_sockclose(server_fd);
        return 1;
    }

    print_str("echo_server: listening -- connect with: chat 127.0.0.1 8080\n");
    print_str("echo_server: send an empty line to close a session.\n");

    // Accept loop (handles clients one at a time)
    while (1) {
        client_fd = sys_accept(server_fd);
        if (client_fd < 0) {
            print_str("echo_server: accept() error, retrying\n");
            continue;
        }
        handle_client();
    }

    // Not reached
    sys_sockclose(server_fd);
    return 0;
}

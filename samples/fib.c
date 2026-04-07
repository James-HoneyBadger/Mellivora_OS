// fibonacci.c - Fibonacci sequence for Mellivora OS TCC
// Prints first 20 Fibonacci numbers
// Compile: tcc fib.c fib
// Run:     fib

int a;
int b;
int temp;
int count;
int digit;
int num;

int main() {
    // Print header
    putchar('F');
    putchar('i');
    putchar('b');
    putchar(':');
    putchar(' ');

    a = 0;
    b = 1;
    count = 0;

    while (count < 20) {
        // Print current number (a)
        num = a;

        // Handle zero specially
        if (num == 0) {
            putchar('0');
        }

        // For non-zero, extract digits
        if (num > 0) {
            // Find highest power of 10
            temp = 1;
            while ((temp * 10) <= num) {
                temp = temp * 10;
            }
            // Print each digit
            while (temp > 0) {
                digit = num / temp;
                putchar(digit + 48);
                num = num - (digit * temp);
                temp = temp / 10;
            }
        }

        putchar(' ');

        // Next Fibonacci
        temp = (a + b);
        a = b;
        b = temp;
        count = count + 1;
    }
    putchar(10);
    return 0;
}

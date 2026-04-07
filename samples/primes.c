// primes.c - Prime number finder for Mellivora OS TCC
// Finds and displays prime numbers up to 100
// Compile: tcc primes.c primes_c
// Run:     primes_c

int num;
int div;
int is_prime;
int count;
int digit;
int temp;

int main() {
    putchar('P');
    putchar('r');
    putchar('i');
    putchar('m');
    putchar('e');
    putchar('s');
    putchar(':');
    putchar(10);

    num = 2;
    count = 0;

    while (num <= 100) {
        // Test if num is prime
        is_prime = 1;
        div = 2;
        while ((div * div) <= num) {
            // Check if num % div == 0
            temp = num / div;
            temp = temp * div;
            if (temp == num) {
                is_prime = 0;
            }
            div = div + 1;
        }

        if (is_prime == 1) {
            // Print the prime number
            temp = num;
            if (temp >= 100) {
                digit = temp / 100;
                putchar(digit + 48);
                temp = temp - (digit * 100);
            }
            if (num >= 10) {
                digit = temp / 10;
                putchar(digit + 48);
                temp = temp - (digit * 10);
            }
            putchar(temp + 48);
            putchar(' ');

            count = count + 1;
            // Newline every 10 primes
            temp = count / 10;
            temp = temp * 10;
            if (temp == count) {
                putchar(10);
            }
        }
        num = num + 1;
    }
    putchar(10);
    return 0;
}

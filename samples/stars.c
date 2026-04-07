// stars.c - Star pattern generator for Mellivora OS TCC
// Draws a triangle of asterisks
// Compile: tcc stars.c stars
// Run:     stars

int row;
int col;
int size;

int main() {
    size = 15;
    row = 1;

    while (row <= size) {
        // Print leading spaces
        col = 0;
        while (col < (size - row)) {
            putchar(' ');
            col = col + 1;
        }
        // Print stars
        col = 0;
        while (col < ((row * 2) - 1)) {
            putchar('*');
            col = col + 1;
        }
        putchar(10);
        row = row + 1;
    }
    return 0;
}

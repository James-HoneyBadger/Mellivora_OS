// boxes.c - ASCII box drawing for Mellivora OS TCC
// Draws nested boxes using ASCII characters
// Compile: tcc boxes.c boxes
// Run:     boxes

int size;
int row;
int col;
int layer;
int ch;

int main() {
    size = 21;
    row = 0;

    while (row < size) {
        col = 0;
        while (col < size) {
            // Calculate distance from nearest edge
            layer = row;
            if ((size - 1 - row) < layer) {
                layer = size - 1 - row;
            }
            if (col < layer) {
                layer = col;
            }
            if ((size - 1 - col) < layer) {
                layer = size - 1 - col;
            }

            // Alternate characters by layer
            ch = layer - ((layer / 2) * 2);
            if (ch == 0) {
                putchar('#');
            } else {
                putchar('.');
            }
            col = col + 1;
        }
        putchar(10);
        row = row + 1;
    }
    return 0;
}

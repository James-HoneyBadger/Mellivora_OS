// mandelbrot_fp.c - ASCII Mandelbrot set (integer fixed-point) for Mellivora OS TCC
// Renders a 70x22 character grid of the classic Mandelbrot set.
// Uses fixed-point arithmetic (scale = 256) -- no floating point required.
// Compile: tcc mandelbrot_fp.c mandelbrot_fp
// Run:     mandelbrot_fp
//
// All variables global (TCC Mellivora convention: no local vars, no printf).

// Fixed-point scale: 1.0 = 256 (8 fractional bits, values stay in int range)
// View: real [-2.5, 1.0], imag [-1.2, 1.2]
// Grid: 70 columns x 22 rows

int SCALE;      // 256
int MAX_ITER;   // 64

// Grid dimensions
int COLS;   // 70
int ROWS;   // 22

// Viewport in fixed-point
int RE_MIN;   // -2.5 * 256 = -640
int RE_MAX;   //  1.0 * 256 =  256
int IM_MIN;   // -1.2 * 256 = -307
int IM_MAX;   //  1.2 * 256 =  307

// Loop variables
int row; int col;

// Per-pixel computation
int cr; int ci;     // c = cr + ci*i  (fixed-point)
int zr; int zi;     // z = zr + zi*i
int zr2; int zi2;   // zr^2 and zi^2 (fixed-point: scaled twice, need /SCALE)
int zrzi;           // zr*zi
int iter;
int escaped;

// Escape character palette (from dense to sparse)
// ' ' . : - = + * # @ (9 levels + space)
int palette_idx;
int palette_ch;

int get_palette_ch() {
    if (palette_idx == 0)  { palette_ch = 32;  }   // ' '
    if (palette_idx == 1)  { palette_ch = 46;  }   // '.'
    if (palette_idx == 2)  { palette_ch = 58;  }   // ':'
    if (palette_idx == 3)  { palette_ch = 45;  }   // '-'
    if (palette_idx == 4)  { palette_ch = 61;  }   // '='
    if (palette_idx == 5)  { palette_ch = 43;  }   // '+'
    if (palette_idx == 6)  { palette_ch = 42;  }   // '*'
    if (palette_idx == 7)  { palette_ch = 35;  }   // '#'
    if (palette_idx == 8)  { palette_ch = 64;  }   // '@'
    return 0;
}

int main() {
    SCALE    = 256;
    MAX_ITER = 64;
    COLS     = 70;
    ROWS     = 22;
    RE_MIN   = -640;    // -2.5 * 256
    RE_MAX   =  256;    //  1.0 * 256
    IM_MIN   = -307;    // -1.2 * 256
    IM_MAX   =  307;    //  1.2 * 256

    // Print header
    putchar('M'); putchar('a'); putchar('n'); putchar('d'); putchar('e');
    putchar('l'); putchar('b'); putchar('r'); putchar('o'); putchar('t');
    putchar(' '); putchar('S'); putchar('e'); putchar('t');
    putchar(' '); putchar('('); putchar('f'); putchar('i'); putchar('x');
    putchar('e'); putchar('d'); putchar('-'); putchar('p'); putchar('o');
    putchar('i'); putchar('n'); putchar('t'); putchar(')'); putchar(10);

    row = 0;
    while (row < ROWS) {
        // ci = imag component of c for this row
        // Map row [0..ROWS-1] -> [IM_MAX..IM_MIN]
        ci = IM_MAX - row * (IM_MAX - IM_MIN) / ROWS;

        col = 0;
        while (col < COLS) {
            // cr = real component of c for this column
            // Map col [0..COLS-1] -> [RE_MIN..RE_MAX]
            cr = RE_MIN + col * (RE_MAX - RE_MIN) / COLS;

            // Iterate z = z^2 + c, starting from z = 0
            zr = 0;
            zi = 0;
            iter = 0;
            escaped = 0;

            while (iter < MAX_ITER) {
                // zr2 = zr * zr / SCALE  (keep in fixed-point)
                zr2 = (zr * zr) / SCALE;
                // zi2 = zi * zi / SCALE
                zi2 = (zi * zi) / SCALE;

                // Escape test: |z|^2 > 4  ->  zr2 + zi2 > 4*SCALE
                if (zr2 + zi2 > 4 * SCALE) {
                    escaped = 1;
                    iter = MAX_ITER;    // break
                }

                if (escaped == 0) {
                    // z = z^2 + c
                    // new_zr = zr^2 - zi^2 + cr
                    // new_zi = 2*zr*zi + ci
                    zrzi = (zr * zi) / SCALE;
                    zr = zr2 - zi2 + cr;
                    zi = 2 * zrzi + ci;
                    iter = iter + 1;
                }
            }

            // Choose character based on iteration count
            if (escaped == 0) {
                // Inside set: filled
                putchar(64);    // '@'
            } else {
                // Outside: map iter (0..MAX_ITER-1) to palette (0..8)
                palette_idx = iter * 8 / MAX_ITER;
                get_palette_ch();
                putchar(palette_ch);
            }

            col = col + 1;
        }
        putchar(10);
        row = row + 1;
    }

    return 0;
}

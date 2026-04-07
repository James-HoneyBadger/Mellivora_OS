// matrix.c - Matrix rain effect for Mellivora OS TCC
// Simple falling characters animation
// Compile: tcc matrix.c matrix
// Run:     matrix

int seed;
int i;
int j;
int frame;
int col;
int row;
int ch;

// Column state: drop position for each of 40 columns
int d0;  int d1;  int d2;  int d3;  int d4;
int d5;  int d6;  int d7;  int d8;  int d9;
int d10; int d11; int d12; int d13; int d14;
int d15; int d16; int d17; int d18; int d19;
int d20; int d21; int d22; int d23; int d24;
int d25; int d26; int d27; int d28; int d29;
int d30; int d31; int d32; int d33; int d34;
int d35; int d36; int d37; int d38; int d39;

int pos;
int r;

int main() {
    seed = 12345;

    // Initialize drops at random positions
    i = 0;
    while (i < 40) {
        seed = (seed * 1103515245 + 12345);
        r = seed;
        if (r < 0) { r = 0 - r; }
        r = r - ((r / 20) * 20);
        if (i==0) { d0=r; }
        if (i==1) { d1=r; }
        if (i==2) { d2=r; }
        if (i==3) { d3=r; }
        if (i==4) { d4=r; }
        if (i==5) { d5=r; }
        if (i==6) { d6=r; }
        if (i==7) { d7=r; }
        if (i==8) { d8=r; }
        if (i==9) { d9=r; }
        if (i==10) { d10=r; }
        if (i==11) { d11=r; }
        if (i==12) { d12=r; }
        if (i==13) { d13=r; }
        if (i==14) { d14=r; }
        if (i==15) { d15=r; }
        if (i==16) { d16=r; }
        if (i==17) { d17=r; }
        if (i==18) { d18=r; }
        if (i==19) { d19=r; }
        if (i==20) { d20=r; }
        if (i==21) { d21=r; }
        if (i==22) { d22=r; }
        if (i==23) { d23=r; }
        if (i==24) { d24=r; }
        if (i==25) { d25=r; }
        if (i==26) { d26=r; }
        if (i==27) { d27=r; }
        if (i==28) { d28=r; }
        if (i==29) { d29=r; }
        if (i==30) { d30=r; }
        if (i==31) { d31=r; }
        if (i==32) { d32=r; }
        if (i==33) { d33=r; }
        if (i==34) { d34=r; }
        if (i==35) { d35=r; }
        if (i==36) { d36=r; }
        if (i==37) { d37=r; }
        if (i==38) { d38=r; }
        if (i==39) { d39=r; }
        i = i + 1;
    }

    // Print 20 rows of "matrix rain"
    frame = 0;
    while (frame < 20) {
        row = 0;
        while (row < 20) {
            col = 0;
            while (col < 40) {
                // Get drop position for this column
                pos = 0;
                if (col==0) { pos=d0; }
                if (col==1) { pos=d1; }
                if (col==2) { pos=d2; }
                if (col==3) { pos=d3; }
                if (col==4) { pos=d4; }
                if (col==5) { pos=d5; }
                if (col==6) { pos=d6; }
                if (col==7) { pos=d7; }
                if (col==8) { pos=d8; }
                if (col==9) { pos=d9; }
                if (col==10) { pos=d10; }
                if (col==11) { pos=d11; }
                if (col==12) { pos=d12; }
                if (col==13) { pos=d13; }
                if (col==14) { pos=d14; }
                if (col==15) { pos=d15; }
                if (col==16) { pos=d16; }
                if (col==17) { pos=d17; }
                if (col==18) { pos=d18; }
                if (col==19) { pos=d19; }
                if (col==20) { pos=d20; }
                if (col==21) { pos=d21; }
                if (col==22) { pos=d22; }
                if (col==23) { pos=d23; }
                if (col==24) { pos=d24; }
                if (col==25) { pos=d25; }
                if (col==26) { pos=d26; }
                if (col==27) { pos=d27; }
                if (col==28) { pos=d28; }
                if (col==29) { pos=d29; }
                if (col==30) { pos=d30; }
                if (col==31) { pos=d31; }
                if (col==32) { pos=d32; }
                if (col==33) { pos=d33; }
                if (col==34) { pos=d34; }
                if (col==35) { pos=d35; }
                if (col==36) { pos=d36; }
                if (col==37) { pos=d37; }
                if (col==38) { pos=d38; }
                if (col==39) { pos=d39; }

                pos = pos + frame;
                pos = pos - ((pos / 20) * 20);

                if (row == pos) {
                    // Head of drop - random char
                    seed = (seed * 1103515245 + 12345);
                    r = seed;
                    if (r < 0) { r = 0 - r; }
                    ch = 33 + (r - ((r / 90) * 90));
                    putchar(ch);
                } else {
                    // Check if within trail (3 chars behind head)
                    j = row - pos;
                    if (j < 0) { j = j + 20; }
                    if (j < 4) {
                        seed = (seed * 1103515245 + 12345);
                        r = seed;
                        if (r < 0) { r = 0 - r; }
                        ch = 33 + (r - ((r / 90) * 90));
                        putchar(ch);
                    } else {
                        putchar(' ');
                    }
                }
                putchar(' ');
                col = col + 1;
            }
            putchar(10);
            row = row + 1;
        }
        putchar(10);
        frame = frame + 1;
    }

    return 0;
}

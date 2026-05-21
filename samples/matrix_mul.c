// matrix_mul.c - 4x4 matrix multiplication demo for Mellivora OS TCC
// Multiplies two hardcoded 4x4 integer matrices and prints the result.
// Compile: tcc matrix_mul.c matrix_mul
// Run:     matrix_mul
//
// All variables global (TCC Mellivora convention: no local vars, no printf).

// Matrix A (4x4): stored row-major as a00..a33
int a00; int a01; int a02; int a03;
int a10; int a11; int a12; int a13;
int a20; int a21; int a22; int a23;
int a30; int a31; int a32; int a33;

// Matrix B (4x4)
int b00; int b01; int b02; int b03;
int b10; int b11; int b12; int b13;
int b20; int b21; int b22; int b23;
int b30; int b31; int b32; int b33;

// Result C = A * B
int c00; int c01; int c02; int c03;
int c10; int c11; int c12; int c13;
int c20; int c21; int c22; int c23;
int c30; int c31; int c32; int c33;

// Loop indices and temporaries
int ri; int ci; int ki;
int aik; int bkj; int acc;
int pn; int pdiv; int pst; int tmp;
int neg;

// print_int: print signed integer using global pn
int print_int() {
    if (pn == 0) { putchar(48); return 0; }
    neg = 0;
    if (pn < 0) { neg = 1; pn = 0 - pn; }
    if (neg) { putchar(45); }
    pdiv = 1000; pst = 0;
    while (pdiv > 0) {
        tmp = pn / pdiv;
        if (tmp > 0) { pst = 1; }
        if (pst == 1) { putchar(48 + tmp); }
        pn = pn - tmp * pdiv;
        pdiv = pdiv / 10;
    }
    return 0;
}

// print_spaces: print n spaces
int ns;
int print_spaces() {
    while (ns > 0) { putchar(32); ns = ns - 1; }
    return 0;
}

// Get A[ri][ki] into aik
int get_aik() {
    if (ri==0) {
        if (ki==0){aik=a00;} if (ki==1){aik=a01;} if (ki==2){aik=a02;} if (ki==3){aik=a03;}
    }
    if (ri==1) {
        if (ki==0){aik=a10;} if (ki==1){aik=a11;} if (ki==2){aik=a12;} if (ki==3){aik=a13;}
    }
    if (ri==2) {
        if (ki==0){aik=a20;} if (ki==1){aik=a21;} if (ki==2){aik=a22;} if (ki==3){aik=a23;}
    }
    if (ri==3) {
        if (ki==0){aik=a30;} if (ki==1){aik=a31;} if (ki==2){aik=a32;} if (ki==3){aik=a33;}
    }
    return 0;
}

// Get B[ki][ci] into bkj
int get_bkj() {
    if (ki==0) {
        if (ci==0){bkj=b00;} if (ci==1){bkj=b01;} if (ci==2){bkj=b02;} if (ci==3){bkj=b03;}
    }
    if (ki==1) {
        if (ci==0){bkj=b10;} if (ci==1){bkj=b11;} if (ci==2){bkj=b12;} if (ci==3){bkj=b13;}
    }
    if (ki==2) {
        if (ci==0){bkj=b20;} if (ci==1){bkj=b21;} if (ci==2){bkj=b22;} if (ci==3){bkj=b23;}
    }
    if (ki==3) {
        if (ci==0){bkj=b30;} if (ci==1){bkj=b31;} if (ci==2){bkj=b32;} if (ci==3){bkj=b33;}
    }
    return 0;
}

// Set C[ri][ci] = acc
int set_cij() {
    if (ri==0) {
        if (ci==0){c00=acc;} if (ci==1){c01=acc;} if (ci==2){c02=acc;} if (ci==3){c03=acc;}
    }
    if (ri==1) {
        if (ci==0){c10=acc;} if (ci==1){c11=acc;} if (ci==2){c12=acc;} if (ci==3){c13=acc;}
    }
    if (ri==2) {
        if (ci==0){c20=acc;} if (ci==1){c21=acc;} if (ci==2){c22=acc;} if (ci==3){c23=acc;}
    }
    if (ri==3) {
        if (ci==0){c30=acc;} if (ci==1){c31=acc;} if (ci==2){c32=acc;} if (ci==3){c33=acc;}
    }
    return 0;
}

// Get C[ri][ci] into pn for printing
int get_cij() {
    if (ri==0) {
        if (ci==0){pn=c00;} if (ci==1){pn=c01;} if (ci==2){pn=c02;} if (ci==3){pn=c03;}
    }
    if (ri==1) {
        if (ci==0){pn=c10;} if (ci==1){pn=c11;} if (ci==2){pn=c12;} if (ci==3){pn=c13;}
    }
    if (ri==2) {
        if (ci==0){pn=c20;} if (ci==1){pn=c21;} if (ci==2){pn=c22;} if (ci==3){pn=c23;}
    }
    if (ri==3) {
        if (ci==0){pn=c30;} if (ci==1){pn=c31;} if (ci==2){pn=c32;} if (ci==3){pn=c33;}
    }
    return 0;
}

// print_matrix: print 4x4 matrix C with labels
int pr;  // current value for printing
int pw;  // field width counter

// Print an integer right-aligned in a 6-char field
int field_val;
int digits;
int dval;
int dv2;
int print_field() {
    // Count digits (including minus sign)
    digits = 1;
    dval = field_val;
    if (dval < 0) { digits = 2; dval = 0 - dval; }
    dv2 = dval;
    while (dv2 >= 10) { digits = digits + 1; dv2 = dv2 / 10; }
    // Pad with spaces
    pw = 6 - digits;
    while (pw > 0) { putchar(32); pw = pw - 1; }
    // Print the number
    pn = field_val;
    print_int();
    return 0;
}

int print_matrix_4x4() {
    ri = 0;
    while (ri < 4) {
        putchar(124);       // '|'
        ci = 0;
        while (ci < 4) {
            get_cij();
            field_val = pn;
            print_field();
            ci = ci + 1;
        }
        putchar(32); putchar(124); putchar(10);  // ' |\n'
        ri = ri + 1;
    }
    return 0;
}

int print_str_A() {
    putchar('A'); putchar(' '); putchar('='); putchar(10);
    return 0;
}

int print_str_B() {
    putchar('B'); putchar(' '); putchar('='); putchar(10);
    return 0;
}

int print_str_C() {
    putchar('C'); putchar(' '); putchar('='); putchar(' ');
    putchar('A'); putchar(' '); putchar('*'); putchar(' '); putchar('B');
    putchar(' '); putchar('='); putchar(10);
    return 0;
}

// Copy C into "display" by temporarily moving data
// (We'll use C to hold A, then B, then the real result)
// Print A using the C slots
int print_A() {
    // Swap C = A temporarily
    c00=a00; c01=a01; c02=a02; c03=a03;
    c10=a10; c11=a11; c12=a12; c13=a13;
    c20=a20; c21=a21; c22=a22; c23=a23;
    c30=a30; c31=a31; c32=a32; c33=a33;
    print_matrix_4x4();
    return 0;
}

int print_B() {
    c00=b00; c01=b01; c02=b02; c03=b03;
    c10=b10; c11=b11; c12=b12; c13=b13;
    c20=b20; c21=b21; c22=b22; c23=b23;
    c30=b30; c31=b31; c32=b32; c33=b33;
    print_matrix_4x4();
    return 0;
}

int main() {
    // Matrix A: a rotation-like 4x4
    a00= 1; a01= 0; a02= 0; a03= 1;
    a10= 0; a11= 2; a12= 0; a13= 0;
    a20= 0; a21= 0; a22= 3; a23= 0;
    a30= 4; a31= 0; a32= 0; a33= 5;

    // Matrix B: lower-triangular
    b00= 1; b01= 0; b02= 0; b03= 0;
    b10= 2; b11= 1; b12= 0; b13= 0;
    b20= 3; b21= 2; b22= 1; b23= 0;
    b30= 4; b31= 3; b32= 2; b33= 1;

    // Print A
    print_str_A();
    print_A();
    putchar(10);

    // Print B
    print_str_B();
    print_B();
    putchar(10);

    // Compute C = A * B  (standard O(n^3))
    ri = 0;
    while (ri < 4) {
        ci = 0;
        while (ci < 4) {
            acc = 0;
            ki = 0;
            while (ki < 4) {
                get_aik();
                get_bkj();
                acc = acc + aik * bkj;
                ki = ki + 1;
            }
            set_cij();
            ci = ci + 1;
        }
        ri = ri + 1;
    }

    // Print result
    print_str_C();
    print_matrix_4x4();
    putchar(10);

    return 0;
}

// sort.c - Sorting algorithms demo for Mellivora OS TCC
// Selection sort and bubble sort on a fixed 16-element array.
// Compile: tcc sort.c sort
// Run:     sort

int a0;  int a1;  int a2;  int a3;
int a4;  int a5;  int a6;  int a7;
int a8;  int a9;  int a10; int a11;
int a12; int a13; int a14; int a15;

// Global index / temp vars (no local vars to keep TCC simple)
int ii; int jj; int va; int vb; int tmp;
int outer; int inner; int midx; int mval; int cv;
int pass; int swapped;
int pn; int pdiv; int pst;

int print_num() {
    if (pn == 0) { putchar(48); return 0; }
    pdiv = 10000; pst = 0;
    while (pdiv > 0) {
        tmp = pn / pdiv;
        if (tmp > 0) { pst = 1; }
        if (pst == 1) { putchar(48 + tmp); }
        pn = pn - tmp * pdiv;
        pdiv = pdiv / 10;
    }
    return 0;
}

// Get a[ii] into va
int getv() {
    if (ii==0)  { va=a0;  } if (ii==1)  { va=a1;  }
    if (ii==2)  { va=a2;  } if (ii==3)  { va=a3;  }
    if (ii==4)  { va=a4;  } if (ii==5)  { va=a5;  }
    if (ii==6)  { va=a6;  } if (ii==7)  { va=a7;  }
    if (ii==8)  { va=a8;  } if (ii==9)  { va=a9;  }
    if (ii==10) { va=a10; } if (ii==11) { va=a11; }
    if (ii==12) { va=a12; } if (ii==13) { va=a13; }
    if (ii==14) { va=a14; } if (ii==15) { va=a15; }
    return 0;
}

// Set a[ii] = tmp
int setv() {
    if (ii==0)  { a0=tmp;  } if (ii==1)  { a1=tmp;  }
    if (ii==2)  { a2=tmp;  } if (ii==3)  { a3=tmp;  }
    if (ii==4)  { a4=tmp;  } if (ii==5)  { a5=tmp;  }
    if (ii==6)  { a6=tmp;  } if (ii==7)  { a7=tmp;  }
    if (ii==8)  { a8=tmp;  } if (ii==9)  { a9=tmp;  }
    if (ii==10) { a10=tmp; } if (ii==11) { a11=tmp; }
    if (ii==12) { a12=tmp; } if (ii==13) { a13=tmp; }
    if (ii==14) { a14=tmp; } if (ii==15) { a15=tmp; }
    return 0;
}

int print_arr() {
    ii = 0;
    while (ii < 16) {
        getv(); pn = va; print_num(); putchar(32);
        ii = ii + 1;
    }
    putchar(10);
    return 0;
}

// Selection sort: find minimum in remaining range, swap to front
int sel_sort() {
    outer = 0;
    while (outer < 15) {
        ii = outer; getv(); mval = va; midx = outer;
        inner = outer + 1;
        while (inner < 16) {
            ii = inner; getv(); cv = va;
            if (cv < mval) { mval = cv; midx = inner; }
            inner = inner + 1;
        }
        if (midx != outer) {
            ii = outer; getv(); vb = va;   // vb = a[outer]
            ii = midx;  getv();            // va = a[midx]
            ii = outer; tmp = va; setv();  // a[outer] = a[midx]
            ii = midx;  tmp = vb; setv();  // a[midx]  = old a[outer]
        }
        outer = outer + 1;
    }
    return 0;
}

// Bubble sort: repeatedly step through, swap adjacent out-of-order pairs
int bub_sort() {
    pass = 15;
    while (pass > 0) {
        swapped = 0; jj = 0;
        while (jj < pass) {
            ii = jj;     getv(); vb = va;   // vb = a[jj]
            ii = jj + 1; getv();            // va = a[jj+1]
            if (vb > va) {
                ii = jj;     tmp = va; setv(); // a[jj]   = a[jj+1]
                ii = jj + 1; tmp = vb; setv(); // a[jj+1] = old a[jj]
                swapped = 1;
            }
            jj = jj + 1;
        }
        if (swapped == 0) { pass = 0; }
        pass = pass - 1;
    }
    return 0;
}

int load_data() {
    a0=64; a1=34; a2=25; a3=12; a4=22; a5=11; a6=90; a7=7;
    a8=47; a9=83; a10=3; a11=58; a12=19; a13=72; a14=41; a15=55;
    return 0;
}

int main() {
    putchar('S'); putchar('o'); putchar('r'); putchar('t');
    putchar(' '); putchar('D'); putchar('e'); putchar('m');
    putchar('o'); putchar(10); putchar(10);

    load_data();
    putchar('U'); putchar('n'); putchar('s'); putchar('o');
    putchar('r'); putchar('t'); putchar('e'); putchar('d');
    putchar(':'); putchar(32); print_arr();

    load_data(); sel_sort();
    putchar('S'); putchar('e'); putchar('l'); putchar('e');
    putchar('c'); putchar('t'); putchar('i'); putchar('o');
    putchar('n'); putchar(' '); putchar('s'); putchar('o');
    putchar('r'); putchar('t'); putchar(':'); putchar(32);
    print_arr();

    load_data(); bub_sort();
    putchar('B'); putchar('u'); putchar('b'); putchar('b');
    putchar('l'); putchar('e'); putchar(' '); putchar('s');
    putchar('o'); putchar('r'); putchar('t'); putchar(':');
    putchar(32); print_arr();

    putchar('D'); putchar('o'); putchar('n'); putchar('e');
    putchar('.'); putchar(10);
    return 0;
}

// Working variables (global for TCC compatibility)
int ii; int jj; int tmp; int va; int vb;
int pn; int pdiv; int pstart;

// Print a positive decimal integer
int print_num() {
    if (pn == 0) { putchar(48); return 0; }
    pdiv = 10000; pstart = 0;
    while (pdiv > 0) {
        tmp = pn / pdiv;
        if (tmp > 0) { pstart = 1; }
        if (pstart == 1) { putchar(48 + tmp); }
        pn = pn - tmp * pdiv;
        pdiv = pdiv / 10;
    }
    return 0;
}

// Get a[ii] into va
int getv() {
    if (ii==0)  { va=a0;  } if (ii==1)  { va=a1;  }
    if (ii==2)  { va=a2;  } if (ii==3)  { va=a3;  }
    if (ii==4)  { va=a4;  } if (ii==5)  { va=a5;  }
    if (ii==6)  { va=a6;  } if (ii==7)  { va=a7;  }
    if (ii==8)  { va=a8;  } if (ii==9)  { va=a9;  }
    if (ii==10) { va=a10; } if (ii==11) { va=a11; }
    if (ii==12) { va=a12; } if (ii==13) { va=a13; }
    if (ii==14) { va=a14; } if (ii==15) { va=a15; }
    return 0;
}

// Set a[ii] = tmp
int setv() {
    if (ii==0)  { a0=tmp;  } if (ii==1)  { a1=tmp;  }
    if (ii==2)  { a2=tmp;  } if (ii==3)  { a3=tmp;  }
    if (ii==4)  { a4=tmp;  } if (ii==5)  { a5=tmp;  }
    if (ii==6)  { a6=tmp;  } if (ii==7)  { a7=tmp;  }
    if (ii==8)  { a8=tmp;  } if (ii==9)  { a9=tmp;  }
    if (ii==10) { a10=tmp; } if (ii==11) { a11=tmp; }
    if (ii==12) { a12=tmp; } if (ii==13) { a13=tmp; }
    if (ii==14) { a14=tmp; } if (ii==15) { a15=tmp; }
    return 0;
}

// Swap a[ii] with a[jj]
int swapv() {
    getv(); va = va;          // va = a[ii]
    tmp = ii; ii = jj; getv(); vb = va; ii = tmp;  // vb = a[jj]
    tmp = vb; setv();         // a[ii] = vb
    ii = jj; tmp = va; setv(); // a[jj] = va (old a[ii])
    // restore ii
    ii = ii;  // ii is jj right now; restore needs saved value
    return 0;
}

// Print all 16 elements
int print_arr() {
    ii = 0;
    while (ii < 16) {
        getv(); pn = va; print_num(); putchar(32);
        ii = ii + 1;
    }
    putchar(10);
    return 0;
}

// Selection sort
int sel_sort() {
    int outer; int inner; int midx; int mval; int cv;
    outer = 0;
    while (outer < 15) {
        ii = outer; getv(); mval = va; midx = outer;
        inner = outer + 1;
        while (inner < 16) {
            ii = inner; getv(); cv = va;
            if (cv < mval) { mval = cv; midx = inner; }
            inner = inner + 1;
        }
        if (midx != outer) {
            ii = outer;  getv(); va = va;     // va = a[outer]
            ii = midx;   getv(); vb = va;     // vb = a[midx]
            ii = outer;  tmp = vb; setv();    // a[outer] = vb
            ii = midx;   tmp = va; setv();    // a[midx]  = va
        }
        outer = outer + 1;
    }
    return 0;
}

// Bubble sort
int bub_sort() {
    int pass; int swapped; int av; int bv;
    pass = 15;
    while (pass > 0) {
        swapped = 0; ii = 0;
        while (ii < pass) {
            getv(); av = va;                  // av = a[ii]
            jj = ii + 1; tmp = ii; ii = jj; getv(); bv = va; ii = tmp;  // bv = a[ii+1]
            if (av > bv) {
                tmp = bv; setv();              // a[ii] = bv
                ii = jj; tmp = av; setv(); ii = tmp;  // a[jj] = av
                swapped = 1;
            }
            ii = ii + 1;
        }
        if (swapped == 0) { pass = 0; }
        pass = pass - 1;
    }
    return 0;
}

int load_data() {
    a0=64; a1=34; a2=25; a3=12; a4=22; a5=11; a6=90; a7=7;
    a8=47; a9=83; a10=3; a11=58; a12=19; a13=72; a14=41; a15=55;
    return 0;
}

int main() {
    putchar('S'); putchar('o'); putchar('r'); putchar('t');
    putchar(' '); putchar('D'); putchar('e'); putchar('m');
    putchar('o'); putchar(10); putchar(10);

    load_data();
    putchar('U'); putchar('n'); putchar('s'); putchar('o');
    putchar('r'); putchar('t'); putchar('e'); putchar('d');
    putchar(':'); putchar(32);
    print_arr();

    load_data(); sel_sort();
    putchar('S'); putchar('e'); putchar('l'); putchar('e');
    putchar('c'); putchar('t'); putchar('i'); putchar('o');
    putchar('n'); putchar(' '); putchar('s'); putchar('o');
    putchar('r'); putchar('t'); putchar(':'); putchar(32);
    print_arr();

    load_data(); bub_sort();
    putchar('B'); putchar('u'); putchar('b'); putchar('b');
    putchar('l'); putchar('e'); putchar(' '); putchar('s');
    putchar('o'); putchar('r'); putchar('t'); putchar(':');
    putchar(32);
    print_arr();

    putchar('D'); putchar('o'); putchar('n'); putchar('e');
    putchar('.'); putchar(10);
    return 0;
}

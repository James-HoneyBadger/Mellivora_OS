// sha256.c - SHA-256 demonstration for Mellivora OS TCC
// Hashes the hardcoded string "Hello, Mellivora!" and prints the 64-char hex digest.
// Compile: tcc sha256.c sha256
// Run:     sha256
//
// TCC Mellivora convention: all variables global, no local vars in functions.

// Hash state
unsigned int H0,H1,H2,H3,H4,H5,H6,H7;

// Working variables for compression rounds
unsigned int ra,rb,rc,rd,re,rf,rg,rh;

// Message schedule: 16 rolling slots (W[i & 15])
unsigned int W0,W1,W2,W3,W4,W5,W6,W7;
unsigned int W8,W9,W10,W11,W12,W13,W14,W15;

// Temporaries
unsigned int T1,T2,S0,S1,Ch,Maj,tmp;
unsigned int wi,wv;

// Message schedule expansion temporaries
unsigned int ms0,ms1,mw7,mw16;

// print_hex32: print H as 8 lowercase hex digits via putchar
unsigned int phv;
int phi;
int phd;
int print_hex32() {
    phi = 28;
    while (phi >= 0) {
        phd = (phv >> phi) & 0xF;
        if (phd < 10) { putchar(48 + phd); }
        else          { putchar(87 + phd); }  /* 'a' - 10 = 87 */
        phi = phi - 4;
    }
    return 0;
}

// getK: load SHA-256 round constant K[wi] into T2
int getK() {
    if (wi==0) {T2=0x428a2f98;} if (wi==1) {T2=0x71374491;}
    if (wi==2) {T2=0xb5c0fbcf;} if (wi==3) {T2=0xe9b5dba5;}
    if (wi==4) {T2=0x3956c25b;} if (wi==5) {T2=0x59f111f1;}
    if (wi==6) {T2=0x923f82a4;} if (wi==7) {T2=0xab1c5ed5;}
    if (wi==8) {T2=0xd807aa98;} if (wi==9) {T2=0x12835b01;}
    if (wi==10){T2=0x243185be;} if (wi==11){T2=0x550c7dc3;}
    if (wi==12){T2=0x72be5d74;} if (wi==13){T2=0x80deb1fe;}
    if (wi==14){T2=0x9bdc06a7;} if (wi==15){T2=0xc19bf174;}
    if (wi==16){T2=0xe49b69c1;} if (wi==17){T2=0xefbe4786;}
    if (wi==18){T2=0x0fc19dc6;} if (wi==19){T2=0x240ca1cc;}
    if (wi==20){T2=0x2de92c6f;} if (wi==21){T2=0x4a7484aa;}
    if (wi==22){T2=0x5cb0a9dc;} if (wi==23){T2=0x76f988da;}
    if (wi==24){T2=0x983e5152;} if (wi==25){T2=0xa831c66d;}
    if (wi==26){T2=0xb00327c8;} if (wi==27){T2=0xbf597fc7;}
    if (wi==28){T2=0xc6e00bf3;} if (wi==29){T2=0xd5a79147;}
    if (wi==30){T2=0x06ca6351;} if (wi==31){T2=0x14292967;}
    if (wi==32){T2=0x27b70a85;} if (wi==33){T2=0x2e1b2138;}
    if (wi==34){T2=0x4d2c6dfc;} if (wi==35){T2=0x53380d13;}
    if (wi==36){T2=0x650a7354;} if (wi==37){T2=0x766a0abb;}
    if (wi==38){T2=0x81c2c92e;} if (wi==39){T2=0x92722c85;}
    if (wi==40){T2=0xa2bfe8a1;} if (wi==41){T2=0xa81a664b;}
    if (wi==42){T2=0xc24b8b70;} if (wi==43){T2=0xc76c51a3;}
    if (wi==44){T2=0xd192e819;} if (wi==45){T2=0xd6990624;}
    if (wi==46){T2=0xf40e3585;} if (wi==47){T2=0x106aa070;}
    if (wi==48){T2=0x19a4c116;} if (wi==49){T2=0x1e376c08;}
    if (wi==50){T2=0x2748774c;} if (wi==51){T2=0x34b0bcb5;}
    if (wi==52){T2=0x391c0cb3;} if (wi==53){T2=0x4ed8aa4a;}
    if (wi==54){T2=0x5b9cca4f;} if (wi==55){T2=0x682e6ff3;}
    if (wi==56){T2=0x748f82ee;} if (wi==57){T2=0x78a5636f;}
    if (wi==58){T2=0x84c87814;} if (wi==59){T2=0x8cc70208;}
    if (wi==60){T2=0x90befffa;} if (wi==61){T2=0xa4506ceb;}
    if (wi==62){T2=0xbef9a3f7;} if (wi==63){T2=0xc67178f2;}
    return 0;
}

// sha256_compress: process one 512-bit block already loaded in W0..W15
int sha256_compress() {
    ra=H0; rb=H1; rc=H2; rd=H3; re=H4; rf=H5; rg=H6; rh=H7;

    wi = 0;
    while (wi < 64) {
        // Expand message schedule for rounds 16..63
        if (wi >= 16) {
            // sigma0(W[i-15])
            tmp = (wi - 15) & 15;
            if (tmp==0){wv=W0;} if (tmp==1){wv=W1;} if (tmp==2){wv=W2;} if (tmp==3){wv=W3;}
            if (tmp==4){wv=W4;} if (tmp==5){wv=W5;} if (tmp==6){wv=W6;} if (tmp==7){wv=W7;}
            if (tmp==8){wv=W8;} if (tmp==9){wv=W9;} if (tmp==10){wv=W10;}if (tmp==11){wv=W11;}
            if (tmp==12){wv=W12;}if (tmp==13){wv=W13;}if (tmp==14){wv=W14;}if (tmp==15){wv=W15;}
            ms0 = ((wv>>7)|(wv<<25)) ^ ((wv>>18)|(wv<<14)) ^ (wv>>3);

            // sigma1(W[i-2])
            tmp = (wi - 2) & 15;
            if (tmp==0){wv=W0;} if (tmp==1){wv=W1;} if (tmp==2){wv=W2;} if (tmp==3){wv=W3;}
            if (tmp==4){wv=W4;} if (tmp==5){wv=W5;} if (tmp==6){wv=W6;} if (tmp==7){wv=W7;}
            if (tmp==8){wv=W8;} if (tmp==9){wv=W9;} if (tmp==10){wv=W10;}if (tmp==11){wv=W11;}
            if (tmp==12){wv=W12;}if (tmp==13){wv=W13;}if (tmp==14){wv=W14;}if (tmp==15){wv=W15;}
            ms1 = ((wv>>17)|(wv<<15)) ^ ((wv>>19)|(wv<<13)) ^ (wv>>10);

            // W[i-7]
            tmp = (wi - 7) & 15;
            if (tmp==0){wv=W0;} if (tmp==1){wv=W1;} if (tmp==2){wv=W2;} if (tmp==3){wv=W3;}
            if (tmp==4){wv=W4;} if (tmp==5){wv=W5;} if (tmp==6){wv=W6;} if (tmp==7){wv=W7;}
            if (tmp==8){wv=W8;} if (tmp==9){wv=W9;} if (tmp==10){wv=W10;}if (tmp==11){wv=W11;}
            if (tmp==12){wv=W12;}if (tmp==13){wv=W13;}if (tmp==14){wv=W14;}if (tmp==15){wv=W15;}
            mw7 = wv;

            // W[i-16] (slot we are about to overwrite)
            tmp = wi & 15;
            if (tmp==0){wv=W0;} if (tmp==1){wv=W1;} if (tmp==2){wv=W2;} if (tmp==3){wv=W3;}
            if (tmp==4){wv=W4;} if (tmp==5){wv=W5;} if (tmp==6){wv=W6;} if (tmp==7){wv=W7;}
            if (tmp==8){wv=W8;} if (tmp==9){wv=W9;} if (tmp==10){wv=W10;}if (tmp==11){wv=W11;}
            if (tmp==12){wv=W12;}if (tmp==13){wv=W13;}if (tmp==14){wv=W14;}if (tmp==15){wv=W15;}
            mw16 = wv;

            // New W[i & 15]
            wv = mw16 + ms0 + mw7 + ms1;
            tmp = wi & 15;
            if (tmp==0){W0=wv;} if (tmp==1){W1=wv;} if (tmp==2){W2=wv;} if (tmp==3){W3=wv;}
            if (tmp==4){W4=wv;} if (tmp==5){W5=wv;} if (tmp==6){W6=wv;} if (tmp==7){W7=wv;}
            if (tmp==8){W8=wv;} if (tmp==9){W9=wv;} if (tmp==10){W10=wv;}if (tmp==11){W11=wv;}
            if (tmp==12){W12=wv;}if (tmp==13){W13=wv;}if (tmp==14){W14=wv;}if (tmp==15){W15=wv;}
        }

        // Load W[wi & 15]
        tmp = wi & 15;
        if (tmp==0){wv=W0;} if (tmp==1){wv=W1;} if (tmp==2){wv=W2;} if (tmp==3){wv=W3;}
        if (tmp==4){wv=W4;} if (tmp==5){wv=W5;} if (tmp==6){wv=W6;} if (tmp==7){wv=W7;}
        if (tmp==8){wv=W8;} if (tmp==9){wv=W9;} if (tmp==10){wv=W10;}if (tmp==11){wv=W11;}
        if (tmp==12){wv=W12;}if (tmp==13){wv=W13;}if (tmp==14){wv=W14;}if (tmp==15){wv=W15;}

        // Load K[wi] into T2
        getK();

        // SHA-256 round
        S1  = ((re>>6)|(re<<26)) ^ ((re>>11)|(re<<21)) ^ ((re>>25)|(re<<7));
        Ch  = (re & rf) ^ (~re & rg);
        T1  = rh + S1 + Ch + T2 + wv;
        S0  = ((ra>>2)|(ra<<30)) ^ ((ra>>13)|(ra<<19)) ^ ((ra>>22)|(ra<<10));
        Maj = (ra & rb) ^ (ra & rc) ^ (rb & rc);
        tmp = S0 + Maj;

        rh=rg; rg=rf; rf=re; re=rd+T1;
        rd=rc; rc=rb; rb=ra; ra=T1+tmp;

        wi = wi + 1;
    }

    H0=H0+ra; H1=H1+rb; H2=H2+rc; H3=H3+rd;
    H4=H4+re; H5=H5+rf; H6=H6+rg; H7=H7+rh;
    return 0;
}

int main() {
    /* Print label: SHA-256("Hello, Mellivora!") = */
    putchar('S'); putchar('H'); putchar('A'); putchar('-');
    putchar('2'); putchar('5'); putchar('6'); putchar('(');
    putchar('"');
    putchar('H'); putchar('e'); putchar('l'); putchar('l'); putchar('o');
    putchar(','); putchar(' ');
    putchar('M'); putchar('e'); putchar('l'); putchar('l'); putchar('i');
    putchar('v'); putchar('o'); putchar('r'); putchar('a'); putchar('!');
    putchar('"'); putchar(')'); putchar(' '); putchar('='); putchar(' ');

    /* Init SHA-256 hash state (sqrt of primes 2..19) */
    H0=0x6a09e667; H1=0xbb67ae85; H2=0x3c6ef372; H3=0xa54ff53a;
    H4=0x510e527f; H5=0x9b05688c; H6=0x1f83d9ab; H7=0x5be0cd19;

    /* Build padded 512-bit block for "Hello, Mellivora!" (17 bytes)
       Words 0-3: message bytes big-endian
         "Hell" = 0x48656C6C
         "o, M" = 0x6F2C204D
         "elli" = 0x656C6C69
         "vora" = 0x766F7261
       Word 4:  '!' (0x21) then 0x80 pad byte then zeros = 0x21800000
       Words 5-13: 0x00000000
       Word 14: 0x00000000 (high 32 bits of 64-bit message length)
       Word 15: 0x00000088 (low 32 bits: 17*8 = 136 = 0x88 bits)  */
    W0=0x48656C6C; W1=0x6F2C204D; W2=0x656C6C69; W3=0x766F7261;
    W4=0x21800000; W5=0; W6=0; W7=0;
    W8=0; W9=0; W10=0; W11=0;
    W12=0; W13=0; W14=0; W15=0x00000088;

    sha256_compress();

    /* Print 256-bit digest as 64 hex chars */
    phv=H0; print_hex32();
    phv=H1; print_hex32();
    phv=H2; print_hex32();
    phv=H3; print_hex32();
    phv=H4; print_hex32();
    phv=H5; print_hex32();
    phv=H6; print_hex32();
    phv=H7; print_hex32();
    putchar(10);
    return 0;
}

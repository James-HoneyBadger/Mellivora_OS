// json_parse.c -- Minimal JSON tokenizer for Mellivora OS TCC
//
// Tokenizes a hardcoded JSON string and prints each token's type and
// value on a separate line.  No heap allocation; all state is global.
//
// Token types:
//   1=LBRACE  2=RBRACE  3=LBRACK  4=RBRACK
//   5=COLON   6=COMMA   7=STRING  8=NUMBER
//   9=TRUE   10=FALSE  11=NULL    0=END
//
// Compile: tcc json_parse.c json_parse
// Run:     json_parse

// ---- Input ----------------------------------------------------------
char json_buf[512];   // JSON to tokenize (filled in main)
int  json_len;        // total length

// ---- Tokenizer state ------------------------------------------------
int pos;              // current read position
int tok_type;         // type of last token
char tok_val[128];    // value text of last token
int tok_vlen;         // length of tok_val

// ---- Output helpers -------------------------------------------------
int  d_tmp;           // scratch for print_int
int  d_div;
int  d_digit;
char d_buf[12];
int  d_i;
int  d_neg;

void print_str(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        putchar(s[i]);
        i = i + 1;
    }
}

void print_int(int n) {
    d_neg = 0;
    if (n < 0) {
        putchar('-');
        n = 0 - n;
        d_neg = 1;
    }
    if (n == 0) {
        putchar('0');
        return;
    }
    d_i = 0;
    while (n > 0) {
        d_buf[d_i] = (n - (n / 10) * 10) + 48;
        n = n / 10;
        d_i = d_i + 1;
    }
    // reverse
    d_div = 0;
    d_digit = d_i - 1;
    while (d_digit >= 0) {
        putchar(d_buf[d_digit]);
        d_digit = d_digit - 1;
    }
}

// ---- Tokenizer -------------------------------------------------------

// skip_ws: advance pos over whitespace
void skip_ws() {
    while (pos < json_len) {
        if (json_buf[pos] == 32) { pos = pos + 1; }
        else if (json_buf[pos] == 9)  { pos = pos + 1; }
        else if (json_buf[pos] == 10) { pos = pos + 1; }
        else if (json_buf[pos] == 13) { pos = pos + 1; }
        else { return; }
    }
}

// match_str: check if json_buf[pos..] starts with literal s (len chars)
//            and advance pos if so.
int match_lit(char *s, int len) {
    int i;
    i = 0;
    while (i < len) {
        if (pos + i >= json_len) { return 0; }
        if (json_buf[pos + i] != s[i]) { return 0; }
        i = i + 1;
    }
    pos = pos + len;
    return 1;
}

// next_token: advance to the next token, fill tok_type / tok_val
void next_token() {
    int i;
    int c;
    tok_vlen = 0;
    tok_val[0] = 0;

    skip_ws();

    if (pos >= json_len) {
        tok_type = 0;   // END
        return;
    }

    c = json_buf[pos];

    if (c == '{')  { tok_type = 1;  pos = pos + 1; tok_val[0] = '{'; tok_val[1] = 0; return; }
    if (c == '}')  { tok_type = 2;  pos = pos + 1; tok_val[0] = '}'; tok_val[1] = 0; return; }
    if (c == '[')  { tok_type = 3;  pos = pos + 1; tok_val[0] = '['; tok_val[1] = 0; return; }
    if (c == ']')  { tok_type = 4;  pos = pos + 1; tok_val[0] = ']'; tok_val[1] = 0; return; }
    if (c == ':')  { tok_type = 5;  pos = pos + 1; tok_val[0] = ':'; tok_val[1] = 0; return; }
    if (c == ',')  { tok_type = 6;  pos = pos + 1; tok_val[0] = ','; tok_val[1] = 0; return; }

    // String
    if (c == '"') {
        tok_type = 7;
        pos = pos + 1;   // skip opening quote
        i = 0;
        while (pos < json_len && json_buf[pos] != '"') {
            if (json_buf[pos] == '\\' && pos + 1 < json_len) {
                // Copy escape sequence
                tok_val[i] = '\\';
                i = i + 1;
                pos = pos + 1;
                tok_val[i] = json_buf[pos];
                i = i + 1;
            } else {
                tok_val[i] = json_buf[pos];
                i = i + 1;
            }
            pos = pos + 1;
            if (i >= 126) { break; }
        }
        if (pos < json_len) { pos = pos + 1; }   // skip closing quote
        tok_val[i] = 0;
        tok_vlen = i;
        return;
    }

    // Number (optional minus, digits, optional dot+digits)
    if (c == '-' || (c >= '0' && c <= '9')) {
        tok_type = 8;
        i = 0;
        if (c == '-') { tok_val[i] = c; i = i + 1; pos = pos + 1; }
        while (pos < json_len) {
            c = json_buf[pos];
            if (c >= '0' && c <= '9') {
                tok_val[i] = c; i = i + 1; pos = pos + 1;
            } else if (c == '.') {
                tok_val[i] = c; i = i + 1; pos = pos + 1;
            } else if (c == 'e' || c == 'E') {
                tok_val[i] = c; i = i + 1; pos = pos + 1;
                if (pos < json_len && (json_buf[pos] == '+' || json_buf[pos] == '-')) {
                    tok_val[i] = json_buf[pos]; i = i + 1; pos = pos + 1;
                }
            } else {
                break;
            }
            if (i >= 126) { break; }
        }
        tok_val[i] = 0;
        tok_vlen = i;
        return;
    }

    // true / false / null
    if (c == 't') {
        if (match_lit("true", 4)) {
            tok_type = 9;
            tok_val[0] = 't'; tok_val[1] = 'r'; tok_val[2] = 'u';
            tok_val[3] = 'e'; tok_val[4] = 0;
            return;
        }
    }
    if (c == 'f') {
        if (match_lit("false", 5)) {
            tok_type = 10;
            tok_val[0] = 'f'; tok_val[1] = 'a'; tok_val[2] = 'l';
            tok_val[3] = 's'; tok_val[4] = 'e'; tok_val[5] = 0;
            return;
        }
    }
    if (c == 'n') {
        if (match_lit("null", 4)) {
            tok_type = 11;
            tok_val[0] = 'n'; tok_val[1] = 'u'; tok_val[2] = 'l';
            tok_val[3] = 'l'; tok_val[4] = 0;
            return;
        }
    }

    // Unknown: consume single char
    tok_type = 255;
    tok_val[0] = c;
    tok_val[1] = 0;
    pos = pos + 1;
}

// ---- Type name lookup -----------------------------------------------
char tn_end[4];
char tn_lb[7];
char tn_rb[7];
char tn_la[7];
char tn_ra[7];
char tn_col[7];
char tn_com[6];
char tn_str[7];
char tn_num[7];
char tn_tru[5];
char tn_fal[6];
char tn_nul[5];
char tn_unk[8];

void init_names() {
    tn_end[0]='E'; tn_end[1]='N'; tn_end[2]='D'; tn_end[3]=0;
    tn_lb[0]='L'; tn_lb[1]='B'; tn_lb[2]='R'; tn_lb[3]='A';
    tn_lb[4]='C'; tn_lb[5]='E'; tn_lb[6]=0;
    tn_rb[0]='R'; tn_rb[1]='B'; tn_rb[2]='R'; tn_rb[3]='A';
    tn_rb[4]='C'; tn_rb[5]='E'; tn_rb[6]=0;
    tn_la[0]='L'; tn_la[1]='B'; tn_la[2]='R'; tn_la[3]='A';
    tn_la[4]='C'; tn_la[5]='K'; tn_la[6]=0;
    tn_ra[0]='R'; tn_ra[1]='B'; tn_ra[2]='R'; tn_ra[3]='A';
    tn_ra[4]='C'; tn_ra[5]='K'; tn_ra[6]=0;
    tn_col[0]='C'; tn_col[1]='O'; tn_col[2]='L'; tn_col[3]='O';
    tn_col[4]='N'; tn_col[5]=0;
    tn_com[0]='C'; tn_com[1]='O'; tn_com[2]='M'; tn_com[3]='M';
    tn_com[4]='A'; tn_com[5]=0;
    tn_str[0]='S'; tn_str[1]='T'; tn_str[2]='R'; tn_str[3]='I';
    tn_str[4]='N'; tn_str[5]='G'; tn_str[6]=0;
    tn_num[0]='N'; tn_num[1]='U'; tn_num[2]='M'; tn_num[3]='B';
    tn_num[4]='E'; tn_num[5]='R'; tn_num[6]=0;
    tn_tru[0]='T'; tn_tru[1]='R'; tn_tru[2]='U'; tn_tru[3]='E'; tn_tru[4]=0;
    tn_fal[0]='F'; tn_fal[1]='A'; tn_fal[2]='L'; tn_fal[3]='S';
    tn_fal[4]='E'; tn_fal[5]=0;
    tn_nul[0]='N'; tn_nul[1]='U'; tn_nul[2]='L'; tn_nul[3]='L'; tn_nul[4]=0;
    tn_unk[0]='U'; tn_unk[1]='N'; tn_unk[2]='K'; tn_unk[3]='N';
    tn_unk[4]='O'; tn_unk[5]='W'; tn_unk[6]='N'; tn_unk[7]=0;
}

void print_type(int t) {
    if (t == 0)  { print_str(tn_end); return; }
    if (t == 1)  { print_str(tn_lb);  return; }
    if (t == 2)  { print_str(tn_rb);  return; }
    if (t == 3)  { print_str(tn_la);  return; }
    if (t == 4)  { print_str(tn_ra);  return; }
    if (t == 5)  { print_str(tn_col); return; }
    if (t == 6)  { print_str(tn_com); return; }
    if (t == 7)  { print_str(tn_str); return; }
    if (t == 8)  { print_str(tn_num); return; }
    if (t == 9)  { print_str(tn_tru); return; }
    if (t == 10) { print_str(tn_fal); return; }
    if (t == 11) { print_str(tn_nul); return; }
    print_str(tn_unk);
}

// ---- Populate json_buf with test JSON --------------------------------
void fill_json() {
    // {"os":"Mellivora","ver":12,"bits":32,"ok":true,"list":[1,2,3]}
    int i;
    char *s;
    s = "{\"os\":\"Mellivora\",\"ver\":12,\"bits\":32,\"ok\":true,\"list\":[1,2,3]}";
    i = 0;
    while (s[i] != 0) {
        json_buf[i] = s[i];
        i = i + 1;
    }
    json_buf[i] = 0;
    json_len = i;
}

// ---- Main -----------------------------------------------------------
int tok_count;

int main() {
    init_names();
    fill_json();
    pos = 0;
    tok_count = 0;

    // Print header
    print_str("Input: ");
    print_str(json_buf);
    putchar(10);
    putchar(10);
    print_str("Tokens:");
    putchar(10);

    // Tokenize loop
    while (1) {
        next_token();
        // Print: "  [count] TYPE  \"val\""
        putchar(' ');
        putchar(' ');
        print_int(tok_count);
        putchar(' ');
        putchar(' ');
        print_type(tok_type);
        if (tok_vlen > 0) {
            putchar(' ');
            putchar(' ');
            if (tok_type == 7) { putchar('"'); }
            print_str(tok_val);
            if (tok_type == 7) { putchar('"'); }
        }
        putchar(10);

        tok_count = tok_count + 1;

        if (tok_type == 0) { break; }
        if (tok_count > 200) { break; }   // safety limit
    }

    putchar(10);
    print_str("Total tokens: ");
    print_int(tok_count);
    putchar(10);

    return 0;
}

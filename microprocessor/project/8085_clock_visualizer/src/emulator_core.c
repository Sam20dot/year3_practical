#define SIM_LIB
/* 8085 terminal simulator: assembler + all 246 opcodes + live "what if" tools.
   Build: gcc -O2 -o sim8085 sim8085.c      Run: ./sim8085 [file.asm]            */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define GRN "\033[32m"
#define YEL "\033[33m"
#define CYN "\033[36m"
#define RED "\033[31m"
#define OFF "\033[0m"

#include "sim8085.h"
Cpu c;                       /* r: B C D E H L (M unused) A */
uint8_t mem[65536], port[256];
int used, nsrc, nbp, bp[16], sim_quiet; static int entry, have, ap, nerr, curln, nsy;
int line_of[65536], la[4000], ll[4000];
static char *src[4000], dn[256][32];
static uint8_t dl[256];
static struct { char n[24]; int v; } sy[512];
char errlog[2048];
static void report(int ln, const char *m) {
  char b[300];
  if (ln) snprintf(b, sizeof b, "line %d: %s\n", ln, m); else snprintf(b, sizeof b, "%s\n", m);
  strncat(errlog, b, sizeof errlog - strlen(errlog) - 1);
  if (!sim_quiet) printf(RED "%s" OFF, b);
}

/* ---------- instruction table (single source of truth for asm + disasm) ---------- */
enum { K_N, K_R, K_RS, K_MOV, K_MVI, K_I8, K_RP, K_LXI, K_PP, K_A16, K_RST, K_LD };
typedef struct { const char *n; uint8_t op, k; } Ins;
static const Ins ISA[] = {
 {"NOP",0x00,K_N},{"HLT",0x76,K_N},{"MOV",0x40,K_MOV},{"MVI",0x06,K_MVI},{"LXI",0x01,K_LXI},
 {"LDAX",0x0A,K_LD},{"STAX",0x02,K_LD},{"LDA",0x3A,K_A16},{"STA",0x32,K_A16},{"LHLD",0x2A,K_A16},
 {"SHLD",0x22,K_A16},{"XCHG",0xEB,K_N},{"ADD",0x80,K_R},{"ADC",0x88,K_R},{"SUB",0x90,K_R},
 {"SBB",0x98,K_R},{"ANA",0xA0,K_R},{"XRA",0xA8,K_R},{"ORA",0xB0,K_R},{"CMP",0xB8,K_R},
 {"ADI",0xC6,K_I8},{"ACI",0xCE,K_I8},{"SUI",0xD6,K_I8},{"SBI",0xDE,K_I8},{"ANI",0xE6,K_I8},
 {"XRI",0xEE,K_I8},{"ORI",0xF6,K_I8},{"CPI",0xFE,K_I8},{"INR",0x04,K_RS},{"DCR",0x05,K_RS},
 {"INX",0x03,K_RP},{"DCX",0x0B,K_RP},{"DAD",0x09,K_RP},{"DAA",0x27,K_N},{"CMA",0x2F,K_N},
 {"STC",0x37,K_N},{"CMC",0x3F,K_N},{"RLC",0x07,K_N},{"RRC",0x0F,K_N},{"RAL",0x17,K_N},{"RAR",0x1F,K_N},
 {"JMP",0xC3,K_A16},{"JNZ",0xC2,K_A16},{"JZ",0xCA,K_A16},{"JNC",0xD2,K_A16},{"JC",0xDA,K_A16},
 {"JPO",0xE2,K_A16},{"JPE",0xEA,K_A16},{"JP",0xF2,K_A16},{"JM",0xFA,K_A16},
 {"CALL",0xCD,K_A16},{"CNZ",0xC4,K_A16},{"CZ",0xCC,K_A16},{"CNC",0xD4,K_A16},{"CC",0xDC,K_A16},
 {"CPO",0xE4,K_A16},{"CPE",0xEC,K_A16},{"CP",0xF4,K_A16},{"CM",0xFC,K_A16},
 {"RET",0xC9,K_N},{"RNZ",0xC0,K_N},{"RZ",0xC8,K_N},{"RNC",0xD0,K_N},{"RC",0xD8,K_N},
 {"RPO",0xE0,K_N},{"RPE",0xE8,K_N},{"RP",0xF0,K_N},{"RM",0xF8,K_N},
 {"PUSH",0xC5,K_PP},{"POP",0xC1,K_PP},{"XTHL",0xE3,K_N},{"SPHL",0xF9,K_N},{"PCHL",0xE9,K_N},
 {"IN",0xDB,K_I8},{"OUT",0xD3,K_I8},{"EI",0xFB,K_N},{"DI",0xF3,K_N},{"RIM",0x20,K_N},
 {"SIM",0x30,K_N},{"RST",0xC7,K_RST},{0,0,0}};
static const char *RN = "BCDEHLMA";
static const char *PN[] = {"B","D","H","SP"}, *QN[] = {"B","D","H","PSW"};

static void dset(int o, int len, const char *s) { snprintf(dn[o], 32, "%s", s); dl[o] = len; }
void build(void) {
  char b[32];
  for (int i = 0; i < 256; i++) dset(i, 1, "???");
  for (const Ins *I = ISA; I->n; I++) for (int x = 0; x < 8; x++) {
    int o = I->op;
    switch (I->k) {
      case K_N: if (!x) dset(o, 1, I->n); break;
      case K_R: sprintf(b, "%s %c", I->n, RN[x]); dset(o | x, 1, b); break;
      case K_RS: sprintf(b, "%s %c", I->n, RN[x]); dset(o | x << 3, 1, b); break;
      case K_MOV: for (int y = 0; y < 8; y++) if (!(x == 6 && y == 6)) { sprintf(b, "MOV %c,%c", RN[x], RN[y]); dset(o | x << 3 | y, 1, b); } break;
      case K_MVI: sprintf(b, "MVI %c,", RN[x]); dset(o | x << 3, 2, b); break;
      case K_I8: if (!x) dset(o, 2, I->n); break;
      case K_RP: if (x < 4) { sprintf(b, "%s %s", I->n, PN[x]); dset(o | x << 4, 1, b); } break;
      case K_LXI: if (x < 4) { sprintf(b, "LXI %s,", PN[x]); dset(o | x << 4, 3, b); } break;
      case K_PP: if (x < 4) { sprintf(b, "%s %s", I->n, QN[x]); dset(o | x << 4, 1, b); } break;
      case K_A16: if (!x) dset(o, 3, I->n); break;
      case K_RST: sprintf(b, "RST %d", x); dset(o | x << 3, 1, b); break;
      case K_LD: if (x < 2) { sprintf(b, "%s %s", I->n, PN[x]); dset(o | x << 4, 1, b); } break;
    }
  }
}
int dis(int a, char *o) {
  int op = mem[a & 0xFFFF], l = dl[op], n = strlen(dn[op]);
  if (l == 1) strcpy(o, dn[op]);
  else if (l == 2) sprintf(o, "%s%s%02XH", dn[op], dn[op][n - 1] == ',' ? "" : " ", mem[(a + 1) & 0xFFFF]);
  else sprintf(o, "%s%s%04XH", dn[op], dn[op][n - 1] == ',' ? "" : " ", mem[(a + 1) & 0xFFFF] | mem[(a + 2) & 0xFFFF] << 8);
  return l;
}

/* ---------- assembler (two pass: labels first, then bytes) ---------- */
static void symset(const char *n, int v) {
  for (int i = 0; i < nsy; i++) if (!strcmp(sy[i].n, n)) { sy[i].v = v; return; }
  if (nsy < 512) { snprintf(sy[nsy].n, 24, "%s", n); sy[nsy++].v = v; }
}
static int term(const char *s, int pass) {
  int l = strlen(s); char *e; long v;
  if (!l) return 0;
  if (!strcmp(s, "$")) return ap;
  if (s[0] == '\'' && l >= 3) return s[1];
  if (isdigit((unsigned char)s[0])) {
    if (s[0] == '0' && s[1] == 'X') return strtol(s + 2, 0, 16);
    v = strtol(s, &e, 16); if (*e == 'H' && !e[1]) return v;
    if (s[l - 1] == 'B') { char t[40]; snprintf(t, 40, "%s", s); t[l - 1] = 0; v = strtol(t, &e, 2); if (!*e) return v; }
    v = strtol(s, &e, 10); if (!*e || (*e == 'D' && !e[1])) return v;
  }
  for (int i = 0; i < nsy; i++) if (!strcmp(sy[i].n, s)) return sy[i].v;
  if (pass == 2) { char e[80]; snprintf(e, 80, "undefined symbol or bad number '%s'", s); report(curln, e); nerr++; }
  return 0;
}
static int expr(const char *s, int pass) {   /* terms joined by + and - */
  int v = 0, sg = 1, n = 0; char b[40];
  for (;; s++) {
    if (*s == '+' || *s == '-' || !*s) {
      b[n] = 0; if (n) v += sg * term(b, pass);
      sg = *s == '-' ? -1 : 1; n = 0; if (!*s) break;
    } else if (n < 39) b[n++] = *s;
  }
  return v;
}
static int regi(const char *s) { const char *p; return (s[0] && !s[1] && (p = strchr(RN, s[0]))) ? (int)(p - RN) : -1; }
static int pairi(const char *s, int psw) {
  for (int i = 0; i < 4; i++) if (!strcmp(s, psw ? QN[i] : PN[i])) return i;
  return -1;
}
static char *rmsp(char *s) { char *o = s, *p = s; for (; *p; p++) if (!isspace((unsigned char)*p)) *o++ = *p; *o = 0; return s; }
#define ERR(m) do { if (pass == 2) { report(ln, m); nerr++; } return; } while (0)

static void asm_line(char *L, int pass, int ln) {
  char *p = strchr(L, ';'); curln = ln;
  if (p) *p = 0;
  for (p = L; *p; p++) *p = toupper((unsigned char)*p);
  p = L; while (isspace((unsigned char)*p)) p++;
  char *col = strchr(p, ':');
  if (col) { *col = 0; char *e = col; while (e > p && isspace((unsigned char)e[-1])) *--e = 0; if (pass == 1) symset(p, ap); p = col + 1; }
  while (isspace((unsigned char)*p)) p++;
  if (!*p) return;
  char *m = p; while (*p && !isspace((unsigned char)*p)) p++;
  if (*p) *p++ = 0;
  while (isspace((unsigned char)*p)) p++;
  if (!strncmp(p, "EQU", 3)) { if (pass == 1) symset(m, expr(rmsp(p + 3), pass)); return; }
  char *ops = rmsp(p);
  if (!strcmp(m, "END")) return;
  if (!strcmp(m, "ORG")) { ap = expr(ops, pass) & 0xFFFF; return; }
  if (!strcmp(m, "DB") || !strcmp(m, "DW")) {
    int w = m[1] == 'W', st = ap; char *t = ops;
    for (;;) {
      char *q = strchr(t, ','); if (q) *q = 0;
      int v = expr(t, pass);
      if (pass == 2) { mem[ap & 0xFFFF] = v; if (w) mem[(ap + 1) & 0xFFFF] = v >> 8; }
      ap += w ? 2 : 1; if (!q) break; t = q + 1;
    }
    if (pass == 2) { la[ln] = st; ll[ln] = ap - st; if (!have) { entry = st; have = 1; } }
    return;
  }
  char *a = ops, *b = strchr(ops, ',');
  if (b) *b++ = 0; else b = ops + strlen(ops);
  const Ins *I = ISA; while (I->n && strcmp(I->n, m)) I++;
  if (!I->n) { if (pass == 2) { char e[560]; snprintf(e, 560, "unknown mnemonic '%s'", m); report(ln, e); nerr++; } ap++; return; }
  int op = I->op, len = 1, v = 0, r, d;
  switch (I->k) {
    case K_R:  if ((r = regi(a)) < 0) ERR("bad register"); op |= r; break;
    case K_RS: if ((r = regi(a)) < 0) ERR("bad register"); op |= r << 3; break;
    case K_MOV: r = regi(a); d = regi(b); if (r < 0 || d < 0 || (r == 6 && d == 6)) ERR("MOV copies register to register. To load a value use MVI, e.g. MVI A,32H"); op |= r << 3 | d; break;
    case K_MVI: if ((r = regi(a)) < 0) ERR("bad register"); op |= r << 3; len = 2; v = expr(b, pass); break;
    case K_I8: len = 2; v = expr(a, pass); break;
    case K_RP: if ((r = pairi(a, 0)) < 0) ERR("use B, D, H or SP"); op |= r << 4; break;
    case K_LXI: if ((r = pairi(a, 0)) < 0) ERR("use B, D, H or SP"); op |= r << 4; len = 3; v = expr(b, pass); break;
    case K_PP: if ((r = pairi(a, 1)) < 0) ERR("use B, D, H or PSW"); op |= r << 4; break;
    case K_A16: len = 3; v = expr(a, pass); break;
    case K_RST: v = expr(a, pass); if (v < 0 || v > 7) ERR("RST needs 0-7"); op |= v << 3; v = 0; break;
    case K_LD: if ((r = pairi(a, 0)) < 0 || r > 1) ERR("use B or D"); op |= r << 4; break;
    default: break;
  }
  if (len == 2 && (v < -128 || v > 255)) ERR("value does not fit in 8 bits");
  if (pass == 2) {
    mem[ap & 0xFFFF] = op; if (len > 1) mem[(ap + 1) & 0xFFFF] = v;
    if (len == 3) mem[(ap + 2) & 0xFFFF] = v >> 8;
    line_of[ap & 0xFFFF] = ln; la[ln] = ap; ll[ln] = len;
    if (!have) { entry = ap; have = 1; }
  }
  ap += len;
}

/* ---------- CPU ---------- */
static int HL(void) { return c.r[4] << 8 | c.r[5]; }
static uint8_t rd(int i) { return i == 6 ? mem[HL()] : c.r[i]; }
static void wr(int i, int v) { if (i == 6) mem[HL()] = v; else c.r[i] = v; }
static int rp(int i) { return i == 3 ? c.sp : c.r[i * 2] << 8 | c.r[i * 2 + 1]; }
static void wp(int i, int v) { v &= 0xFFFF; if (i == 3) c.sp = v; else { c.r[i * 2] = v >> 8; c.r[i * 2 + 1] = v; } }
static int flags(void) { return c.s << 7 | c.z << 6 | c.ac << 4 | c.p << 2 | 2 | c.cy; }
static void setflags(int v) { c.s = v >> 7 & 1; c.z = v >> 6 & 1; c.ac = v >> 4 & 1; c.p = v >> 2 & 1; c.cy = v & 1; }
static void push(int v) { mem[--c.sp] = v >> 8; mem[--c.sp] = v; }
static int pop(void) { int lo = mem[c.sp++]; int hi = mem[c.sp++]; return hi << 8 | lo; }
static int szp(int v) { int n = 0, t; v &= 255; c.s = v >> 7; c.z = !v; for (t = v; t; t >>= 1) n ^= t & 1; c.p = !n; return v; }
static void alu(int k, int b) {
  int a = c.r[7], x, cy;
  switch (k) {
    case 0: case 1: cy = k && c.cy; x = a + b + cy; c.ac = ((a & 15) + (b & 15) + cy) > 15; c.cy = x > 255; c.r[7] = szp(x); break;
    case 2: case 3: case 7: cy = k == 3 && c.cy; x = a + (~b & 255) + !cy; c.ac = ((a & 15) + (~b & 15) + !cy) > 15; c.cy = !(x > 255); x = szp(x); if (k != 7) c.r[7] = x; break;
    case 4: c.r[7] = szp(a & b); c.cy = 0; c.ac = 1; break;
    case 5: c.r[7] = szp(a ^ b); c.cy = 0; c.ac = 0; break;
    case 6: c.r[7] = szp(a | b); c.cy = 0; c.ac = 0; break;
  }
}
static int cond(int i) {
  switch (i) { case 0: return !c.z; case 1: return c.z; case 2: return !c.cy; case 3: return c.cy;
               case 4: return !c.p; case 5: return c.p; case 6: return !c.s; default: return c.s; }
}
void step(void) {
  if (c.halt) return;
  int op = mem[c.pc], b1 = mem[(c.pc + 1) & 0xFFFF], w = b1 | mem[(c.pc + 2) & 0xFFFF] << 8, T = 4, len = 1, jmp = -1;
  if ((op & 0xC0) == 0x40) {
    if (op == 0x76) { c.halt = 1; T = 5; }
    else { int d = op >> 3 & 7, s = op & 7; wr(d, rd(s)); T = (d == 6 || s == 6) ? 7 : 4; }
  }
  else if ((op & 0xC0) == 0x80) { alu(op >> 3 & 7, rd(op & 7)); T = (op & 7) == 6 ? 7 : 4; }
  else if ((op & 0xC7) == 0xC6) { alu(op >> 3 & 7, b1); len = 2; T = 7; }
  else if ((op & 0xC7) == 6) { wr(op >> 3 & 7, b1); len = 2; T = (op >> 3 & 7) == 6 ? 10 : 7; }
  else if ((op & 0xC6) == 4) {
    int d = op >> 3 & 7, v = rd(d), dec = op & 1, n = (v + (dec ? -1 : 1)) & 255;
    c.ac = dec ? (v & 15) != 0 : (n & 15) == 0; szp(n); wr(d, n); T = d == 6 ? 10 : 4;
  }
  else if ((op & 0xCF) == 1) { wp(op >> 4 & 3, w); len = 3; T = 10; }
  else if ((op & 0xCF) == 3) { wp(op >> 4 & 3, rp(op >> 4 & 3) + 1); T = 6; }
  else if ((op & 0xCF) == 0xB) { wp(op >> 4 & 3, rp(op >> 4 & 3) - 1); T = 6; }
  else if ((op & 0xCF) == 9) { int x = HL() + rp(op >> 4 & 3); c.cy = x > 0xFFFF; wp(2, x); T = 10; }
  else if ((op & 0xC7) == 0xC2) { len = 3; if (cond(op >> 3 & 7)) { jmp = w; T = 10; } else T = 7; }
  else if ((op & 0xC7) == 0xC4) { len = 3; if (cond(op >> 3 & 7)) { push(c.pc + 3); jmp = w; T = 18; } else T = 9; }
  else if ((op & 0xC7) == 0xC0) { if (cond(op >> 3 & 7)) { jmp = pop(); T = 12; } else T = 6; }
  else if ((op & 0xCF) == 0xC5) { int p = op >> 4 & 3; push(p == 3 ? c.r[7] << 8 | flags() : rp(p)); T = 12; }
  else if ((op & 0xCF) == 0xC1) { int v = pop(), p = op >> 4 & 3; if (p == 3) { c.r[7] = v >> 8; setflags(v); } else wp(p, v); T = 10; }
  else if ((op & 0xC7) == 0xC7) { push(c.pc + 1); jmp = op & 0x38; T = 12; }
  else switch (op) {
    case 0x02: case 0x12: mem[rp(op >> 4 & 3)] = c.r[7]; T = 7; break;
    case 0x0A: case 0x1A: c.r[7] = mem[rp(op >> 4 & 3)]; T = 7; break;
    case 0x07: c.cy = c.r[7] >> 7; c.r[7] = c.r[7] << 1 | c.cy; break;
    case 0x0F: c.cy = c.r[7] & 1; c.r[7] = c.r[7] >> 1 | c.cy << 7; break;
    case 0x17: { int o = c.cy; c.cy = c.r[7] >> 7; c.r[7] = c.r[7] << 1 | o; } break;
    case 0x1F: { int o = c.cy; c.cy = c.r[7] & 1; c.r[7] = c.r[7] >> 1 | o << 7; } break;
    case 0x22: mem[w] = c.r[5]; mem[(w + 1) & 0xFFFF] = c.r[4]; len = 3; T = 16; break;
    case 0x2A: c.r[5] = mem[w]; c.r[4] = mem[(w + 1) & 0xFFFF]; len = 3; T = 16; break;
    case 0x32: mem[w] = c.r[7]; len = 3; T = 13; break;
    case 0x3A: c.r[7] = mem[w]; len = 3; T = 13; break;
    case 0x27: { int a = c.r[7], add = 0, cy = c.cy;   /* decimal adjust: fix each BCD digit */
      if (c.ac || (a & 15) > 9) add = 6;
      if (c.cy || a > 0x99) { add |= 0x60; cy = 1; }
      c.ac = ((a & 15) + (add & 15)) > 15; c.cy = cy; c.r[7] = szp(a + add); } break;
    case 0x2F: c.r[7] = ~c.r[7]; break;
    case 0x37: c.cy = 1; break;
    case 0x3F: c.cy = !c.cy; break;
    case 0xC3: jmp = w; len = 3; T = 10; break;
    case 0xC9: jmp = pop(); T = 10; break;
    case 0xCD: push(c.pc + 3); jmp = w; len = 3; T = 18; break;
    case 0xD3: port[b1] = c.r[7]; used = 1; len = 2; T = 10; break;
    case 0xDB: c.r[7] = port[b1]; len = 2; T = 10; break;
    case 0xE3: { int lo = mem[c.sp], hi = mem[(c.sp + 1) & 0xFFFF]; mem[c.sp] = c.r[5]; mem[(c.sp + 1) & 0xFFFF] = c.r[4]; c.r[5] = lo; c.r[4] = hi; T = 16; } break;
    case 0xE9: jmp = HL(); T = 6; break;
    case 0xEB: { int t = c.r[2]; c.r[2] = c.r[4]; c.r[4] = t; t = c.r[3]; c.r[3] = c.r[5]; c.r[5] = t; } break;
    case 0xF9: c.sp = HL(); T = 6; break;
    default: break;   /* NOP, EI, DI, RIM, SIM, unused opcodes */
  }
  c.pc = jmp >= 0 ? jmp : (uint16_t)(c.pc + len);
  c.t += T; c.n++;
}

/* ---------- display ---------- */
static const char *DESC[][2] = {
 {"MOV","copy source into destination, flags untouched"},{"MVI","load the next byte into the register"},
 {"LXI","load next 2 bytes into a register pair (low byte first)"},{"ADD","A = A + source, all flags set"},
 {"ADI","A = A + next byte, all flags set"},{"SUB","A = A - source, CY = borrow"},
 {"CPI","computes A - byte but only flags change: Z=1 if equal, CY=1 if A < byte"},
 {"CMP","computes A - source, only flags change"},{"DAA","fix A back to BCD after an add: adds 6 to any digit > 9"},
 {"INR","register + 1, CY not affected"},{"DCR","register - 1, CY not affected"},
 {"DCX","pair - 1 and NO flags change, so test with MOV A,H then ORA L"},{"INX","pair + 1, no flags change"},
 {"ORA","A = A OR source, CY cleared (ORA A is a cheap 'is A zero' test)"},{"ANA","A = A AND source, CY cleared, AC set"},
 {"XRA","A = A XOR source (XRA A clears A in 1 byte)"},{"JMP","PC = address, always"},{"JNZ","jump if Z = 0, else fall through"},
 {"JZ","jump if Z = 1"},{"JC","jump if CY = 1"},{"JNC","jump if CY = 0"},{"CALL","push return address, then jump"},
 {"RET","pop return address into PC"},{"OUT","send A to an I/O port"},{"IN","read an I/O port into A"},
 {"LDA","A = memory[address]"},{"STA","memory[address] = A"},{"PUSH","store pair on stack, SP - 2"},
 {"POP","load pair from stack, SP + 2"},{"HLT","stop the CPU"},{"NOP","do nothing for 4 T-states"},{0,0}};
static const char *why(const char *d) {
  for (int i = 0; DESC[i][0]; i++) { size_t n = strlen(DESC[i][0]); if (!strncmp(d, DESC[i][0], n) && (d[n] == ' ' || !d[n])) return DESC[i][1]; }
  return "";
}
static void clock7(void) {
  static const uint8_t sg[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};
  int d[6] = {port[0x12] >> 4, port[0x12] & 15, port[0x11] >> 4, port[0x11] & 15, port[0x10] >> 4, port[0x10] & 15};
  printf(RED);
  for (int row = 0; row < 3; row++) {
    printf("   ");
    for (int i = 0; i < 6; i++) {
      int s = d[i] < 10 ? sg[d[i]] : 0;
      if (row == 0) printf(" %c  ", s & 1 ? '_' : ' ');
      else if (row == 1) printf("%c%c%c ", s & 32 ? '|' : ' ', s & 64 ? '_' : ' ', s & 2 ? '|' : ' ');
      else printf("%c%c%c ", s & 16 ? '|' : ' ', s & 8 ? '_' : ' ', s & 4 ? '|' : ' ');
      if (i == 1 || i == 3) printf(row == 1 ? " : " : "   ");
    }
    printf("\n");
  }
  printf(OFF);
}
static void show(void) {
  char d[40];
  printf("\n" CYN "A:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X M:%02X" OFF "\n", c.r[7], c.r[0], c.r[1], c.r[2], c.r[3], c.r[4], c.r[5], mem[HL()]);
  printf("HL:%04X SP:%04X PC:%04X    S:%d Z:%d AC:%d P:%d CY:%d  (F=%02X)\n", HL(), c.sp, c.pc, c.s, c.z, c.ac, c.p, c.cy, flags());
  printf("T-states:%llu  instructions:%llu  = %.6f s at 3 MHz%s\n", (unsigned long long)c.t, (unsigned long long)c.n, c.t / 3e6, c.halt ? "  [HALTED]" : "");
  dis(c.pc, d); printf("next: " YEL "%04X  %s" OFF, c.pc, d);
  int ln = line_of[c.pc]; if (ln && ln <= nsrc) printf("    ; line %d: %s", ln, src[ln - 1]);
  printf("\n");
  if (used) clock7();
}
static void trace_one(void) {
  static uint8_t om[65536]; Cpu o = c; char d[40], *nm = "BCDEHL?A"; int a = c.pc, l = dis(a, d), ln = line_of[a], first = 1;
  memcpy(om, mem, 65536);
  printf(YEL "%04X" OFF ": ", a);
  for (int i = 0; i < 3; i++) { if (i < l) printf("%02X ", mem[(a + i) & 0xFFFF]); else printf("   "); }
  step();
  printf(" %-14s[%d T]", d, (int)(c.t - o.t)); if (ln) printf("  ; line %d", ln); printf("\n     ");
  for (int i = 0; i < 8; i++) if (i != 6 && o.r[i] != c.r[i]) { printf(GRN "%c: %02X->%02X  " OFF, nm[i], o.r[i], c.r[i]); first = 0; }
  if (o.sp != c.sp) { printf(GRN "SP: %04X->%04X  " OFF, o.sp, c.sp); first = 0; }
  if (o.s != c.s) { printf(GRN "S:%d->%d  " OFF, o.s, c.s); first = 0; }
  if (o.z != c.z) { printf(GRN "Z:%d->%d  " OFF, o.z, c.z); first = 0; }
  if (o.ac != c.ac) { printf(GRN "AC:%d->%d  " OFF, o.ac, c.ac); first = 0; }
  if (o.p != c.p) { printf(GRN "P:%d->%d  " OFF, o.p, c.p); first = 0; }
  if (o.cy != c.cy) { printf(GRN "CY:%d->%d  " OFF, o.cy, c.cy); first = 0; }
  for (int i = 0, k = 0; i < 65536 && k < 3; i++) if (om[i] != mem[i]) { printf(GRN "[%04X]: %02X->%02X  " OFF, i, om[i], mem[i]); first = 0; k++; }
  if (first) printf("(no register or flag changed)");
  printf("\n     why: %s\n", why(d));
}

/* ---------- commands ---------- */
static void addsrc(const char *s) { if (nsrc < 4000) src[nsrc++] = strdup(s); }
static void clear_src(void) { for (int i = 0; i < nsrc; i++) free(src[i]); nsrc = 0; }
void reset(void) { memset(&c, 0, sizeof c); c.sp = 0xFFFF; c.pc = entry; memset(port, 0, 256); used = 0; }
int assemble(void) {
  char buf[512];
  memset(mem, 0, 65536); memset(line_of, 0, sizeof line_of); memset(la, 0, sizeof la); memset(ll, 0, sizeof ll);
  nsy = 0; nerr = 0; have = 0; entry = 0; errlog[0] = 0;
  for (int pass = 1; pass <= 2; pass++) {
    ap = 0;
    for (int i = 0; i < nsrc; i++) { snprintf(buf, 512, "%s", src[i]); asm_line(buf, pass, i + 1); }
  }
  if (nerr) { if (!sim_quiet) printf(RED "%d error(s), nothing loaded." OFF "\n", nerr); return 0; }
  reset();
  int n = 0; for (int i = 1; i <= nsrc; i++) n += ll[i];
  if (!sim_quiet) printf(GRN "assembled OK: %d bytes, entry %04XH, %d symbols" OFF "\n", n, entry, nsy);
  return 1;
}
static void list(void) {
  for (int i = 1; i <= nsrc; i++) {
    char h[16] = ""; for (int j = 0; j < ll[i] && j < 3; j++) sprintf(h + strlen(h), "%02X ", mem[(la[i] + j) & 0xFFFF]);
    if (ll[i]) printf("%3d  %04X  %-9s %s\n", i, la[i], h, src[i - 1]); else printf("%3d                 %s\n", i, src[i - 1]);
  }
}
static long hx(const char *s) { return strtol(s, 0, 16); }
void run(uint64_t lim) {
  uint64_t end = c.t + lim; int first = 1;
  while (!c.halt && c.t < end) {
    if (!first) for (int i = 0; i < nbp; i++) if (bp[i] == c.pc) { if (!sim_quiet) printf(YEL "breakpoint at %04X" OFF "\n", c.pc); return; }
    first = 0; step();
  }
}
static void setreg(char *n, int v) {
  for (char *p = n; *p; p++) *p = toupper((unsigned char)*p);
  int i = regi(n);
  if (i >= 0 && i != 6) c.r[i] = v;
  else if (!strcmp(n, "BC")) wp(0, v); else if (!strcmp(n, "DE")) wp(1, v); else if (!strcmp(n, "HL")) wp(2, v);
  else if (!strcmp(n, "SP")) c.sp = v; else if (!strcmp(n, "PC")) c.pc = v;
  else if (!strcmp(n, "S")) c.s = v & 1; else if (!strcmp(n, "Z")) c.z = v & 1; else if (!strcmp(n, "AC")) c.ac = v & 1;
  else if (!strcmp(n, "P")) c.p = v & 1; else if (!strcmp(n, "CY")) c.cy = v & 1;
  else printf("unknown register %s\n", n);
}
static const char *SAMPLE =
 "        ORG 0000H\n"
 "        MVI C,00H      ; seconds (BCD)\n"
 "        MVI D,00H      ; minutes\n"
 "        MVI E,00H      ; hours\n"
 "LOOP:   MOV A,C\n        OUT 10H        ; seconds port\n        MOV A,D\n        OUT 11H        ; minutes port\n"
 "        MOV A,E\n        OUT 12H        ; hours port\n"
 "        CALL DELAY     ; wait about 1 second at 3 MHz\n"
 "        MOV A,C\n        ADI 01H\n        DAA            ; keep it BCD\n        MOV C,A\n"
 "        CPI 60H\n        JNZ LOOP\n        MVI C,00H\n"
 "        MOV A,D\n        ADI 01H\n        DAA\n        MOV D,A\n        CPI 60H\n        JNZ LOOP\n        MVI D,00H\n"
 "        MOV A,E\n        ADI 01H\n        DAA\n        MOV E,A\n        CPI 24H\n        JNZ LOOP\n        MVI E,00H\n"
 "        JMP LOOP\n"
 "DELAY:  MVI B,02H      ; 2 x 62500 x 24 T = 3,000,000 T = 1 s\n"
 "OUTER:  LXI H,0F424H   ; 62500\n"
 "INNER:  DCX H          ; 6 T\n        MOV A,H        ; 4 T\n        ORA L          ; 4 T\n        JNZ INNER      ; 10 T\n"
 "        DCR B\n        JNZ OUTER\n        RET\n";
const char *sim_sample(void) { return SAMPLE; }
const char *sim_src(int i) { return (i >= 1 && i <= nsrc) ? src[i - 1] : ""; }
void load_text(const char *t) {
  char b[512]; clear_src();
  while (*t) { int n = 0; while (*t && *t != '\n' && n < 511) b[n++] = *t++; b[n] = 0; if (*t == '\n') t++; addsrc(b); }
}
static int load_file(const char *f) {
  FILE *fp = fopen(f, "r"); char b[512];
  if (!fp) { printf("cannot open %s\n", f); return 0; }
  clear_src();
  while (fgets(b, 512, fp)) { b[strcspn(b, "\r\n")] = 0; addsrc(b); }
  fclose(fp); return 1;
}
static void help(void) {
  puts("edit            type or paste code, finish with a line containing only .\n"
       "sample | load FILE   load the clock demo / a .asm file\n"
       "asm             assemble (errors show the line number)      list   listing with bytes\n"
       "s [n]           step n instructions, shows WHAT changed and WHY\n"
       "run [T]         run T T-states (default 3000000 = 1 second at 3 MHz)\n"
       "b ADDR          toggle breakpoint      regs      m ADDR [n]      dis ADDR [n]\n"
       "set REG VAL     A B C D E H L BC DE HL SP PC S Z AC P CY       poke ADDR VAL..\n"
       "patch ADDR code change ONE instruction live, e.g.  patch 0020 ADI 02H\n"
       "or just type an instruction, e.g.  MVI A,78H   (runs immediately)\nreset | help | q          (all numbers are hex)");
}

/* immediate mode: type an instruction at the prompt and it runs at once (program PC/counters untouched) */
static int imm(const char *l) {
  char b[640], w[16] = ""; int sa = 0xFFF0, i = 0;
  while (isspace((unsigned char)*l)) l++;
  while (l[i] && !isspace((unsigned char)l[i]) && i < 15) { w[i] = toupper((unsigned char)l[i]); i++; }
  const Ins *I = ISA; while (I->n && strcmp(I->n, w)) I++;
  if (!I->n) return 0;
  uint8_t sv[3], sh = c.halt; uint16_t spc = c.pc; uint64_t st = c.t, sn = c.n; int sh2 = have, se = entry, sl = line_of[sa];
  memcpy(sv, &mem[sa], 3); snprintf(b, 640, "%s", l); b[strcspn(b, "\r\n")] = 0;
  ap = sa; nerr = 0; asm_line(b, 2, 0);
  if (!nerr) { c.halt = 0; c.pc = sa; trace_one(); }
  c.pc = spc; c.t = st; c.n = sn; c.halt = sh; have = sh2; entry = se; line_of[sa] = sl; memcpy(&mem[sa], sv, 3);
  if (!nerr) show();
  return 1;
}

#ifndef SIM_LIB
int main(int argc, char **argv) {
  char line[600], cmd[32], a1[64], a2[400];
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD md; if (GetConsoleMode(h, &md)) SetConsoleMode(h, md | 4);
#endif
  build(); reset();
  puts(CYN "8085 simulator - terminal edition" OFF "   type 'help'");
  if (argc > 1 && load_file(argv[1])) assemble();
  for (;;) {
    printf(GRN "8085> " OFF); fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) break;
    cmd[0] = a1[0] = a2[0] = 0;
    int n = sscanf(line, "%31s %63s %399[^\n]", cmd, a1, a2);
    if (n < 1) continue;
    if (!strcmp(cmd, "q")) break;
    else if (!strcmp(cmd, "help")) help();
    else if (!strcmp(cmd, "edit")) { char b[512]; clear_src(); puts("(paste code, end with a line containing only .)");
      while (fgets(b, 512, stdin)) { b[strcspn(b, "\r\n")] = 0; if (!strcmp(b, ".")) break; addsrc(b); } assemble(); }
    else if (!strcmp(cmd, "sample")) { load_text(SAMPLE); if (assemble()) show(); }
    else if (!strcmp(cmd, "load")) { if (load_file(a1)) assemble(); }
    else if (!strcmp(cmd, "asm")) assemble();
    else if (!strcmp(cmd, "list")) list();
    else if (!strcmp(cmd, "s")) { int k = n > 1 ? atoi(a1) : 1; if (k < 1) k = 1; for (int i = 0; i < k && !c.halt; i++) { if (k <= 20) trace_one(); else step(); } show(); }
    else if (!strcmp(cmd, "run")) { run(n > 1 ? strtoull(a1, 0, 10) : 3000000ULL); show(); }
    else if (!strcmp(cmd, "regs")) show();
    else if (!strcmp(cmd, "reset")) { reset(); show(); }
    else if (!strcmp(cmd, "b")) { int a = hx(a1), f = -1; for (int i = 0; i < nbp; i++) if (bp[i] == a) f = i;
      if (f >= 0) { bp[f] = bp[--nbp]; printf("breakpoint %04X removed\n", a); } else if (nbp < 16) { bp[nbp++] = a; printf("breakpoint at %04X\n", a); } }
    else if (!strcmp(cmd, "m")) { int a = hx(a1), k = n > 2 ? hx(a2) : 64; for (int i = 0; i < k; i += 16) { printf("%04X: ", (a + i) & 0xFFFF);
      for (int j = 0; j < 16 && i + j < k; j++) { int x = (a + i + j) & 0xFFFF; if (x == c.pc) printf(YEL "%02X" OFF " ", mem[x]); else printf("%02X ", mem[x]); } printf("\n"); } }
    else if (!strcmp(cmd, "dis")) { int a = hx(a1), k = n > 2 ? hx(a2) : 8; char d[40]; while (k-- > 0) { int l = dis(a, d); printf("%04X  %s\n", a, d); a = (a + l) & 0xFFFF; } }
    else if (!strcmp(cmd, "set")) { if (n < 3) puts("usage: set REG VAL"); else { setreg(a1, hx(a2)); show(); } }
    else if (!strcmp(cmd, "poke")) { int a = hx(a1); char *t = strtok(a2, " "); while (t) { mem[a++ & 0xFFFF] = hx(t); t = strtok(0, " "); } puts("ok"); }
    else if (!strcmp(cmd, "patch")) { if (n < 3) puts("usage: patch ADDR instruction"); else {
      int a = hx(a1), old = dl[mem[a]]; char b[512], d[40]; snprintf(b, 512, "%s", a2); ap = a; nerr = 0; asm_line(b, 2, 0);
      if (!nerr) { dis(a, d); printf(GRN "%04X now: %s" OFF "%s\n", a, d, dl[mem[a]] != old ? "   (size changed: the next instruction may now be misaligned!)" : ""); } } }
    else if (!imm(line)) puts("unknown command, type 'help'");
  }
  return 0;
}

#endif

#ifndef SIM8085_H
#define SIM8085_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint8_t r[8], s, z, ac, p, cy, halt; uint16_t pc, sp; uint64_t t, n; } Cpu; /* r: B C D E H L (M) A */
extern Cpu c;
extern uint8_t mem[65536], port[256];
extern int used, nsrc, nbp, bp[16], line_of[65536], la[4000], ll[4000], sim_quiet;
extern char errlog[2048];
void build(void);                 /* call once at start */
void reset(void);                 /* registers to power-on, PC = program entry */
int assemble(void);               /* assemble sources set by load_text; 1 = OK, errors in errlog */
void load_text(const char *t);    /* set source text */
void step(void);
void run(uint64_t tstates);       /* run for a T-state budget, stops at HLT / breakpoint */
int dis(int addr, char *out);
const char *sim_sample(void);
const char *sim_src(int line);    /* 1-based */
#ifdef __cplusplus
}
#endif
#endif

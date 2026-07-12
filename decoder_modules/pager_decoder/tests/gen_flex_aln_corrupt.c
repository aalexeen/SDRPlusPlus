/* Generator for the DROP-01 corrupted-word fixture.
 * Same ALN payload as gen_flex_aln.c, but links the corrupt engine
 * (gen_flex_engine_corrupt.c) and sets errors=99 so exactly one content
 * word is made uncorrectable. Writes aln_corrupt_1600.s16.
 *
 * Build (see run_tests.sh):
 *   gcc -O2 -I<pager_reference> gen_flex_aln_corrupt.c \
 *       gen_flex_engine_corrupt.c <pager_reference>/bch.c -lm -o gen_aln_corrupt
 */
#include "gen.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* argv[1] optional: errors mode + output name.
 *   (none) → 99 (content-word corrupt) → aln_corrupt_1600.s16
 *   "addr" → 98 (address-word corrupt) → aln_addr_corrupt_1600.s16
 *   "vec"  → 97 (vector-word corrupt)  → aln_vec_corrupt_1600.s16 */
int main(int argc, char** argv) {
    struct gen_params p;
    struct gen_state s;
    memset(&p, 0, sizeof(p));

    int mode = 99;
    const char* out = "golden/aln_corrupt_1600.s16";
    if (argc > 1 && strcmp(argv[1], "addr") == 0) { mode = 98; out = "golden/aln_addr_corrupt_1600.s16"; }
    else if (argc > 1 && strcmp(argv[1], "vec") == 0) { mode = 97; out = "golden/aln_vec_corrupt_1600.s16"; }

    p.type = gentype_flex;
    p.ampl = 16000;
    p.p.flex.capcode = 1234567;
    p.p.flex.cycle = 0;
    p.p.flex.frame = 0;
    p.p.flex.errors = mode;
    strncpy(p.p.flex.message, "HELLO FLEX 1234567890", sizeof(p.p.flex.message) - 1);

    gen_init_flex(&p, &s);

    int cap = 4 * 1024 * 1024;
    short *buf = malloc(cap * sizeof(short));
    memset(buf, 0, cap * sizeof(short));
    int total = 0, chunk;
    while (total < cap) {
        int want = 4096;
        if (total + want > cap) want = cap - total;
        chunk = gen_flex(buf + total, want, &p, &s);
        if (chunk <= 0) break;
        total += chunk;
    }

    FILE *rf = fopen(out, "wb");
    if (!rf) { perror("open output"); return 1; }
    fwrite(buf, sizeof(short), total, rf);
    fclose(rf);
    printf("Wrote %s (%d samples)\n", out, total);
    free(buf);
    return 0;
}

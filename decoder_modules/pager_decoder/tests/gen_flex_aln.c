#include "gen.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Minimal WAV writer: mono 16-bit PCM */
static void write_wav(const char *path, const short *data, int nsamples, int rate) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror("wav open"); exit(1); }
    int byte_rate = rate * 2;
    int data_bytes = nsamples * 2;
    int chunk = 36 + data_bytes;
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    int sub1 = 16; fwrite(&sub1, 4, 1, f);
    short afmt = 1; fwrite(&afmt, 2, 1, f);
    short nch = 1; fwrite(&nch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    short blockalign = 2; fwrite(&blockalign, 2, 1, f);
    short bps = 16; fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
    fwrite(data, 2, nsamples, f);
    fclose(f);
}

int main(void) {
    struct gen_params p;
    struct gen_state s;
    memset(&p, 0, sizeof(p));

    p.type = gentype_flex;
    p.ampl = 16000;
    p.p.flex.capcode = 1234567;
    p.p.flex.cycle = 0;
    p.p.flex.frame = 0;
    p.p.flex.errors = 0;
    strncpy(p.p.flex.message, "HELLO FLEX 1234567890", sizeof(p.p.flex.message) - 1);

    gen_init_flex(&p, &s);

    /* Collect all samples */
    int cap = 4 * 1024 * 1024;
    short *buf = malloc(cap * sizeof(short));
    memset(buf, 0, cap * sizeof(short));  /* gen_flex ADDS to *buf, so start at 0 */
    int total = 0;
    int chunk;
    /* gen_flex fills into buf (which it +='s onto). Feed in chunks. */
    while (total < cap) {
        int want = 4096;
        if (total + want > cap) want = cap - total;
        chunk = gen_flex(buf + total, want, &p, &s);
        if (chunk <= 0) break;
        total += chunk;
    }

    printf("Generated %d samples (%.3f sec at %d Hz)\n", total, (double)total / SAMPLE_RATE, SAMPLE_RATE);

    /* Write raw s16 */
    FILE *rf = fopen("gen_flex.s16", "wb");
    fwrite(buf, sizeof(short), total, rf);
    fclose(rf);

    write_wav("gen_flex.wav", buf, total, SAMPLE_RATE);

    printf("Wrote gen_flex.s16 (%d bytes) and gen_flex.wav\n", total * 2);
    free(buf);
    return 0;
}

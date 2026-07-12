#include "FlexDecoder.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <cstdlib>

// Usage: cpp_harness <fixture.s16> [scale] [verbosity]
//   fixture.s16 : path to signed-16-bit LE mono @ 22050 Hz FLEX signal
//   scale       : float multiplier applied to each sample (default 1/32768)
//   verbosity   : FlexDecoder verbosity level (default 3)
// Emits one "CALLBACK ..." line per decoded message so a runner can grep/assert.
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <fixture.s16> [scale] [verbosity]\n", argv[0]); return 2; }
    const char* path = argv[1];

    // Scaling factor: default 1/32768; override via argv[2]
    float scale = 1.0f / 32768.0f;
    if (argc > 2) scale = (float)atof(argv[2]);

    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n = bytes / 2;
    std::vector<int16_t> raw(n);
    if (fread(raw.data(), 2, n, f) != n) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);

    std::vector<float> samples(n);
    for (size_t i = 0; i < n; i++) samples[i] = (float)raw[i] * scale;

    printf("Loaded %zu samples, scale=%g\n", n, scale);

    int verbosity = (argc > 3) ? atoi(argv[3]) : 3;
    flex_next_decoder::FlexDecoder decoder(22050, verbosity);

    int callbacks = 0;
    decoder.setMessageCallback(
        [&](int64_t capcode, int type, const std::string& content) {
            callbacks++;
            printf("CALLBACK capcode=%lld type=%d content=[%s]\n",
                   (long long)capcode, type, content.c_str());
        });

    // Feed in chunks of 1024
    size_t off = 0;
    while (off < n) {
        size_t c = 1024;
        if (off + c > n) c = n - off;
        decoder.processSamples(samples.data() + off, c);
        off += c;
    }

    printf("TOTAL CALLBACKS: %d\n", callbacks);
    return 0;
}

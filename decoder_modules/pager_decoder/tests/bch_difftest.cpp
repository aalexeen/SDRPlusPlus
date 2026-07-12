// Differential test: C++ FlexErrorCorrector::fixErrors gate  vs  reference
// bch_flex_next_correct (bch.c). Verifies the even-parity miscorrection gate
// added for BCH-01 across all 5 reference cases with injected 0/1/2/3-bit errors.
//
// Build:
//   g++ -std=c++17 -I<module>/src bch_difftest.cpp \
//       <module>/src/BCHCode.cpp <ref>/bch.c -o bch_difftest
//
// The C++ side instantiates the real BCHCode and replicates the exact gate
// logic from FlexErrorCorrector.cpp (kept in sync by hand — the class pulls in
// too much FLEX plumbing to link standalone).

#include <cstdint>
#include <cstdio>
#include <array>
#include <vector>
#include "BCHCode.h"

extern "C" {
    unsigned int bch_flex_encode(unsigned int data);
    int bch_flex_next_correct(unsigned int *codeword);
}

static inline int popcnt(uint32_t x) { return __builtin_popcount(x); }

// Add even-parity bit 31 over bits 0-30 to a 31-bit BCH codeword.
static uint32_t add_parity31(uint32_t code31) {
    code31 &= 0x7FFFFFFFu;
    return code31 | (uint32_t(__builtin_parity(code31)) << 31);
}

// ---- Replicate the C++ fixErrors gate exactly (FlexErrorCorrector.cpp) ----
struct CppResult { bool ok; uint32_t data; };

static CppResult cpp_fix(BCHCode& bch, uint32_t data) {
    const uint32_t received_parity_bit = data & 0x80000000u;
    std::array<int,31> received;
    uint32_t temp = data;
    for (int i = 0; i < 31; i++) { received[i] = (temp >> 30) & 1; temp <<= 1; }

    int decode_result = bch.decode(received.data());
    if (decode_result != 0) return { false, 0 };

    uint32_t corrected = 0;
    for (int i = 0; i < 31; i++) { corrected <<= 1; corrected |= received[i]; }

    uint32_t error_mask = (data & 0x7FFFFFFFu) ^ corrected;
    uint32_t errors_fixed = popcnt(error_mask);

    if (errors_fixed > 0 && (popcnt(received_parity_bit | corrected) & 1u))
        return { false, 0 };

    return { true, corrected & 0x1FFFFFu };   // return 21-bit info, like caller masks
}

int main() {
    int poly[6] = { 1, 0, 1, 0, 0, 1 };
    BCHCode bch(poly, 5, 31, 21, 2);

    // Deterministic PRNG (no rand()): xorshift32
    uint32_t rng = 0xC0FFEEu;
    auto next = [&]() { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; };

    long total = 0, agree_ok = 0, disagree = 0;
    long by_nerr_total[4] = {0}, by_nerr_disagree[4] = {0};
    // Track the crucial cases
    long case2_seen = 0, case2_agree = 0;   // bit-31-only flip: both must ACCEPT
    long case4_seen = 0, case4_agree = 0;   // 3-bit: reference rejects -> C++ must too

    const long ITERS = 300000;
    for (long it = 0; it < ITERS; it++) {
        uint32_t data21 = next() & 0x1FFFFFu;
        uint32_t clean = add_parity31(bch_flex_encode(data21));

        // Inject 0..3 bit errors across all 32 positions
        int nerr = it & 3;               // cycle 0,1,2,3
        uint32_t noisy = clean;
        uint32_t flipped = 0;
        int placed = 0;
        while (placed < nerr) {
            int b = next() % 32;
            if (flipped & (1u<<b)) continue;
            flipped |= (1u<<b);
            noisy ^= (1u<<b);
            placed++;
        }

        // Reference
        uint32_t ref_cw = noisy;
        int ref_ret = bch_flex_next_correct(&ref_cw);   // ref_cw -> 21-bit info on success
        bool ref_ok = (ref_ret >= 0);
        uint32_t ref_data = ref_ok ? (ref_cw & 0x1FFFFFu) : 0;

        // C++
        CppResult c = cpp_fix(bch, noisy);

        total++;
        by_nerr_total[nerr]++;

        bool match = (ref_ok == c.ok) && (!ref_ok || ref_data == c.data);
        if (match) { if (ref_ok) agree_ok++; }
        else { disagree++; by_nerr_disagree[nerr]++; }

        // Case 2: exactly bit 31 flipped (nerr==1 && flipped==0x80000000)
        if (nerr == 1 && flipped == 0x80000000u) {
            case2_seen++;
            // both must accept AND recover original data
            if (ref_ok && c.ok && ref_data == data21 && c.data == data21) case2_agree++;
        }
        // Case 4: 3 injected errors — reference decision is ground truth
        if (nerr == 3) {
            case4_seen++;
            if (match) case4_agree++;
        }

        if (!match && disagree <= 20) {
            printf("MISMATCH nerr=%d flip=0x%08X noisy=0x%08X | ref(ok=%d,data=0x%06X) cpp(ok=%d,data=0x%06X)\n",
                   nerr, flipped, noisy, ref_ok, ref_data, c.ok, c.data);
        }
    }

    printf("\n=== BCH-01 differential test ===\n");
    printf("total=%ld  agree(accept+match)=%ld  disagree=%ld\n", total, agree_ok, disagree);
    for (int n = 0; n < 4; n++)
        printf("  %d-bit errors: total=%ld disagree=%ld\n", n, by_nerr_total[n], by_nerr_disagree[n]);
    printf("case2 (bit31-only flip): seen=%ld both-accept-and-recover=%ld\n", case2_seen, case2_agree);
    printf("case4 (3-bit errors): seen=%ld ref/cpp-agree=%ld  (agreement=%.4f)\n",
           case4_seen, case4_agree, case4_seen? (double)case4_agree/case4_seen : 0.0);
    printf("RESULT: %s\n", disagree == 0 ? "PASS (bit-exact match on all cases)"
                                         : "FAIL (see mismatches above)");
    return disagree == 0 ? 0 : 1;
}

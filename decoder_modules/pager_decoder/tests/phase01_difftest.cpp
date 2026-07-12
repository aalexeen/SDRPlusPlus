// PHASE-01 differential test: capcode long-address formula.
//
// Verifies the C++ port in FlexFrameProcessor::processAddressInfoWord against
// the reference demod_flex_next.c:3359-3410, across the whole address-word
// classification space (short + all 3 long-address sets + boundaries).
//
// Both functions below are copied VERBATIM from their respective sources so a
// mismatch reveals a porting error. Like bch_difftest.cpp, this hand-replicates
// the logic — keep it in sync with FlexFrameProcessor.cpp if that changes.
//
// Build/run: see run_tests.sh (phase01 target).

#include <cstdint>
#include <cstdio>

// ---- Reference: verbatim from demod_flex_next.c:3359-3410 --------------------
// Returns capcode; sets *is_long and *valid (valid=0 => reference would skip).
static int64_t ref_capcode(uint32_t aiw, uint32_t w2_in, int* is_long, int* valid) {
    *valid = 1;
    int long_address = (aiw >= 0x000001L && aiw <= 0x008000L) ||   // LA1
                       (aiw >= 0x1F7FFFL && aiw <= 0x1FFFFEL);     // LA2
    *is_long = long_address;
    int64_t capcode = (int64_t)aiw - 0x8000L;  // short address default
    if (long_address) {
        uint32_t w1 = aiw;
        uint32_t w2 = w2_in;
        capcode = 0;
        if (w1 >= 1 && w1 <= 32768 &&
            w2 >= 2064383L && w2 <= 2097150L) {
            capcode = (int64_t)w1 + (int64_t)(2097151L - w2) * 32768LL + 2068480LL;
        } else if (w1 >= 1 && w1 <= 32768 &&
                   w2 >= 1966081L && w2 <= 2031616L) {
            capcode = (int64_t)w1 + (int64_t)(w2 - 1933312L) * 32768LL + 2068480LL;
        } else if (w1 >= 2064383L && w1 <= 2097150L &&
                   w2 >= 1966081L && w2 <= 2031616L) {
            capcode = (int64_t)(w1 - 2064383L) + (int64_t)(w2 - 1867776L) * 32768LL + 2068479LL;
        } else {
            *valid = 0;  // unknown long-address set -> reference does `continue`
        }
    }
    // Reference range check (l.3412): capcode>MAX or <0 => invalid.
    if (capcode > 4297068542LL || capcode < 0) *valid = 0;
    return capcode;
}

// ---- Port under test: verbatim from FlexFrameProcessor::processAddressInfoWord
static int64_t cpp_capcode(uint32_t raw_aiw, uint32_t next_word, int* is_long, int* valid) {
    *valid = 1;
    bool long_address = (raw_aiw >= 0x000001u && raw_aiw <= 0x008000u) ||   // LA1
                        (raw_aiw >= 0x1F7FFFu && raw_aiw <= 0x1FFFFEu);     // LA2
    *is_long = long_address;
    int64_t capcode = static_cast<int64_t>(raw_aiw) - 0x8000;
    if (long_address) {
        uint32_t w1 = raw_aiw;
        uint32_t w2 = next_word;
        capcode = 0;
        if (w1 >= 1 && w1 <= 32768 &&
            w2 >= 2064383u && w2 <= 2097150u) {
            capcode = static_cast<int64_t>(w1)
                      + static_cast<int64_t>(2097151u - w2) * 32768LL + 2068480LL;
        } else if (w1 >= 1 && w1 <= 32768 &&
                   w2 >= 1966081u && w2 <= 2031616u) {
            capcode = static_cast<int64_t>(w1)
                      + static_cast<int64_t>(w2 - 1933312u) * 32768LL + 2068480LL;
        } else if (w1 >= 2064383u && w1 <= 2097150u &&
                   w2 >= 1966081u && w2 <= 2031616u) {
            capcode = static_cast<int64_t>(w1 - 2064383u)
                      + static_cast<int64_t>(w2 - 1867776u) * 32768LL + 2068479LL;
        } else {
            *valid = 0;
        }
    }
    // Mirror the caller's isValidCapcode() (0..MAX_CAPCODE).
    if (capcode > 4297068542LL || capcode < 0) *valid = 0;
    return capcode;
}

int main() {
    // Sweep w1 across every classification boundary and representative interior
    // points; sweep w2 across LA2/LA3/LA4 ranges + out-of-range sentinels.
    const uint32_t w1s[] = {
        0, 1, 2, 100, 32767, 32768, 32769,             // LA1 + just past
        0x008000, 0x008001, 0x100000, 0x1E0000,        // SA range
        0x1E0001, 0x1F0000, 0x1F7FFE,                  // special/reserved
        0x1F7FFF, 0x1F8000, 0x1FFFFE, 0x1FFFFF,        // LA2 + idle
        2064383, 2064384, 2097150, 2097151,            // LA2 numeric boundaries
    };
    const uint32_t w2s[] = {
        0, 1, 1966080, 1966081, 2000000, 2031616, 2031617,   // LA3/LA4 range
        2064382, 2064383, 2080000, 2097150, 2097151,         // LA2 range
        0x1FFFFF,
    };

    long checked = 0, mismatches = 0;
    for (uint32_t w1 : w1s) {
        for (uint32_t w2 : w2s) {
            int rl, rv, cl, cv;
            int64_t rc = ref_capcode(w1, w2, &rl, &rv);
            int64_t cc = cpp_capcode(w1, w2, &cl, &cv);
            checked++;
            if (rc != cc || rl != cl || rv != cv) {
                mismatches++;
                if (mismatches <= 20) {
                    printf("MISMATCH w1=0x%06X w2=0x%07X | ref(cap=%lld long=%d valid=%d) "
                           "cpp(cap=%lld long=%d valid=%d)\n",
                           w1, w2, (long long)rc, rl, rv, (long long)cc, cl, cv);
                }
            }
        }
    }

    printf("PHASE-01 difftest: %ld pairs checked, %ld mismatches\n", checked, mismatches);
    if (mismatches == 0) { printf("PASS  phase01-difftest\n"); return 0; }
    printf("FAIL  phase01-difftest\n");
    return 1;
}

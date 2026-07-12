#!/usr/bin/env bash
# FLEX C++ decoder regression harness (local-only, not for upstream).
#
# Compiles cpp_harness.cpp against the project's FLEX C++ sources STANDALONE
# (no SDR++ core / flog / dsp), runs it over committed golden .s16 fixtures,
# and asserts the decoded output matches known-good strings.
#
# Golden strings capture CURRENT behaviour of the fixed decoder, including one
# KNOWN discrepancy (see aln case). They are a regression oracle, not a spec.
#
# Regenerate fixtures (needs a GPL multimon-ng / gen-ng build, kept out of tree):
#   cc gen_flex_aln.c     -o gen_aln     -lm && ./gen_aln     > golden/aln_1600.s16
#   cc gen_flex_numeric.c -o gen_numeric -lm && ./gen_numeric > golden/numeric_1600.s16
# (see the top of each gen_flex_*.c for the exact capcode/payload they emit)

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
FLEX="$HERE/../src/flex/flex_next_decoder"
BCH="$HERE/../src/BCHCode.cpp"
BUILD="$HERE/build"
mkdir -p "$BUILD"

echo "== compiling cpp_harness (standalone) =="
g++ -std=c++17 -O2 -I"$FLEX" -I"$FLEX/parsers" -I"$HERE/../src" \
    "$HERE/cpp_harness.cpp" \
    "$FLEX"/*.cpp "$FLEX"/parsers/*.cpp "$BCH" \
    -o "$BUILD/cpp_harness" || { echo "BUILD FAILED"; exit 1; }

pass=0; fail=0

# check <name> <fixture> <expected-substring>
check() {
    local name="$1" fixture="$2" expect="$3"
    local out
    out="$("$BUILD/cpp_harness" "$HERE/golden/$fixture" 2>/dev/null)"
    if grep -qF -- "$expect" <<<"$out"; then
        echo "PASS  $name"
        pass=$((pass+1))
    else
        echo "FAIL  $name"
        echo "      expected substring: $expect"
        echo "      --- actual output ---"
        sed 's/^/      /' <<<"$out"
        fail=$((fail+1))
    fi
}

# check_nodecode <name> <fixture>: assert the page is DROPPED (no message
# callback emitted) — used for address/vector-word corruption where the
# reference multimon-ng also drops the whole page.
check_nodecode() {
    local name="$1" fixture="$2"
    local n
    n="$("$BUILD/cpp_harness" "$HERE/golden/$fixture" 2>/dev/null | grep -c '^CALLBACK capcode')"
    if [ "$n" -eq 0 ]; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name (expected 0 message callbacks, got $n)"; fail=$((fail+1))
    fi
}

# check_drop <name> <fixture> <expected-DROP-substring>: assert the diagnostic
# callback fired with the expected dropped-page reason (0-callback + a DROP line).
# This is what makes "dropped pages are logged" a test, not a claim.
check_drop() {
    local name="$1" fixture="$2" expect="$3"
    local out
    out="$("$BUILD/cpp_harness" "$HERE/golden/$fixture" 2>/dev/null)"
    if grep -qF -- "$expect" <<<"$out"; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name"
        echo "      expected DROP substring: $expect"
        echo "      --- actual output ---"
        sed 's/^/      /' <<<"$out"
        fail=$((fail+1))
    fi
}

# check_nodrop <name> <fixture>: assert a CLEAN fixture produces ZERO drop
# diagnostics (guards against false-positive drop logging / idle-skip leaking).
check_nodrop() {
    local name="$1" fixture="$2"
    local n
    n="$("$BUILD/cpp_harness" "$HERE/golden/$fixture" 2>/dev/null | grep -c '^DROP ')"
    if [ "$n" -eq 0 ]; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name (expected 0 drop diagnostics, got $n)"; fail=$((fail+1))
    fi
}

echo "== running golden fixtures =="
# ALN: capcode 1234567, payload "HELLO FLEX 1234567890".
# NOTE: frag field "0.0.C" is CURRENT behaviour; multimon-ng emits "3.0.K"
# (known open FRAG-flag discrepancy). This asserts our behaviour, not correctness.
check "aln-1600"     aln_1600.s16     "HELLO FLEX 1234567890"
# NUM: payload "1234567890.123" — all 13 digits (PARSE-01) + '.' (PARSE-03) + PARSE-12.
check "numeric-1600" numeric_1600.s16 "1234567890.123"
# DROP-01: one content word made uncorrectable → its 3 chars become '?', rest
# of the message survives (page not dropped). Byte-matches reference multimon-ng.
# Regenerate: see gen_flex_aln_corrupt.c (errors=99 mode).
check "aln-corrupt"  aln_corrupt_1600.s16 "HE??? FLEX 1234567890"
# DROP-01 numeric: one BCD body word uncorrectable → its digits become '?',
# digit alignment across words preserved. Byte-matches reference multimon-ng.
# Regenerate: see gen_flex_numeric_corrupt.c (errors=99 mode).
check "numeric-corrupt" numeric_corrupt_1600.s16 "1234??????.123"
# PARSE-11: 0x0C space-fill BCD digit must be OUTPUT (not skipped), else digit
# alignment desyncs. Payload "1234 67890.123" → space preserved. Byte-matches
# reference multimon-ng. Regenerate: gen_num_clean "1234 67890.123".
check "numeric-space" numeric_space_1600.s16 "1234 67890.123"
# SHORT-NUM-01: a short numeric page (word count so small the vector n field
# decrements to 0) must still decode. It was rejected by a message_length>0
# gate that only applies to Alphanumeric. Payload "12 34". Regenerate:
# gen_num_clean "12 34".
check "numeric-short" numeric_short_1600.s16 "NUM|12 34"
# DROP-01 frame gates: a corrupt ADDRESS or VECTOR word must drop the whole
# page (page boundaries/recipient unknown), matching reference multimon-ng which
# also emits nothing. Exercises the address-gate (FlexFrameProcessor l.115) and
# vector-gate (l.145). Regenerate: gen_flex_aln_corrupt.c "addr" / "vec".
check_nodecode "aln-addr-corrupt" aln_addr_corrupt_1600.s16
check_nodecode "aln-vec-corrupt"  aln_vec_corrupt_1600.s16
# Diagnostic-logging: the same dropped pages must be surfaced via the diagnostic
# callback (decoder_next.h tees this to $SDRPP_FLEX_LOG_DIR and the GUI list).
# Without this the decoder is silent exactly when a message is lost.
check_drop "addr-corrupt-diag" aln_addr_corrupt_1600.s16 "DROP drop|A|idx=1|bad-address-word"
check_drop "vec-corrupt-diag"  aln_vec_corrupt_1600.s16  "DROP drop|A|vidx=2|bad-vector-word|cap=1234567"
# A clean signal must produce ZERO drop diagnostics (idle skips are not logged).
check_nodrop "aln-clean-nodrop"     aln_1600.s16
check_nodrop "numeric-clean-nodrop" numeric_1600.s16

# PHASE-01: differential test of the capcode long-address formula (no audio —
# pure arithmetic swept across the address classification space).
echo "== phase01 capcode differential test =="
if g++ -std=c++17 -O2 "$HERE/phase01_difftest.cpp" -o "$BUILD/phase01_difftest" 2>/dev/null; then
    if "$BUILD/phase01_difftest" | grep -q "^PASS"; then
        echo "PASS  phase01-difftest"; pass=$((pass+1))
    else
        echo "FAIL  phase01-difftest"; "$BUILD/phase01_difftest" | tail -3; fail=$((fail+1))
    fi
else
    echo "FAIL  phase01-difftest (build)"; fail=$((fail+1))
fi

# FlexFileLogger: unit-test the std-only file-logging layer (decoder_next.h's
# writer) — open/append/flush when enabled, no-op + graceful when not. The GUI
# wrapper can't compile standalone, so this covers the last unverified layer.
echo "== flex file logger unit test =="
if g++ -std=c++17 -O2 -I"$FLEX" "$HERE/filelogger_test.cpp" -o "$BUILD/filelogger_test" 2>/dev/null; then
    if "$BUILD/filelogger_test" "$BUILD" | grep -q "^PASS"; then
        echo "PASS  filelogger"; pass=$((pass+1))
    else
        echo "FAIL  filelogger"; "$BUILD/filelogger_test" "$BUILD" | tail -6; fail=$((fail+1))
    fi
else
    echo "FAIL  filelogger (build)"; fail=$((fail+1))
fi

echo "== $pass passed, $fail failed =="
[ "$fail" -eq 0 ]

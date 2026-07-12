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

# check_no_bleed <name> <fixture>: hard assert that NO ALN line contains a run
# of raw hex words (an 8-hex-digit followed by another) — the signature of the
# FRAG content-bleed bug. The fixture is a real off-air 3200/4FSK capture whose
# fragmented pages USED TO leak ~32 raw signature/binary words into ALN text;
# multimon-ng decodes the SAME samples with zero bleed. Fixed in Stage 1 (the
# broken fragment-assembly facade in FlexMessageDecoder::processFragment now
# passes fragments through cleanly instead of dumping a shared collided buffer).
# This was previously an XFAIL; promoted to a hard check once the leak stopped.
check_no_bleed() {
    local name="$1" fixture="$2"
    local bleed
    bleed="$("$BUILD/cpp_harness" "$HERE/golden/$fixture" 2>/dev/null \
             | grep '|ALN|' | grep -cE '[0-9A-F]{8} [0-9A-F]{8}')"
    if [ "$bleed" -eq 0 ]; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name (FRAG content-bleed regressed: $bleed ALN line(s) with hex leak)"; fail=$((fail+1))
    fi
}

# check_count <name> <fixture> <expected-callback-count>: assert the exact
# number of message callbacks. Fragment reassembly must NOT change the message
# count — each fragment still emits one line (the completing 'C' fragment's line
# just grows to hold the reassembled text). A changed count means a fragment was
# swallowed or duplicated.
check_count() {
    local name="$1" fixture="$2" want="$3"
    local n
    n="$("$BUILD/cpp_harness" "$HERE/golden/$fixture" 2>/dev/null | grep -c '^CALLBACK capcode')"
    if [ "$n" -eq "$want" ]; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name (expected $want message callbacks, got $n)"; fail=$((fail+1))
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
# FRAG content-bleed: real off-air 3200/4 capture (capcode 6259133, an F+C
# fragment pair). Our decoder USED TO leak ~32 raw signature/binary words as hex
# into the ALN text + duplicate the header ("NFrom: rths@careawame:"); the Stage 1
# fix stops the leak so each fragment now shows its own clean text (no reassembly
# yet — that is Stage 2). Capture window [74s,90s] of a 127s live capture; needs
# history from ~76s for the (former) bleed to reproduce (it is stateful — a
# shorter window loses it), so this fixture also guards the whole fragment path.
check_no_bleed "frag-bleed-3200" frag_bleed_3200.s16
# FRAG reassembly (Stage 2): the SAME fixture carries two interleaved fragment
# streams both with msg_n=0 across frames 074-076 — capcode 6259133 (2-fragment
# F+C, clean K+/SIG+) and 7303131 (3-fragment F+F+C). Keying the store on
# (capcode, type, msg_n) separates them; keying on capcode=0 (the Stage-1 bug)
# or msg_n alone re-collides them → bleed. The completing 'C' fragment's line
# must carry the reassembled text.
#   - 6259133: reassembled content is BYTE-IDENTICAL to multimon-ng's C.1/0 line.
check "frag-reassemble-6259133" frag_bleed_3200.s16 \
      "From: rths@careaware.com Subject: RTHS Alert - RQSTD (T): Name: MILLS, MARK Age: 62 years FIN: 16329399 Gender: Male Origin Unit: Regional Emergency Department Admitting: Attending: Ahmed Bendary DO Level of Care:  [14]"
#   - 7303131 (3-fragment, K- so its tail has uncorrectable '?' words): assert the
#     178-char CLEAN reassembled prefix (F+F+C chained). The corrupted tail past
#     char 178 diverges from multimon only inside '?'-runs — that is the known
#     BCH permissive residual (BCH-02, deferred), NOT a reassembly defect (both
#     strings are 214 chars, so word alignment/boundaries match exactly).
check "frag-reassemble-7303131-prefix" frag_bleed_3200.s16 \
      "From: rths@careaware.com Subject: RTHS Alert - RQSTD (T): Name: MILLS, MARK Age: 62 years FIN: 16329399 Gender: Male Origin Unit: Regiona, e"
# Reassembly must not change the message count: still 11 callbacks (each fragment
# emits its own line; only the completing 'C' line grows).
check_count "frag-count-unchanged" frag_bleed_3200.s16 11
# BIN/HEX parser (PARSE-06): capcode 8278737 is a complete (K) HEX page. The old
# BinaryParser dumped raw %08X words; the rewrite unpacks the FLEX HEX body —
# continuous LSB-first 4-bit nibbles across 21-bit words, hdr2 skip on the
# initial fragment, and termination-fill stripping — so the content is
# BYTE-IDENTICAL to multimon-ng's HEX line (160 nibbles). Also exercises the
# type-dependent fragment-header layout fix (Binary: C@bit12/F@13-14/N@15-20 vs
# alpha C@10/F@11-12/N@13-18) — without it this page mis-reads as frag=1/cont=1.
check "bin-hex-unpack" frag_bleed_3200.s16 \
      "F41172D5D84227F509AEC344AA238AF38BEF13B7AEA09C33ED130E813466B460E75CE11F23D740D1D18243781D8B14F1413CB233CE5329AE288F9AC480F34CFC005FD64DB3C778B96F23D3A1DB68C72A"

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

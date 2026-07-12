# FLEX C++ decoder — local regression harness

Local-only (not upstream). Compiles the FLEX C++ decoder **standalone** (no SDR++
core/flog/dsp) against committed golden `.s16` fixtures and asserts decoded output.

## Run
```
./run_tests.sh
```
Builds `build/cpp_harness` from `../src/flex/flex_next_decoder/*.cpp` + `parsers/*.cpp`
+ `../src/BCHCode.cpp`, feeds each `golden/*.s16` through `FlexDecoder(22050,3)` via
`processSamples` + `setMessageCallback`, greps the callback lines.

## Test suite (9 checks, all cross-checked vs reference multimon-ng)
| test | fixture | asserts |
|---|---|---|
| `aln-1600` | `aln_1600.s16` | ALN path, BCH clean-path no-regression |
| `numeric-1600` | `numeric_1600.s16` | PARSE-01 (13 digits), PARSE-03 (`.`), PARSE-12 (vector index) |
| `aln-corrupt` | `aln_corrupt_1600.s16` | DROP-01: bad content word → `HE???`, page survives |
| `numeric-corrupt` | `numeric_corrupt_1600.s16` | DROP-01 numeric: bad body word → `1234??????.123`, alignment kept |
| `numeric-space` | `numeric_space_1600.s16` | PARSE-11: 0x0C space-fill output (`1234 67890.123`) |
| `numeric-short` | `numeric_short_1600.s16` | SHORT-NUM-01: short numeric decodes (`NUM\|12 34`) |
| `aln-addr-corrupt` | `aln_addr_corrupt_1600.s16` | address-word gate → page dropped (0 callbacks) |
| `aln-vec-corrupt` | `aln_vec_corrupt_1600.s16` | vector-word gate → page dropped (0 callbacks) |
| `phase01-difftest` | (no audio) | PHASE-01 capcode formula vs reference over 286 (w1,w2) pairs |

Golden strings are the **current** behaviour of the fixed decoder — a regression
oracle, not a spec. Known quirk: ALN frag field is `0.0.C`; multimon-ng emits `3.0.K`
(open FRAG-flag discrepancy).

## Regenerate fixtures (needs GPL multimon-ng/gen-ng, kept out of tree)
Generators = wrapper (`gen_flex_*.c`, has `main`) + engine (`*_engine_corrupt.c`, has
`gen_flex()`), linked with `pager_reference/bch.c`. Engines add test-only error modes:
`errors==99` corrupts one content/body word, `98` the address word, `97` the vector word
(5 bit flips = guaranteed-uncorrectable). All emit 1600-baud / 2FSK @ 22050 Hz.
```
REF=../../../../pager_reference   # pager_reference/
gcc -O2 -I$REF gen_flex_aln.c          $REF/gen_flex.c              $REF/bch.c -lm -o build/gen_aln
gcc -O2 -I$REF gen_flex_aln_corrupt.c  gen_flex_engine_corrupt.c    $REF/bch.c -lm -o build/gen_aln_corrupt
gcc -O2 -I$REF gen_flex_numeric_corrupt.c gen_flex_numeric_engine_corrupt.c $REF/bch.c -lm -o build/gen_numeric_corrupt
# then: ./build/gen_aln_corrupt [addr|vec];  ./build/gen_numeric_corrupt "<payload>"
```

## Not runtime-covered yet
baud-3200 (A2/A4/A7); Tone parser (no tone generator — corrected but unverified);
FRAG reassembly + PARSE-06/binary (generator can't emit fragmented or binary msgs);
MULTI-PAGE skip index-safety (all corrupt fixtures are single-page; positional
`vector_index` vs C's skip-compacted `vec_used` = latent PHASE-02); long-address
audio path (PHASE-01 formula is difftested, but no long-address generator).
`bch_difftest.cpp` hand-replicates the BCH gate; `phase01_difftest.cpp` hand-replicates
the capcode formula — both rot if their source changes; keep in sync.

# Stage 08 — Message content parsers (C → C++ correctness comparison)

**Verdict.** The C++ parsers reproduce the *core bit-unpacking skeletons* correctly
(alpha 7-bit char order incl. the initial-fragment signature skip; tone short-numeric
bit positions; numeric shift-register digit logic and header-skip counts), but they are
**functionally incomplete and contain at least two 🔴 arithmetic/data-table bugs that
corrupt output**. The single largest structural gap is that `MessageParseInput` carries
**no per-word BCH error array at all**, so *every* C++ parser has silently dropped the C
decoder's `'?'`-substitution for damaged words — damaged messages emit plausible-looking
garbage instead of flagged corruption. In addition: (1) `NumericParser`'s word-count
arithmetic collapses to zero because it shifts an already-masked value, truncating every
numeric page to ~1 word; (2) the `FLEX_BCD` table differs from C at index 10 (`' '` vs
`'.'`); (3) `NumericParser` drops the `0x0C` space-fill digit that C deliberately keeps,
misaligning digit groups; (4) `BinaryParser` is a raw 8-hex-per-word dump with none of C's
nibble bit-stream unpacking, termination-fill stripping, or signature check; (5) **no K
checksum or signature (S) verification exists in any C++ parser**; (6) `ToneParser`
collapses C's 4-subtype `parse_short_message` (numeric / source / numbered / network-ID /
reserved) into "short-numeric or empty". `FlexMessageDecoder`'s fragment reassembly is
also a stub (hardcoded `capcode = 0`). C is ground truth throughout.

## Findings

| ID | Sev | C ref (file:line) | C++ (file:line) | Description | Fix suggestion |
|----|-----|-------------------|------------------|-------------|----------------|
| PARSE-01 | 🔴 | demod_flex_next.c:2239-2242 | NumericParser.cpp:27-31 | **Numeric word-count field is always 0.** C reads the *original* vector word twice: `w1 = phaseptr[j] >> 7; w2 = w1 >> 7; w1 &= 0x7f; w2 = (w2 & 0x07) + w1;` — so `w2 = start + (3-bit word_count)`. C++ does `w1 = (vector_word>>7) & 0x7F;` **then** `w2 = (w1>>7) & 0x07;`. Because `w1` is already masked to 7 bits, `w1>>7 == 0` always, so `w2 = 0 + w1 = w1`. Consequence: word count collapses to start; after the short-address `w2++` the loop runs a single word → every numeric message truncates to ~4 digits regardless of true length. | Recompute from the raw word without pre-masking: `uint32_t raw=vector_word>>7; uint32_t w2c=(raw>>7)&0x07; w1=raw&0x7F; w2=w2c+w1;` |
| PARSE-02 | 🔴 | demod_flex_next.c:1921-1928, 2351-2352, 2464-2475, 2652-2662 | (absent — no bch array in MessageParseInput, IMessageParser.h:14-54) | **No damaged-word `'?'` substitution anywhere.** C tracks `bch_err[]` per word and emits `'?'` for every uncorrectable word (3× `'?'` per alpha word, `'?'` per lost numeric digit, 5× `'?'` for a lost long-addr short-numeric 2nd word, `'?'` nibbles for lost binary words). `MessageParseInput` has no error/`bch_err` field, so all four C++ parsers read `phase_data[...]` unconditionally and emit garbage bits as if valid. Also drives K/S "fail on BCH error" logic (PARSE-05) which is likewise absent. | Add `const int* bch_err` (or `std::vector<bool>`) to `MessageParseInput`; in each parser check it per word and substitute `'?'` exactly as C does. |
| PARSE-03 | 🔴 | demod_flex_next.c:2230 (`"0123456789.U -]["`) | FlexTypes.h:36-37 | **FLEX_BCD table wrong at index 10.** C index 10 = `'.'` (ARIB "spare"); C++ index 10 = `' '`. Indices 0-9,11,13,14,15 match; index 12 = `' '` matches. Any numeric/short digit with nibble `0x0A` renders as space instead of `.`. (C is ground truth; note multimon-ng historically used space here, but this port must match its own reference C.) | Change `FLEX_BCD[10]` from `' '` to `'.'`. |
| PARSE-04 | 🟠 | demod_flex_next.c:2353-2358 (comment: "Output all BCD digits including space fill (0x0C). Do NOT skip") | NumericParser.cpp:77 | **NumericParser drops the 0x0C fill digit; C keeps it as space.** C emits `flex_bcd[digit]` for *every* group (0x0C → index 12 → `' '`). C++ guards `if (digit != BCD_FILL_CHAR && digit < FLEX_BCD.size())`, skipping 0x0C entirely, so `"555 1234"` becomes `"5551234"` and any embedded spacing is lost. (Also internally inconsistent: `ToneParser::extractShortNumeric` does *not* skip 0x0C, matching C.) | Remove the `digit != BCD_FILL_CHAR` skip; emit `FLEX_BCD[digit]` for all groups, as C and as ToneParser already do. |
| PARSE-05 | 🔴 | demod_flex_next.c:1862-1899 (alpha K), 1958-1971 (alpha sig S), 2280-2326 (numeric K), 2730-2760 (HEX sig S) | (absent in all parsers) | **No K checksum or signature (S) verification.** C computes and reports K (`.K+`/`.K-`) for alpha and numeric, and the 7-bit alpha signature / 8-bit HEX signature. None of the C++ parsers compute or expose any checksum/signature; `MessageParseResult` has no field for it. Corrupt-but-BCH-clean messages are reported as clean. | Port K/S computation into each parser; add `k_fail`/`sig_fail` fields to `MessageParseResult` and surface in output formatting. |
| PARSE-06 | 🔴 | demod_flex_next.c:2559-2760 (nibble bitstream + termination-fill strip + sig) | BinaryParser.cpp:34-47, 95-102 | **BinaryParser is a raw hex-word dump, not a HEX/binary decoder.** C parses hdr2 control fields (R/M/D/H/B/I/S), unpacks a continuous 4-bit-nibble LSB-first bit stream across 21-bit words, strips termination fill (rules 2 & 3), and validates the 8-bit signature. C++ merely prints each 32-bit word as `%08X` space-separated. Output bears no resemblance to C's HEX text for real binary pages. | Reimplement per C: skip/parse hdr2 on initial fragment, extract 21-bit-per-word nibble stream, strip fill, emit `0-9A-F` nibbles, verify S. |
| PARSE-07 | 🟠 | demod_flex_next.c:2402-2556 (parse_short_message: 4 sub-types) | ToneParser.cpp:31-41 | **ToneParser handles only sub-type 0; C handles 4.** C `parse_short_message` switches on bits 7-8: t=0 numeric **or Network-ID (addr_type=='N')**, t=1 Source code (SRC), t=2 Numbered (S/N/R), t=3 Reserved. C++ only decodes t=0 as short-numeric and treats t≠0 as empty "tone-only", losing SRC/Numbered/Reserved/Network-ID entirely. Also the "all-space ⇒ tone-only" distinction (C 2455-2487) is not reproduced. | Port the full 4-way switch and the Network-ID / all-space branches from `parse_short_message`. |
| PARSE-08 | 🟠 | demod_flex_next.c:182-192 (enum), 3966-4024 (dispatch) | FlexTypes.h:56-65; FlexMessageDecoder.cpp:358-374 | **Message-type enum/dispatch mismatch for value 2.** C enum value 2 = `FLEX_PAGETYPE_SHORT_MESSAGE` → `parse_short_message`; C++ names value 2 `Tone` → `ToneParser`. Values 0,1,3,4,5,6,7 match C exactly. C also routes `SECURE`(0) to alpha-or-binary by sub-type t (3979-3998) and has a synthetic `TONE_ONLY`(8) with no vector; C++ maps `Secure`→AlphanumericParser only (no binary sub-branch) and has no value-8 handling. Net: value-2 semantics (short-message vs tone) and secure-binary sub-routing diverge. | Rename `Tone`→`ShortMessage` for clarity, implement the secure t-subtype alpha/binary split, and confirm the synthetic tone-only path. |
| PARSE-09 | 🔴 | demod_flex_next.c:1973-2046 + frag_store (frag_find/alloc/append keyed by capcode+type+msg_n) | FlexMessageDecoder.cpp:107-147 | **Fragment reassembly is a non-functional stub.** C keys fragment slots on capcode+type+msg_n with F-sequence checking and gap markers. C++ `processFragment` hardcodes `int64_t capcode = 0;` (line 112, comment admits it), so every fragment stream collides in one bucket, and it emits `"[Fragment N buffered]"` placeholder text instead of real reassembled content. | Thread the real capcode (and type/msg_n) from `MessageParseInput` into `processFragment`; key buffers on that tuple; assemble content per C. |
| PARSE-10 | 🟡 | demod_flex_next.c:1956 (`message[currentChar]='\0'`, no trimming) | FlexMessageDecoder.cpp:328-345 (`postProcessMessage` trims) | **Post-parse whitespace trimming not in C.** `postProcessMessage` strips leading/trailing `" \t\r\n"` from all content. C emits content verbatim (numeric spacing is significant, PARSE-04). Trimming can alter numeric/alpha output and interacts badly with checksum-relevant spacing. | Drop the trim, or restrict it to a mode that never runs for numeric/short messages. |

## Sub-parts that MATCH C (verified correct ports)

- **Alpha 7-bit char extraction order & count** — AlphanumericParser.cpp:36-47 matches
  demod_flex_next.c:1929-1954: char1 = `dw & 0x7F`, char2 = `>>7 & 0x7F`, char3 =
  `>>14 & 0x7F`, 3 chars/word. The initial-fragment signature skip
  (`if (i > 0 || fragment_number != 0x03)` skips char1 on word 0) correctly mirrors C's
  `if (i == 0 && is_initial)` branch that consumes bits 0-6 as the signature.
- **`add_ch` / `addCharacterSafe`** — IMessageParser.cpp:52-82 matches
  demod_flex_next.c:1510-1563: `\t`→`\\t`, `\n`→`\\n`, `\r`→`\\r`, `%`→`%%`, store only
  printable 0x20-0x7E, drop others (incl. ETX 0x03).
- **`calculateFragmentFlag` (K/C/F/?)** — IMessageParser.cpp:25-34 matches
  demod_flex_next.c:1840-1843: `cont==0 & frag==3`→K, `cont==0 & frag!=3`→C, `cont==1`→F.
- **Numeric header-skip counts** — NumericParser.cpp:57-61 matches
  demod_flex_next.c:2336-2340: `+10` bits for NumberedNumeric, `+2` otherwise
  (initial `count = 4`).
- **Numeric shift-register digit logic** — NumericParser.cpp:66-81 matches
  demod_flex_next.c:2344-2361: `digit=(digit>>1)&0x0F; if(dw&1) digit^=0x08; dw>>=1;`
  emit every 4th bit; 21 bits/word. (Bug PARSE-04 is only in the *emit filter*, not this
  shift logic.)
- **Numeric next-word reload** — NumericParser.cpp:85-87 (`phase_data[word_index]`)
  matches C's `phaseptr[i]` (demod_flex_next.c:2370), given w1 was pre-incremented.
- **Numeric first-word selection & short/long-addr handling** — NumericParser.cpp:43-50
  matches demod_flex_next.c:2257-2265 (short: `dw=phaseptr[w1]; w1++; w2++`; long:
  `dw=phaseptr[j+1]`). Note NumericParser recomputes from `vector_word_index` and does
  **not** consume the upstream `message_length`, so PHASE-03's vector-length bug does not
  reach it — PARSE-01 is a *separate*, local bug.
- **ToneParser short-numeric bit positions** — ToneParser.cpp:73-93 matches
  demod_flex_next.c:2458-2469: short-addr digits at bits 9/13/17 (`for i=9;i<=17;i+=4`),
  long-addr 2nd word digits at bits 0..16 (`for i=0;i<=16;i+=4`).
- **Message-type enum values 0,1,3,4,5,6,7** — FlexTypes.h:56-65 match the C enum
  (demod_flex_next.c:182-192). Only value 2 diverges (PARSE-08).

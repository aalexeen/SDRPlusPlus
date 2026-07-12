# FLEX Decoder: C original ↔ C++ port comparison

**Reference (ground truth, works):** `pager_reference/demod_flex_next.c` (4925 lines,
full ARIB STD-43A, Vasyl Samoilov mods). Confirmed as our port source (`ARIB` appears
15× here, 0× in `demod_flex.c`).

**Port (suspect):** `decoder_modules/pager_decoder/src/flex/flex_next_decoder/` (+ `parsers/`).

**Goal:** independently decompose both sides, map stage-by-stage, find translation bugs,
perf issues, and crash causes. SDR++ reportedly crashed often on this module.

**Method:** C file is the spine. Compare down to constants / bit-ops, not prose.
Severity: 🔴 correctness bug · 🟠 likely bug / risky · 🟡 perf or style · 🟢 intentional/OK.

---

## Stage map (C function → C++ class/method)

| # | Stage | C (demod_flex_next.c) | C++ |
|---|-------|----------------------|-----|
| 1 | Sample intake, symbol build | `buildSymbol`(4642), `flex_sym`(4397 dispatcher) | `FlexDemodulator::buildSymbol/finalizeSymbol`; `FlexDecoder::processSample/processSymbol` |
| 2 | Sync detect + mode/speed | `flex_sync_check`(1246), `flex_sync`(1275), `decode_mode`(1295), `read_2fsk`(1335) | `FlexSynchronizer::processSymbol/checkSyncPattern/decodeSyncMode/isValidSyncCode/getSyncModeInfo` |
| 3 | State machine Sync1→FIW→Sync2→Data | states in `flex_sym`/`read_data`(4271) | `FlexStateMachine` + `Sync1/FIW/Sync2/DataState`; `FlexDecoder::handle*State` |
| 4 | FIW parse (cycle/frame/checksum) | `decode_fiw`(1341) | `FlexDecoder::handleFIWState` (l.225+, "from original decode_fiw") |
| 5 | Phase data collect / deinterleave | `read_data`(4271), `decode_data`(4162), `clear_phase_data`(4125) | `FlexDataCollector::processSymbol/symbolToBits/updatePhaseBuffers/calculateBufferIndex/checkForIdlePatterns` |
| 6 | BCH(31,21) error correction | `count_bits`(1202), `bch3121_fix_errors`(1220), `bch.h` | `FlexErrorCorrector::fixErrors/countBits` + `BCHCode.cpp` |
| 7 | Phase dispatch (word types) — **1200 lines, biggest risk** | `decode_phase`(2908–4125) | `FlexFrameProcessor::processFrame/processPhase` + BIW/AIW/VIW structs + `handleShortInstruction` |
| 8 | Message parsers | `parse_alphanumeric`(1827), `parse_numeric`(2224), `parse_short_message`(2402), `parse_binary`(2559) | `parsers/{Alphanumeric,Numeric,Tone,Binary}Parser` + `FlexMessageDecoder` |
| 9 | Fragments + dedup | `frag_*`(1572–1730), `dedup_check_words`(1730) | `FlexMessageDecoder::FragmentBuffer/processFragment/clearFragmentBuffers` |
| 10 | Group messages | `flex_group_endpoint`(890) | `FlexGroupHandler` |
| 11 | **OTA time decode (BIW date/time)** | `flextime_*`(502–890), `flextime_emit`(902) | ⚠️ **NOT PORTED** (grep empty) — see D-001 |
| 12 | Output + address type | `flex_next_json_emit`(1000), `addr_type_char`(979) | `FlexOutputFormatter::outputMessage` |

---

## Discrepancy table

| ID | Stage | Severity | C behavior | C++ behavior | Status |
|----|-------|----------|-----------|--------------|--------|
| D-001 | 11 | 🟢? | Decodes OTA date/time from BIW, votes across frames, emits timestamp | Entire `flextime_*` block absent | Open — likely intentional omission (time not essential to message content); confirm with user |
| **BCH-01** | 6 | 🔴 **CONFIRMED** | `bch_flex_next_correct` (`bch.c:511-547`) uses bit 31 (even parity over bits 0-30) to reject BCH miscorrections: after `code31 ^= error`, if `parity32((*cw & 0x80000000) \| code31)` is odd → return -1 (3+ real errors) | `FlexErrorCorrector::fixErrors` strips bit 31 at intake (`.cpp:59-62` reads only bits 30..0; masks `& 0x7FFFFFFF`) → parity gate absent → false corrections silently accepted, corrupting many words/phase | **Root cause of "messages incorrect".** Fix: after successful `decode`, recombine bit 31 and reject if 32-bit popcount is odd (mirror `bch.c:541-543`). Verified against `bch.c` + `bch.h:82-103` by main agent. |
| **BCH-02** | 6 | 🔴 CONFIRMED | Same func: `key==0` but bit31 parity bad → "only bit 31 wrong", return 1 (`bch.c:522-525`) | Case absent (bit 31 discarded) | Same fix covers it |
| BCH-03 | 6 | 🟡 | C returns -1/0/1/2 (error count) | C++ `decode` collapses to 0/1; `fixErrors` returns bool | Currently harmless — all call sites use only bool; but the C I&D/alt-phase merge that branches on the count has NO C++ equivalent (see decode_phase agent) |
| BCH-04..08 | 6 | 🟢 | primitive poly, bit ordering, countBits (byte-for-byte), compiled path = real `BCHCode.cpp` not stub | match | OK |

| **DATA-01** | 5 | 🔴 ✅ **FIXED** | `decode_mode` sets `Sync.baud`/`Sync.levels` from the sync-code table (`demod_flex_next.c:1306-1322`); data collection branches on them | `FlexDataCollector::baud_rate_` stuck at `BAUD_1600` — the sync_info assignment was commented out (`FlexDataCollector.cpp:31`) while `fsk_levels_` IS refreshed | **FIXED 2026-07-12: `baud_rate_ = sync_info.baud_rate;` at the top of `processSymbol`.** Confirmed `SyncInfo.baud_rate` holds SYMBOL rate (`FLEX_MODES` table `FlexTypes.h:46-51`: A3=1600/4, A2=3200/2, A4=3200/4) matching C's `Sync.baud`. Builds clean. |
| **DATA-03** | 4 | 🔴 CONFIRMED | C uses BCH-corrected FIW everywhere | `handleFIWState` extracts cycle/frame from `corrected_fiw` (`FlexDecoder.cpp:246-247`) ✓ but never stores it back; then `processCompletedFrame` RE-extracts from raw `fiw_raw_data_` (`.cpp` processCompletedFrame l.6-7) → uses uncorrected cycle/frame for phase processing. Two internal paths disagree | Fix: store corrected FIW back into `fiw_raw_data_` (or pass corrected value through). |
| ~~DATA-02~~ | 5 | 🟢 **DOWNGRADED (not a bug)** | C routes A3's 2nd sub-phase bit to Phase **C** | C++ routes it to Phase **B**, BUT does so *consistently*: producer `updatePhaseBuffers`→B, idle-check `areAllActivePhasesIdle`→{A,B}, and final parser `getActivePhasesForMode`→{A,B} all agree | **Not a bug — internally self-consistent buffer-naming convention.** The B-vs-C label is cosmetic; all three A3 consumers use {A,B}, so A3 decodes correctly. "Fixing" to C's {A,C} would require changing all three in lockstep for zero functional gain and real regression risk. Verified all 3 consumers 2026-07-12. |
| DATA-04 | 3 | 🟠 | C `handleSync2State`: S2 = C + inv.C correlation + ±1 symbol boundary correction | C++ `handleSync2State` is a blind symbol-count stub | Belongs to SYNC2 stage; deep review pending (demod-sync agent). |
| DATA-05 | 3 | 🟡 | — | `FlexStateMachine` State-Pattern `processSymbol` path is DEAD CODE; live dispatch is `FlexDecoder::handle*State` | Cleanup opportunity, not a bug. |
| DATA-06 | 4 | 🟠 | `decode_fiw` consumes repeat/roaming/traffic/num_tx for subframe retransmission | C++ `decode_fiw` drops them | May affect multi-tx handling; low priority. |

| **PHASE-01** | 7 | 🔴 CONFIRMED | `_next` uses ARIB range-table long-address scheme: `long_address` iff `aiw∈[0x1,0x8000]` (LA1) OR `[0x1F7FFF,0x1FFFFE]` (LA2); capcode via per-set formulas (`demod_flex_next.c` decode_phase +l.452-505, e.g. `w1-2064383`, distinct sets) with 64-bit range check `>4297068542` | C++ uses the OLD `demod_flex.c` PDW formula: `capcode = (next_word ^ MESSAGE_BITS_MASK)<<15; += LONG_ADDRESS_CONSTANT + raw_aiw` (`FlexFrameProcessor.cpp:288-297`) + small `MAX_CAPCODE` | **Wrong long-address capcodes → messages attributed to wrong pagers.** Ported the formula from the wrong C file. Fix: reimplement the `_next` range-table + per-set arithmetic. |
| ~~PHASE-03~~ | 7 | 🟢 **DOWNGRADED (not a bug in practice)** | `_next` uses type-dependent width (`:786-792`): numeric (V=011/100/111) → 3-bit `((viw>>14)&0x07)+1`; alpha/hex/secure (V=000/101/110) → 7-bit `(viw>>14)&0x7F` | C++ `FlexFrameProcessor.cpp:331` uses 7-bit for ALL types. This is **correct for the types that consume `message_length`** (only AlphanumericParser + BinaryParser read it — verified by grep). Numeric/Tone parsers ignore `message_length` and recompute themselves | **Does NOT manifest:** the 7-bit field is right for ALN/Hex/Binary; numeric never reads this field (its own bug is PARSE-01). Corrected after advisor flagged possible double-count. |
| PHASE-02 | 7 | 🟠 CONFIRMED | C compacts vector-slot consumption: idle/errored address words are skipped WITHOUT consuming a vector word | C++ maps positionally → a bad address between valid ones misaligns all following vectors | Structural. Verify against C's skip logic; fix alignment. |
| PHASE-04 | 7-8 | 🟠 | C frag/cont extraction differs for short addr + Binary/HEX | C++ uniform extraction | Cross-check with parser agents. |
| PHASE-10+ | 7 | 🟠/🟢 | `_next` decode_phase also does: priority flag, per-word BCH gating, BIW2/3/4 (date/time/SSID), tone-only inference, dedup/merge, end-of-VF system-message vector, special-address handling | Absent from FlexFrameProcessor | Some genuinely dropped, some plausibly relocated to FlexMessageDecoder/FlexGroupHandler — verify in wave 2. Overlaps D-001 (OTA time). |
| PHASE-16 | 7 | 🟢 | short-instruction slot mask | C++ `0x7F` vs C `0x0F` | False positive — `raw_viw` pre-masked to 21 bits; harmless. |

| **SYNC-01** | 1 | 🔴 ✅ **FIXED** | C `Demodulator.baud` never reset per-sample (writers: init, FIW→Sync2=Sync.baud, after-data=1600, !locked reset) | `FlexDecoder.cpp:131` called `setBaudRate(1600)` UNCONDITIONALLY every sample in `processSingleSample`, clobbering the 3200 set at l.278 on the next sample. `buildSymbol` derives symbol timing from `current_baud_` → all 3200-baud modes (A2/A4/A7) mistracked | **FIXED 2026-07-12: deleted the per-sample `setBaudRate(1600)`** (replaced with an explanatory comment). Verified 1600 re-established after each frame (post-`processCompletedFrame`) and on loss of lock (`FlexDemodulator.cpp:62`), both writing the demodulator's `current_baud_`. Builds clean. |
| SYNC-09 | 1 | 🟡 | C caps envelope window (`demod_flex_next.c:4663-4669`) | C++ omits the cap | Signal-quality only. |
| SYNC-11 | 1/5 | 🟠 | C has integrate-and-dump alternate slicer feeding AltA/B/C/D | C++ omits it; consumers are in DATA/phase stage | Cross-stage; revisit with data stage. |
| SYNC-02..08,10 | 1-2 | 🟢 | sync marker `0xA6C6AAAA`, Hamming `<4`, `count_bits`, 64-bit SR order, outer-XOR, polarity, `decode_mode` table (all 5 codes), zero-crossing PLL 0.045/0.050, slice 0.667, lock (len24/0x6666), 4-level dibit map, `read_2fsk` | all match constant-for-constant | OK — sync/mode core is faithful |

---

## Wave-1 verdict (critical stages)

**5 confirmed 🔴 correctness bugs**, two of which converge on the same real-world failure
(3200-baud modes never decode) and two of which corrupt output at all baud rates:

1. **BCH-01/02** — missing bit-31 even-parity gate → false "corrections" accepted → garbage words (all modes). *The prime suspect for "messages incorrect".*
2. **SYNC-01** — `setBaudRate(1600)` every sample clobbers 3200 → demod mistracks 3200 modes.
3. **DATA-01** — collector `baud_rate_` stuck at 1600 (assignment commented out) → 3200 collection wrong. (Pairs with SYNC-01: two hard-pins.)
4. **PHASE-01** — long-address capcode uses the OLD `demod_flex.c` PDW formula, not the `_next` ARIB range-table → wrong capcodes.
5. **PHASE-03** — numeric vector length uses 7-bit field instead of 3-bit → wrong numeric message length.
6. **DATA-03** — `processCompletedFrame` re-reads cycle/frame from *uncorrected* FIW.

All six independently verified against the C reference by the main loop (not just the sub-agents).
The sync/mode-detection core and BCH GF-math are otherwise faithful.

---

## CRASH investigation (main loop — why SDR++ crashed on this module)

| ID | Severity | Location | Description | Fix |
|----|----------|----------|-------------|-----|
| **CRASH-01** | 🔴 ✅ **FIXED** | `flex/decoder_next.h:142,172,313` | **Data race, no synchronization.** `flexMessages` (`std::vector<std::string>`) is `push_back`-ed from the **DSP thread** (`_audioHandler`→`handleFlexMessage`, l.313) while `showMenu` **reads/iterates it from the GUI thread** (l.172). A `push_back` reallocation concurrent with GUI iteration → use-after-free / segfault. `grep` finds **0 mutex/lock/atomic** in the wrapper. This is almost certainly the reported "SDR++ crashes on this module". | **FIXED 2026-07-12:** added `std::mutex flexMessagesMutex`; GUI snapshots the vector under a short lock then renders the copy; `clear` and `push_back` guarded. Module rebuilds clean. **This was a MODULE bug, not an SDR++ core bug.** |
| CRASH-02 | 🟡 | `decoder_next.h:251-252` | Wrapper creates its own `bchDecoder` (`BCHCode`) duplicating the one inside `FlexErrorCorrector` — dead/duplicate, not a crash. | Remove the duplicate. |
| CRASH-03 | 🟡 | `decoder_next.h` broad `try/catch` + `isfinite`/`clamp` everywhere | Defensive wrapper swallows exceptions — hides root causes (masks symptoms, complicates debugging). Explains why crashes were intermittent rather than immediate. | Keep guards but log+surface, don't silently swallow. |

**Verdict on crashes:** cause is in the MODULE (unsynchronized GUI/DSP shared `std::vector`),
not SDR++ core. Contrast: the POCSAG decoder never shows a message list in GUI (`pocsag/decoder.h`
just `flog::debug`s), so it has no such race — consistent with only FLEX crashing.

---

## Performance notes (main loop)

| ID | Severity | Location | Note |
|----|----------|----------|------|
| PERF-01 | 🟡 | `dsp.h:80-89` | Per-sample scalar DC-removal loop in `FLEXDSP::run` runs before AGC; fine but could fold into a single pass. Low priority. |
| PERF-02 | 🟡 | `FlexErrorCorrector::countBits` | Manual popcount used even when `__builtin_popcount` is available (guarded by `USE_BUILTIN_POPCOUNT` which is never defined). Define it to use hardware popcount. |

## Wave 2 — output & dropped features

| ID | Stage | Severity | C behavior | C++ behavior | Status |
|----|-------|----------|-----------|--------------|--------|
| **DROP-01** | 7-8 | 🔴 CONFIRMED | Per-word `bch_err[]` flags follow phase data into parsers; `parse_alphanumeric:96-99` inserts `'?'` markers for BCH-failed words; K-checksum + signature verified | `applyErrorCorrection` masks uncorrectable word to `0x1FFFFF` (`FlexFrameProcessor.cpp:213-216`), **no per-word error flag exists**; parser unpacks it as three `0x7F` (DEL) chars silently; `k_ok`/`sig_ok` never computed | Structural. C++ lacks the `bch_err[]` array entirely. Fix: thread per-word error flags from FrameProcessor → parsers, emit `?` for damaged words. Only affects damaged frames. |
| OUT-01 | 12 | 🟠 | `flex_next_json_emit` emits rich per-field cJSON | C++ `FlexOutputFormatter` emits old multimon pipe line `FLEX_NEXT\|baud/levels\|cycle.frame.phase\|capcode\|...` | Different output schema — never ported. Info-loss, not decode-corruption. |
| OUT-02 | 12 | 🟠 | `addr_type_char` → 7 types S/L/N/T/O/I/R (`:979`) | Collapsed to 2-way L/S in `formatHeader` (`FlexOutputFormatter.cpp:107-110`) + separate G flag; Info/Network/Operator/Reserved lost | Info-loss. |
| DROP-02 | 11-12 | 🟢/🟠 | `is_priority`, `sec_subtype`/`opr_category`, BIW2/3/4 (SSID/date/time/SysInfo), BIW101 end-of-VF sysmsg, whole `flextime_*` OTA time | ABSENT (grep 0 hits, none relocated) | Missing features, not bugs — never corrupt an emitted message. Confirms D-001. |
| DROP-03 | 7 | 🟢 | Tone-only inference is **disabled in C itself** (`demod_flex_next.c:3281-3303`) | C++ matches C's active behavior | **NOT a drop** — corrects earlier PHASE agent. |

| **DROP-01b** | 7 | 🔴 CONFIRMED | C `decode_phase` (`:2929-2948`) NEVER abandons a phase: each uncorrectable word gets `bch_err[i]=1` + `bch_uncorr++` but the phase is always parsed; damaged words become `?` | C++ `applyErrorCorrection` (`FlexFrameProcessor.cpp:236`) invents a **50%-fail threshold** `success = failed_words <= size/2` and abandons the whole phase above it — no equivalent in C | Under weak signal, C++ silently drops whole phases the C reference would partially decode. Fix: always process; drop the threshold; carry per-word flags (ties to DROP-01). |

## Wave 2 — message parsers

| ID | Stage | Severity | C behavior | C++ behavior | Status |
|----|-------|----------|-----------|--------------|--------|
| **PARSE-01** | 8 | 🔴 CONFIRMED | `parse_numeric:2239-2242`: `w1=phaseptr[j]>>7; w2=w1>>7;` (w2 from UNMASKED w1, bits 14-16) `w1&=0x7f; w2=(w2&0x07)+w1` | `NumericParser.cpp:28-31`: masks `w1=(vw>>7)&0x7F` FIRST, then `w2=(w1>>7)&0x07` → `w1>>7==0` always → `w2==w1` → **word count = 0, every numeric message truncated to ~1 word/~4 digits.** Op order swapped; the correct logic is even in the comment at l.26. | Distinct from PHASE-03. Fix: compute w2 from the unmasked value before masking w1. |
| **PARSE-02** | 8 | 🔴 CONFIRMED | All 4 C parsers insert `'?'` for BCH-damaged words via `bch_err[]` | `MessageParseInput` carries no per-word error array → no `?` substitution anywhere | Same root as DROP-01. |
| **PARSE-03** | 8 | 🔴 CONFIRMED | BCD table `"0123456789.U -]["` — index 10 = `'.'` (spare, ARIB 3.10.1.1) | `FlexTypes.h:36-37` index 10 = `' '` (space) | One-cell table bug. Fix: index 10 → `'.'`. |
| **PARSE-06** | 8 | 🔴 CONFIRMED | C does full nibble bit-stream unpack + termination-fill strip + signature validation for binary/HEX | `BinaryParser` is a raw `%08X`-per-word hex dump | Rewrite BinaryParser to match C. |
| **PARSE-09** | 9 | 🔴 CONFIRMED | Fragment reassembly functional | `FlexMessageDecoder::processFragment` hardcodes `capcode=0`, emits `"[Fragment N buffered]"` placeholder | Reassembly is a non-functional stub. |
| PARSE-05 | 8 | 🟠 | C computes+reports K checksum and signature (S) for alpha/numeric/HEX | Absent in all C++ parsers | No integrity verification. |
| PARSE-04 | 8 | 🟠 | C keeps `0x0C` space-fill digit | `NumericParser` drops it (misaligns groups); but `ToneParser` keeps it → **two C++ parsers internally inconsistent** | Fix NumericParser to match. |
| PARSE-07 | 8 | 🟠 | C `parse_short_message` handles 4 sub-types (numeric/Network-ID, source, numbered, reserved) | `ToneParser` handles only sub-type 0 | Incomplete. |
| PARSE-08 | 8 | 🟠 | dispatch value 2 = `SHORT_MESSAGE`→`parse_short_message` | C++ value 2 = `Tone`→`ToneParser`; Secure(0) binary sub-routing also diverges | Enum/dispatch mismatch at value 2 (0,1,3-7 match). |
| PARSE-✓ | 8 | 🟢 | — | 7-bit char order, `add_ch`, frag-flag K/C/F logic, numeric shift-register+header-skip, tone bit positions 9/13/17, enum values 0/1/3-7 all match | OK |

*Note:* `MAX_CAPCODE=4297068542LL` (`FlexTypes.h:34`) **matches** the C range limit — so PHASE-01's
range *constant* is right; only the long-address *formula* is wrong (from old demod_flex.c).

## Wave 2 — fragments / dedup / groups

| ID | Stage | Severity | C behavior | C++ behavior | Status |
|----|-------|----------|-----------|--------------|--------|
| **FRAG-01..07** | 9 | 🔴 CONFIRMED | 64-slot fragment cache `frag_find/alloc/append/expire`, keyed `(capcode,type,msg_n)`, F-sequence tracking, R/M carry-over, wrap-safe `&0x7FF` frame expiry, partial-output-on-timeout; 32-slot word-level `dedup_check_words` combine cache | **Both entirely absent.** Only `processFragment` stub (`capcode=0`, no expiry, naive concat — see PARSE-09). Author comment: `// We'd need to get this from the input context` | Multi-part messages never reassemble; duplicate retransmissions never combined. Big missing subsystem. |
| GRP-01 | 10 | 🟠 | GroupDelivering / 2-FIW fragment-delivery timeout | dropped | Secondary group behavior. |
| GRP-02 | 10 | 🟠 | slot-reuse teardown | dropped | Secondary. |
| GRP-05 | 10 | 🟡 | capcode cap | off-by-one | Minor. |
| GRP-06,07,09,10 | 10 | 🟢 | capcode range 2029568–2029583, `getGroupBit=capcode−MIN`, target-cycle algo, 4-branch expiry ladder | match line-for-line | OK — group core faithful |
| GRP-03/04 | 10 | 🟢 | group_bit `&0x0f`, GROUP_BITS=16 | C++ `&0x7F`, =17 | Cosmetic — `applyErrorCorrection` pre-masks to 21 bits (`FlexFrameProcessor.cpp:224`) before vector read (l.136), so bits 21-23 never reach group_bit; always 0-15. NOT a bug (agent self-corrected). |

---

## FINAL SUMMARY — all stages analyzed

**Analysis complete: 2 waves, 7 sub-agents + main-loop verification, C ground truth throughout.**

### 🔴 Correctness bugs (confirmed) — grouped by user symptom

**"Messages displayed but incorrect":**
- BCH-01/02 — missing bit-31 even-parity gate → false BCH corrections accepted (all modes). *Prime cause.*
- PARSE-01 — numeric word-count computed as 0 → numeric messages truncated to ~4 digits.
- PHASE-01 — long-address capcode uses old demod_flex.c PDW formula → wrong capcodes (range const IS right).
- PARSE-03 — BCD table index 10 = ' ' should be '.'.
  *(PHASE-03 numeric-length was DOWNGRADED to non-bug: 7-bit field is correct for the ALN/Hex/Binary
  parsers that actually read message_length; numeric recomputes its own — that's PARSE-01.)*
- PARSE-06 — BinaryParser is a raw hex dump, not real unpacking.
- DATA-03 — cycle/frame re-read from uncorrected FIW.
- DROP-01/PARSE-02 — damaged words emit 0x7F instead of '?'; no K/signature checks.
- DROP-01b — invented 50%-fail threshold abandons whole phases C would partially decode.

**"Doesn't work at 3200 baud" (A2/A4/A7):**
- SYNC-01 — `setBaudRate(1600)` every sample clobbers 3200 (delete FlexDecoder.cpp:131).
- DATA-01 — collector `baud_rate_` stuck at 1600 (uncomment assignment).

**"SDR++ crashes on this module":**
- CRASH-01 — unsynchronized `flexMessages` vector shared between DSP and GUI threads. **MODULE bug, not core.**

**Missing subsystems (multi-part messages):**
- FRAG-01..07 / PARSE-09 — fragment reassembly + word dedup entirely absent (stub only).

### 🟠 Risky / info-loss
PHASE-02 (positional vs skip-compacted vector alignment), DATA-04 (Sync2 stub), DATA-06 (FIW
repeat/roaming dropped), SYNC-11 (alt slicer), PARSE-04/05/07/08 (numeric fill inconsistency,
no K/sig, incomplete short-message + dispatch value-2 mismatch), OUT-01/02 (pipe vs cJSON, 7→2
address types), GRP-01/02, DROP-02 (OTA time / BIW2-4 / priority absent — incl. D-001).

### 🟡 Perf / cleanup
PERF-01 (per-sample DC loop), PERF-02 (countBits ignores `__builtin_popcount`), DATA-05
(FlexStateMachine State-Pattern dead code), CRASH-02 (duplicate BCHCode in wrapper), CRASH-03
(broad try/catch masks errors), GRP-05 (off-by-one cap), GRP-03/04 (cosmetic wider mask).

### 🟢 Verified faithful (the port got these right)
Sync marker/Hamming/decode_mode table, BCH GF-math + bit ordering + countBits, FIW field
extraction + checksum, deinterleave index math, 7-bit alpha char unpacking, group subsystem core,
enum values (except value 2), MAX_CAPCODE constant.

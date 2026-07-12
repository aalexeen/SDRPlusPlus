# Stage 12 — Output formatting + address type, and DROPPED-feature audit

**Verdict.** The C++ port never ported `flex_next_json_emit` at all: the C reference emits a
rich **cJSON object** (per-field, self-describing), while the C++ `FlexOutputFormatter` emits the
**old multimon-ng pipe-delimited line** `FLEX_NEXT|baud/levels|cycle.frame.phase|capcode|flags|type|...`.
So "does the message line format match?" — **no, it is a different output schema entirely.** Only
baud, level, cycle, frame, phase, capcode and group_capcodes survive (in different form). The 7-way
address-type classification (`addr_type_char` → S/L/N/T/O/I/R) collapses to a 2-way `L/S` flag
(Temporary/group is partially recovered via a separate `G` flag). PART B: `is_priority`,
`sec_subtype`/`opr_category`, BIW2/3/4 (SSID/date/time/SysInfo), BIW101 end-of-VF system message,
the whole `flextime_*` OTA-time block, and per-word BCH `?` substitution are **genuinely absent** from
the entire `flex_next_decoder` tree — none are relocated. Tone-only *inference* is **not** a real drop:
the C reference itself has it disabled ("all vector slots treated as valid"). The only dropped item
that affects output *correctness* (rather than being a missing feature) is per-word BCH gating: a
damaged content word is masked to `0x1FFFFF` and read raw by the parser, silently emitting `0x7F` (DEL)
characters instead of a `?` damage marker.

Severity: 🔴 correctness · 🟠 likely/risky/info-loss · 🟡 perf/style · 🟢 OK/intentional.

| ID | Sev | C ref (demod_flex_next.c) | C++ (flex_next_decoder/) | Description | Fix/Note |
|----|-----|---------------------------|--------------------------|-------------|----------|
| OUT-01 | 🟠 | `flex_next_json_emit` 1000–1199 (cJSON object) | `FlexOutputFormatter::outputMessage`/`formatHeader` .cpp:17–116 | **Whole output schema differs.** C = structured JSON; C++ = multimon-ng pipe line `FLEX_NEXT\|baud/levels\|cyc.frm.phase\|capcode\|flags\|type\|`. Not a per-field bug — a format substitution. | If parity with the `_next` JSON is required, re-implement emit as JSON. Otherwise document as intentional format choice. All rows below are the consequences of this substitution. |
| OUT-02 | 🟠 | `addr_type_char` 979–995 → `addr_type` field 1030–1033 (S/L/N/T/O/I/R) | `formatHeader` .cpp:107–110 emits only `addr_flag = long?'L':'S'` | Address-type classification collapsed to Long/Short. `addr_type_char` is **not ported anywhere** (grep `addr_type` = 0 hits in C++). Info-loss, not decode-corruption. | Temporary/group ('T') partially recovered via separate `G` flag (`is_group_message` from `FlexGroupHandler::isGroupCapcode`). Truly lost: Info-Service(I)/Network(N)/Operator(O)/Reserved(R) distinction. Note: C++ also never classifies special-address ranges 0x1F0001–0x1F7FFE beyond the group sub-range. |
| OUT-03 | 🟢 | `flex_next_json_emit` 1021–1029: baud, level, phase, cycle, frame, capcode | `formatHeader` .cpp:97–105 | These fields DO survive (re-encoded into the pipe header). Values equivalent. | OK. |
| OUT-04 | 🟢 | `group_capcodes` array 1072–1077 | `outputMessage` .cpp:59–64 | Group capcode list survives (printed as `\|`-separated 10-digit fields before content). | OK. |
| OUT-05 | 🟡 | `msg_type` name string 1037–1052 (0=secure … 8=tone_only) | `formatHeader` .cpp:113 emits `static_cast<int>(type)`; `getMessageTypeString` .cpp:118–139 emits 3-letter tag | C++ emits a numeric type + own 3-letter tag (SEC/SIN/TON/NUM/SNM/ALN/BIN/NNU). C++ `MessageType` enum (FlexTypes.h:56–64) is 0–7 and does NOT match C's 0–8 name map (C's tone_only=8; C++ Tone=2). Human-readable name string absent. | Cosmetic; if downstream parses the type field, numbering mismatch matters. |
| DROP-01 | 🔴 | Per-word BCH gating: `bch_err[]` array (283), set at 2940–2943; alphanumeric `?` fill 1922–1927, numeric 2352/2473, binary/hex 2652–2658; K-checksum 1862–1899; signature check 1917–1969 | **ABSENT.** `FlexErrorCorrector::fixErrors` + `FlexFrameProcessor::applyErrorCorrection` .cpp:200–242 mask a failed word to `0x1FFFFF`; parsers (e.g. `AlphanumericParser.cpp:32–47`) read words raw, no `bch_err`/`?`/K/signature logic | **Only output-correctness drop.** A BCH-uncorrectable *content* word → masked `0x1FFFFF` → parser extracts three `0x7F` (DEL) chars, silently corrupting the message with no `?` damage marker. K-checksum (`k_ok`) and signature (`sig_ok`) fields never computed or emitted. | Scope: only manifests on damaged frames (clean/correctable words unaffected). Fix: propagate per-word BCH-fail status to parsers; substitute `?` for damaged words; port K/signature verification. |
| DROP-02 | 🟠 | `is_priority` (315) set at 3424 (`(i-aoffset)<prio`), emitted 1035–1036; `.P` tag 2191/2377/2881 | **ABSENT** (grep `priority`/`is_priority` = 0 hits) | Priority-address flag never computed or emitted. Missing feature, no message corruption. | Low priority. Requires the `prio` field from BIW which C++ also doesn't extract. |
| DROP-03 | 🟠 | BIW2/3/4 parsing 3015–3260: SSID1 (3033), Date (3080s), Time (3120s), SysInfo/101 (3165), SSID2 (3223) → `type_tag` "BIW_SSID1"/"BIW_SSID2" etc. | **ABSENT.** `extractBlockInfoWord` .cpp:244–281 reads only `biw.raw_data` for address/vector offsets; no BIW subtype decode | Entire BIW2/3/4 subtype decode (local ID, coverage, country, TMF, date, time, timezone) dropped. `type_tag` field never emitted. | Missing feature. Feeds DROP-04/05. |
| DROP-04 | 🟢 | BIW101 end-of-VF system-message vector: `biw_sysmsg_a_type` (880) set 3171; Section 3.9.2 | **ABSENT** (grep `biw_sysmsg`/`101` = 0 hits) | System-message vector decode at end of VF not ported. | Rare/feature-only; no impact on normal paging output. |
| DROP-05 | 🟢 | `flextime_*` OTA-time block 502–902 (`flextime_emit` 902; vote/expire/validate); emitted 1092–1191 as `flextime` object | **ABSENT** (grep `flextime`/`ota_time` = 0 hits; see FLEX_COMPARISON D-001) | Whole over-the-air date/time/timezone decode + voting + JSON `flextime` object dropped. | Confirmed intentional per D-001 (time not essential to message content). Depends on DROP-03 (BIW date/time). |
| DROP-06 | 🟢 | `sec_subtype`/`opr_category` (316–317), reset 3421–3422, emitted 1086–1090 | **ABSENT** (grep = 0 hits) | SEC sub-type and Operator-message category strings never derived or emitted. | Feature-only. |
| DROP-07 | 🟢 | Tone-only *inference* block 3281–3303 | Not implemented in C++ | **NOT a real drop.** C reference explicitly disables it: "Currently disabled -- all vector slots are treated as valid (matches FLEX behavior)." C++ matches C's active behavior. | No action. (C++ *does* have a ToneParser for explicit Tone vector type — that path exists.) |
| DROP-08 | 🟠 | Special-address handling: LA1/LA2 + range table 3348–3411, `addr_type_char` ranges 979–995 | Partial: `FlexFrameProcessor::processAddressInfoWord` .cpp:283–321 only does long/short + group; special ranges 0x1F0001–0x1F7FFE not classified | Special short-address ranges (Reserved/Info/Network/Operator) not distinguished; only Temporary/group sub-range handled. Overlaps OUT-02 and PHASE-01 (wrong long-addr capcode formula, already logged). | Info-loss on special addresses; long-address capcode correctness is PHASE-01's scope. |

## Notes on relocation (PART B (a) vs (b))
Every audited feature was grepped across the whole `flex_next_decoder/` tree (incl. `parsers/`):
- **Genuinely absent (not relocated):** per-word BCH `?`/K/signature (DROP-01), `is_priority` (DROP-02),
  BIW2/3/4 (DROP-03), BIW101 (DROP-04), `flextime_*` (DROP-05), `sec_subtype`/`opr_category` (DROP-06),
  `addr_type_char` S/L/N/T/O/I/R (OUT-02).
- **Partially relocated:** group/Temporary address handling lives in `FlexGroupHandler` + the `G`
  flag (covers the 'T' case of OUT-02/DROP-08); message parsing lives in `parsers/` (but without the
  BCH-gated `?` substitution of DROP-01).
- **Not a drop:** tone-only inference (DROP-07) — disabled in C too.

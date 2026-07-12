# FLEX protocol reference (for verifying constants during comparison)

Sources: FLEX Thesis (VT, sigidwiki), wavecom decoder help, multimon-ng gen_flex.c,
Theldus/tinyflex, TUCoPS flex.txt. Use these to judge whether C++ constants are *correct*,
not just whether they match C.

## Codeword
- 32-bit codeword = **BCH(31,21)** + 1 even parity bit.
- Generator polynomial: `g(x) = x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1`.
- 21 information bits (x^30..x^10), 10 parity bits, +1 overall even parity bit.
- Corrects **up to 2 errors** per 32-bit codeword.
- Codeword layout (tinyflex encode): `[21 data][10 BCH][1 parity]`; FIW encoded as `rev32(dw)`
  then encode_word — **bit reversal matters**; multimon decoder does bit reversal during BCH.

## Frame / timing
- 4-minute cycle = 128 frames, each frame 1.875 s.
- Frame = 1600 bps sync header + data blocks.
- SYNC1 (two 32-bit sync words + 16 dotting bits) + FIW, always 1600 bps 2-FSK.
- SYNC2 = 25 ms (trains receiver for new baud/mod). DATA ≈ 1760 ms.
- Sync1's two 32-bit words identify baud (1600/3200) and modulation (2/4-FSK) of Sync2+data.

## FIW bit fields (21 data bits) — from gen_flex.c build_fiw / tinyflex create_fiw
- bits 3:0   = checksum
- bits 7:4   = cycle number (0–14)
- bits 14:8  = frame number (0–127)
- bit 15     = n (roaming)         [gen_flex calls 20:15 "fix3/reserved"]
- bit 16     = r (multi-tx)
- bits 20:17 = t (low traffic)     [tinyflex]
- Checksum rule: nibble0+nibble1+nibble2+nibble3+nibble4 + bit20 = 0xF (mod 16).

## Transmission modes (baud / FSK / phases)
| Symbol rate | Modulation | Bit rate | Phases |
|-------------|-----------|----------|--------|
| 1600 Bd | 2-FSK  | 1600 | A |
| 1600 Bd | 4-FSK  | 3200 | A, B |
| 3200 Bd | 2-FSK  | 3200 | A, C |
| 3200 Bd | 4-FSK  | 6400 | A, B, C, D |

## Blocks / interleaving
- Block = 8 words × 32 bits. **Words stacked in rows, transmitted by columns → interleaved.**
- 1600 bps: block = eight 32-bit codewords (256 bits).
- 3200 bps: 512 bits from two multiplexed 8-codeword groups (4-FSK, 1600 sym/s).
- 6400 bps: 1024 bits from four multiplexed groups (4-FSK, 3200 sym/s).
- Receiver demuxes + de-interleaves into groups of 8 codewords, then BCH-checks.
- ⚠️ Source disagreement: "10 data blocks" (TUCoPS) vs "11 blocks" (thesis) — verify against code.
- Fields per frame after sync: Block Info Word → Address field → Vector field → Message data
  → leftover. Addresses carried first (pagers sleep if not addressed). Fields need NOT align
  with codeword-group boundaries.

## Message types (8)
Alphanumeric / Secure, three Numeric types, Binary, Tone-Only, Instruction.

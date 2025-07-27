#include "flex.h"
#include <string.h>
#include <utils/flog.h>

#define FLEX_SYNC_MARKER    0xA6C6AAAA
#define SYNC_DISTANCE       4
#define PHASE_WORDS         88

namespace flex {

    Decoder::Decoder() {
        syncBuf = 0;
        synced = false;
        syncPos = 0;
        state = STATE_SYNC1;
        dataPos = 0;
        dataCount = 0;
        baud = 1600;
        levels = 2;
        polarity = false;
        memset(dataBuf, 0, sizeof(dataBuf));
        memset(&fiw, 0, sizeof(fiw));
    }

    void Decoder::process(uint8_t* symbols, int count) {
        for (int i = 0; i < count; i++) {
            uint8_t sym = symbols[i];

            switch (state) {
                case STATE_SYNC1: {
                    // Look for sync pattern
                    syncBuf = (syncBuf << 1) | ((sym < 2) ? 1 : 0);

                    if (checkSync(syncBuf)) {
                        flog::debug("FLEX: Sync found, baud={}, levels={}", baud, levels);
                        state = STATE_FIW;
                        dataPos = 0;
                        synced = true;
                    }
                    break;
                }

                case STATE_FIW: {
                    // Collect Frame Information Word (32 bits)
                    if (levels == 2) {
                        dataBuf[0] = (dataBuf[0] >> 1) | ((sym > 1) ? 0x80000000 : 0);
                        dataPos++;
                        if (dataPos >= 32) {
                            fiw.raw = dataBuf[0];
                            if (decodeFIW()) {
                                state = STATE_SYNC2;
                                dataPos = 0;
                            } else {
                                state = STATE_SYNC1;
                                synced = false;
                            }
                        }
                    } else {
                        // 4-level FSK handling would go here
                        // For now, fall back to sync search
                        state = STATE_SYNC1;
                        synced = false;
                    }
                    break;
                }

                case STATE_SYNC2: {
                    // Skip second sync pattern
                    dataPos++;
                    if (dataPos >= 32) {
                        state = STATE_DATA;
                        dataPos = 0;
                        dataCount = 0;
                    }
                    break;
                }

                case STATE_DATA: {
                    // Collect data phase
                    if (levels == 2) {
                        int wordIdx = dataCount / 32;
                        int bitIdx = dataCount % 32;

                        if (wordIdx < PHASE_WORDS) {
                            if (bitIdx == 0) {
                                dataBuf[wordIdx] = 0;
                            }
                            dataBuf[wordIdx] = (dataBuf[wordIdx] >> 1) | ((sym > 1) ? 0x80000000 : 0);
                            dataCount++;

                            if (dataCount >= PHASE_WORDS * 32) {
                                processData();
                                state = STATE_SYNC1;
                                synced = false;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    bool Decoder::checkSync(uint64_t buf) {
        // Check for normal sync
        uint32_t marker = (buf & 0x0000FFFFFFFF0000ULL) >> 16;
        uint16_t codehigh = (buf & 0xFFFF000000000000ULL) >> 48;
        uint16_t codelow = ~(buf & 0x000000000000FFFFULL);

        if (hammingDistance(marker, FLEX_SYNC_MARKER) < SYNC_DISTANCE &&
            hammingDistance(codelow, codehigh) < SYNC_DISTANCE) {
            decodeMode(codehigh);
            polarity = false;
            return true;
        }

        // Check for inverted sync
        marker = (~buf & 0x0000FFFFFFFF0000ULL) >> 16;
        codehigh = (~buf & 0xFFFF000000000000ULL) >> 48;
        codelow = ~((~buf) & 0x000000000000FFFFULL);

        if (hammingDistance(marker, FLEX_SYNC_MARKER) < SYNC_DISTANCE &&
            hammingDistance(codelow, codehigh) < SYNC_DISTANCE) {
            decodeMode(codehigh);
            polarity = true;
            return true;
        }

        return false;
    }

    void Decoder::decodeMode(uint16_t syncCode) {
        struct {
            uint16_t sync;
            int baud;
            int levels;
        } modes[] = {
            { 0x870C, 1600, 2 },
            { 0xB068, 1600, 4 },
            { 0x7B18, 3200, 2 },
            { 0xDEA0, 3200, 4 },
            { 0x4C7C, 3200, 4 },
            { 0, 0, 0 }
        };

        for (int i = 0; modes[i].sync != 0; i++) {
            if (hammingDistance(modes[i].sync, syncCode) < SYNC_DISTANCE) {
                baud = modes[i].baud;
                levels = modes[i].levels;
                return;
            }
        }

        // Default to 1600/2
        baud = 1600;
        levels = 2;
    }

    bool Decoder::decodeFIW() {
        uint32_t fiw_data = fiw.raw;

        if (!correctBCH(fiw_data)) {
            flog::debug("FLEX: FIW BCH correction failed");
            return false;
        }

        // Extract fields
        uint32_t checksum = (fiw_data & 0xF) +
                           ((fiw_data >> 4) & 0xF) +
                           ((fiw_data >> 8) & 0xF) +
                           ((fiw_data >> 12) & 0xF) +
                           ((fiw_data >> 16) & 0xF) +
                           ((fiw_data >> 20) & 0x1);
        checksum &= 0xF;

        if (checksum != 0xF) {
            flog::debug("FLEX: FIW checksum failed: {}", checksum);
            return false;
        }

        fiw.cycleno = (fiw_data >> 4) & 0xF;
        fiw.frameno = (fiw_data >> 8) & 0x7F;

        int timeseconds = fiw.cycleno * 4 * 60 + fiw.frameno * 4 * 60 / 128;
        flog::debug("FLEX: FIW cycle={} frame={} time={}:{:02d}",
                   fiw.cycleno, fiw.frameno, timeseconds/60, timeseconds%60);

        return true;
    }

    void Decoder::processData() {
        // Process each data word
        for (int i = 0; i < PHASE_WORDS; i++) {
            uint32_t word = dataBuf[i];

            if (!correctBCH(word)) {
                continue; // Skip corrupted words
            }

            // Check if this is an address word (bit 31 = 0)
            if ((word & 0x80000000) == 0) {
                // Address word
                Address addr = (word >> 13) & 0x1FFFFF;
                MessageType type = (MessageType)((word >> 11) & 0x3);
                bool longAddr = (word >> 10) & 1;

                // Simple message handling - look for following data
                if (i + 1 < PHASE_WORDS) {
                    switch (type) {
                        case MESSAGE_TYPE_ALPHANUMERIC:
                            decodeAlphanumeric(dataBuf, i + 1, 1, addr);
                            break;
                        case MESSAGE_TYPE_STANDARD_NUMERIC:
                        case MESSAGE_TYPE_SPECIAL_NUMERIC:
                        case MESSAGE_TYPE_NUMBERED_NUMERIC:
                            decodeNumeric(dataBuf, i + 1, 1, addr);
                            break;
                        case MESSAGE_TYPE_TONE:
                            decodeTone(addr);
                            break;
                        default:
                            flog::debug("FLEX: Unknown message type {}", (int)type);
                            break;
                    }
                }
            }
        }
    }

    void Decoder::decodeAlphanumeric(uint32_t* data, int start, int len, Address addr) {
        std::string message;

        for (int i = 0; i < len && (start + i) < PHASE_WORDS; i++) {
            uint32_t word = data[start + i];

            // Extract 21 data bits (skip checksum)
            uint32_t textBits = (word >> 11) & 0x1FFFFF;

            // Decode 3 characters per word
            for (int j = 0; j < 3; j++) {
                uint8_t ch = (textBits >> (14 - j * 7)) & 0x7F;
                if (ch >= 32 && ch <= 126) {
                    message += (char)ch;
                } else if (ch == 0x03) {
                    // End of message
                    break;
                }
            }
        }

        if (!message.empty()) {
            onMessage(addr, MESSAGE_TYPE_ALPHANUMERIC, message);
        }
    }

    void Decoder::decodeNumeric(uint32_t* data, int start, int len, Address addr) {
        const char digits[] = "0123456789 U -][";
        std::string message;

        if (start < PHASE_WORDS) {
            uint32_t word = data[start];
            uint32_t numData = (word >> 11) & 0x1FFFFF;

            // Simple numeric decoding - extract 4-bit digits
            for (int i = 0; i < 5; i++) {
                uint8_t digit = (numData >> (16 - i * 4)) & 0xF;
                if (digit < 16) {
                    message += digits[digit];
                }
            }
        }

        if (!message.empty()) {
            onMessage(addr, MESSAGE_TYPE_STANDARD_NUMERIC, message);
        }
    }

    void Decoder::decodeTone(Address addr) {
        onMessage(addr, MESSAGE_TYPE_TONE, "TONE");
    }

    bool Decoder::correctBCH(uint32_t& data) {
        // Simplified BCH correction - in reality this would use proper BCH(31,21) code
        // For now, just return true to allow basic functionality
        return true;
    }

    int Decoder::hammingDistance(uint32_t a, uint32_t b) {
        uint32_t diff = a ^ b;
        int count = 0;
        while (diff) {
            count += diff & 1;
            diff >>= 1;
        }
        return count;
    }
}
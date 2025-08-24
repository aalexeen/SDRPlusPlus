#include "FlexSynchronizer.h"
#include <iostream>

namespace flex_next_decoder {

    FlexSynchronizer::FlexSynchronizer()
        : sync_buffer_(0), last_polarity_(false), symbol_count_(0) {
    }

    FlexSynchronizer::FlexSynchronizer(int verbosity_level)
        : FlexNextDecoder(verbosity_level), sync_buffer_(0), last_polarity_(false), symbol_count_(0) {
    }

    uint32_t FlexSynchronizer::processSymbol(uint8_t symbol) { // checked
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "processSymbol called with symbol: " << static_cast<int>(symbol) << std::endl;
        }
        // ✅ Increment symbol counter
        symbol_count_++;

        // ✅ Shift symbol into 64-bit sync buffer
        // Symbols < 2 become 1, symbols >= 2 become 0 (same as original C code)
        uint8_t bit = (symbol < 2) ? 1 : 0;
        sync_buffer_ = (sync_buffer_ << 1) | bit;

        // ✅ Check for positive polarity sync pattern first
        uint32_t sync_code = checkSyncPattern(sync_buffer_);
        if (sync_code != 0) {
            last_polarity_ = false; // Normal polarity
            return sync_code;
        }

        // ✅ Check for negative polarity (inverted signal)
        sync_code = checkSyncPattern(~sync_buffer_);
        if (sync_code != 0) {
            last_polarity_ = true; // Inverted polarity
            return sync_code;
        }

        // ✅ No sync pattern found
        return 0;
    }

    uint32_t FlexSynchronizer::checkSyncPattern(uint64_t buffer) { // checked
        // ✅ Extract 64-bit FLEX sync pattern: AAAA:BBBBBBBB:CCCC
        // AAAA = upper 16 bits (codehigh)
        // BBBBBBBB = middle 32 bits (marker)
        // CCCC = lower 16 bits (codelow, inverted)
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(FlexSynchronizer).name() << ": " << "checkSyncPattern called with buffer: " << std::hex << buffer << std::dec << std::endl;
        }

        uint32_t marker = static_cast<uint32_t>((buffer & MARKER_MASK) >> MARKER_SHIFT);
        uint16_t codehigh = static_cast<uint16_t>((buffer & CODEHIGH_MASK) >> CODEHIGH_SHIFT);
        uint16_t codelow = static_cast<uint16_t>(~(buffer & CODELOW_MASK)); // Invert codelow


        // ✅ Validate sync pattern structure
        /*if (!validateSyncStructure(buffer, marker, codehigh, codelow)) {
            return 0;
        }*/

        // ✅ Check marker field with hamming distance < 4
        if (countBitDifferences(marker, SYNC_MARKER) >= HAMMING_THRESHOLD) {
            return 0;
        }

        // ✅ Check outer code with hamming distance < 4
        // codelow is already inverted in validateSyncStructure
        if (countBitDifferences(codehigh, codelow) >= HAMMING_THRESHOLD) {
            return 0;
        }

        // ✅ Valid sync pattern found, return the high code
        return codehigh;
    }

    /*bool FlexSynchronizer::validateSyncStructure(uint64_t buffer,
                                                 uint32_t& marker_out,
                                                 uint16_t& codehigh_out,
                                                 uint16_t& codelow_out) const {
        // ✅ Extract 64-bit FLEX sync pattern: AAAA:BBBBBBBB:CCCC
        // AAAA = upper 16 bits (codehigh)
        // BBBBBBBB = middle 32 bits (marker)
        // CCCC = lower 16 bits (codelow, inverted)

        codehigh_out = static_cast<uint16_t>((buffer & CODEHIGH_MASK) >> CODEHIGH_SHIFT);
        marker_out = static_cast<uint32_t>((buffer & MARKER_MASK) >> MARKER_SHIFT);
        codelow_out = static_cast<uint16_t>(~(buffer & CODELOW_MASK)); // Invert codelow

        return true; // Structure is always extractable
    }*/

    bool FlexSynchronizer::decodeSyncMode(uint32_t sync_code, SyncInfo& sync_info) { // checked (original analog decode_mode
        // ✅ Clear output structure
        sync_info = {};
        sync_info.sync_code = sync_code;
        sync_info.polarity = last_polarity_;

        // ✅ Find matching FLEX mode using hamming distance
        for (const auto& mode : FLEX_MODES) {
            if (countBitDifferences(mode.sync_code, sync_code) < HAMMING_THRESHOLD) {
                sync_info.sync_code = mode.sync_code;
                sync_info.baud_rate = mode.baud_rate;
                sync_info.levels = mode.levels;

                if (verbosity_level_ >= 3) {
                    std::cout << "FLEX_NEXT: SyncInfoWord: sync_code=0x" << std::hex << sync_code
                              << " baud=" << std::dec << sync_info.baud_rate
                              << " levels=" << sync_info.levels
                              << " polarity=" << (sync_info.polarity ? "NEG" : "POS") << std::endl;
                }
                return true;
            }
        }

        // ✅ Unknown sync code - use default fallback
        if (verbosity_level_ >= 3) {
            std::cout << "FLEX_NEXT: Unknown sync code 0x" << std::hex << sync_code
                      << ", defaulting to 1600bps 2FSK" << std::dec << std::endl;
        }

        // sync_info.baud_rate = 1600;
        // sync_info.levels = 2;
        return false;
    }

    void FlexSynchronizer::reset() {
        sync_buffer_ = 0;
        last_polarity_ = false;
        symbol_count_ = 0;
    }

    //=============================================================================
    // Static Utility Methods
    //=============================================================================

    bool FlexSynchronizer::isValidSyncCode(uint32_t sync_code) {
        for (const auto& mode : FLEX_MODES) {
            if (countBitDifferences(mode.sync_code, sync_code) < HAMMING_THRESHOLD) {
                return true;
            }
        }
        return false;
    }

    FlexMode FlexSynchronizer::getSyncModeInfo(uint32_t sync_code) {
        for (const auto& mode : FLEX_MODES) {
            if (countBitDifferences(mode.sync_code, sync_code) < HAMMING_THRESHOLD) {
                return mode;
            }
        }

        // ✅ Return default mode if not found
        return { 0x870C, 1600, 2 }; // Default to 1600 bps, 2-level FSK
    }

    uint32_t FlexSynchronizer::countBitDifferences(uint32_t a, uint32_t b) { // checked
        // ✅ Count differing bits using XOR and population count
        // This is the same algorithm as the original count_bits function
        uint32_t diff = a ^ b;

#ifdef USE_BUILTIN_POPCOUNT
        return __builtin_popcount(diff);
#else
        // ✅ Manual bit counting (same as original C code)
        uint32_t n = (diff >> 1) & 0x77777777;
        diff = diff - n;
        n = (n >> 1) & 0x77777777;
        diff = diff - n;
        n = (n >> 1) & 0x77777777;
        diff = diff - n;
        diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
        diff = diff * 0x01010101;
        return diff >> 24;
#endif
    }

} // namespace flex_next_decoder
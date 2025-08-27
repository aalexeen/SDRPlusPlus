#include "FlexErrorCorrector.h"
#include <stdexcept>
#include <iostream>
#include <array>

namespace flex_next_decoder {

    FlexErrorCorrector::FlexErrorCorrector() {
        try {
            // Initialize BCH(31,21,5) code with FLEX-specific parameters
            // Generator polynomial: x^5 + x^2 + 1 = 100101 binary
            std::array<int, 6> polynomial = { 1, 0, 1, 0, 0, 1 };

            // BCH parameters for FLEX protocol:
            // m=5 (field order), n=31 (code length), k=21 (data length), t=2 (error capability)
            bch_code_ = std::make_unique<BCHCode>(polynomial.data(), // Generator polynomial coefficients
                                                  5, // Field order (GF(2^5))
                                                  31, // Code length
                                                  21, // Data length
                                                  2 // Error correction capability
            );
        } catch (const std::exception &e) {
            throw std::runtime_error("Failed to initialize BCH error corrector: " + std::string(e.what()));
        }
    }

    FlexErrorCorrector::FlexErrorCorrector(int verbosity_level) : FlexNextDecoder(verbosity_level) {
        try {
            // Initialize BCH(31,21,5) code with FLEX-specific parameters
            // Generator polynomial: x^5 + x^2 + 1 = 100101 binary
            std::array<int, 6> polynomial = { 1, 0, 1, 0, 0, 1 };

            // BCH parameters for FLEX protocol:
            // m=5 (field order), n=31 (code length), k=21 (data length), t=2 (error capability)
            bch_code_ = std::make_unique<BCHCode>(polynomial.data(), // Generator polynomial coefficients
                                                  5, // Field order (GF(2^5))
                                                  31, // Code length
                                                  21, // Data length
                                                  2 // Error correction capability
            );
        } catch (const std::exception &e) {
            throw std::runtime_error("Failed to initialize BCH error corrector: " + std::string(e.what()));
        }
    }

    // bch3121_fix_errors
    bool FlexErrorCorrector::fixErrors(uint32_t &data, char phase_id) { // checked
        // Convert 32-bit data word to BCH coefficient array format
        // Extract bits from MSB to LSB (bit 30 down to bit 0)
        std::array<int, 31> received;
        uint32_t temp_data = data;

        // Only print debug for non-zero data
        if (data != 0 && verbosity_level_ >= 3) {
            std::cout << "DEBUG: Input data=0x" << std::hex << data << std::dec << std::endl;
        }

        /*Convert the data pattern into an array of coefficients*/
        for (int i = 0; i < 31; i++) {
            received[i] = (temp_data >> 30) & 1; // Extract MSB
            temp_data <<= 1; // Shift left for next bit
        }

        // Only print bit pattern for non-zero data and high verbosity
        if (data != 0 && verbosity_level_ >= 4) {
            std::cout << "DEBUG: BCH input bits: ";
            for (int i = 0; i < 31; i++) { std::cout << received[i]; }
            std::cout << std::endl;
        }

        // Apply BCH error correction
        int decode_result = bch_code_->decode(received.data()); // checked

        if (data != 0 && verbosity_level_ >= 3) {
            std::cout << "DEBUG: BCH decode result=" << decode_result << std::endl;
        }

        if (decode_result == 0) {
            // Success: Convert corrected coefficients back to 32-bit format
            uint32_t corrected_data = 0;
            for (int i = 0; i < 31; i++) {
                corrected_data <<= 1;
                corrected_data |= received[i];
            }

            // Count how many errors were fixed
            uint32_t error_mask = (data & 0x7FFFFFFF) ^ corrected_data;
            uint32_t errors_fixed = countBits(error_mask);

            if (errors_fixed > 0) {
                if (verbosity_level_ >= 3) {
                    std::cout << "FLEX_NEXT: Phase " << phase_id << " Fixed " << errors_fixed << " errors @ 0x"
                              << std::hex << error_mask << " (0x" << std::hex << (data & 0x7FFFFFFF) << " -> 0x"
                              << std::hex << corrected_data << ")" << std::dec << std::endl;
                }
            }

            // Write corrected data back to caller
            data = corrected_data;
            return true;
        } else {
            // Only log failures for non-zero data to reduce spam
            if (data != 0) {
                if (verbosity_level_ >= 3) {
                    std::cout << "FLEX_NEXT: Phase " << phase_id << " Data corruption - Unable to fix errors (0x"
                              << std::hex << data << std::dec << ")." << std::endl;
                }
            }
            return false;
        }
    }

    uint32_t FlexErrorCorrector::countBits(uint32_t data) const { // checked
        // Efficient bit counting using Brian Kernighan's algorithm
        // variation or compiler builtin if available

#ifdef USE_BUILTIN_POPCOUNT
        return __builtin_popcount(data);
#else
        // Manual bit counting for portability
        // This algorithm counts bits in parallel using bit manipulation
        uint32_t n = (data >> 1) & 0x77777777;
        data = data - n;
        n = (n >> 1) & 0x77777777;
        data = data - n;
        n = (n >> 1) & 0x77777777;
        data = data - n;
        data = (data + (data >> 4)) & 0x0f0f0f0f;
        data = data * 0x01010101;
        return data >> 24;
#endif
    }

} // namespace flex_next_decoder

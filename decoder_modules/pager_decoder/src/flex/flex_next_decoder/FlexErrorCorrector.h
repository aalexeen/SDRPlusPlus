#pragma once

#include "FlexTypes.h"
#include "../../BCHCode.h"
#include "FlexNextDecoder.h"

#include <memory>
#include <cstdint>


class BCHCode;
namespace flex_next_decoder {

    /**
     * @class FlexErrorCorrector
     * @brief BCH Error Correction for FLEX Protocol
     *
     * Provides BCH(31,21,5) error correction functionality for FLEX paging protocol.
     * Can correct up to 2 bit errors in 32-bit data words.
     *
     * Uses RAII for automatic resource management of BCH code tables.
     */
    class FlexErrorCorrector : public FlexNextDecoder {
    public:
        /**
         * @brief Constructor - initializes BCH(31,21,5) error correction
         * @throws std::runtime_error if BCH initialization fails
         */
        FlexErrorCorrector();
        explicit FlexErrorCorrector(int verbosity_level);

        /**
         * @brief Destructor - automatically cleans up resources
         */
        ~FlexErrorCorrector() = default;

        // Delete copy operations (BCHCode is not copyable)
        FlexErrorCorrector(const FlexErrorCorrector&) = delete;
        FlexErrorCorrector& operator=(const FlexErrorCorrector&) = delete;

        // Allow move operations
        FlexErrorCorrector(FlexErrorCorrector&&) = default;
        FlexErrorCorrector& operator=(FlexErrorCorrector&&) = default;

        /**
         * @brief Fix bit errors in 32-bit data word using BCH correction
         * @param data 32-bit data word to correct (modified in place)
         * @param phase_id Phase identifier for logging ('A', 'B', 'C', 'D', 'F')
         * @return true if successful (0-2 errors corrected), false if uncorrectable
         *
         * Converts 32-bit word to BCH format, applies error correction,
         * and converts back. Reports number of errors fixed.
         */
        bool fixErrors(uint32_t& data, char phase_id);

    private:
        /**
         * @brief Count number of set bits in a 32-bit word
         * @param data 32-bit word to count
         * @return Number of '1' bits in the word
         *
         * Uses efficient bit counting algorithm or compiler builtin if available.
         */
        uint32_t countBits(uint32_t data) const;


        /**
         * @brief BCH(31,21,5) error correction engine
         *
         * Manages BCH lookup tables and correction algorithms.
         * Automatically initialized with FLEX-specific parameters:
         * - Generator polynomial: x^5 + x^2 + 1 (binary: 100101)
         * - Field order: m=5 (GF(2^5))
         * - Code length: n=31
         * - Data length: k=21
         * - Error capability: t=2
         */
        std::unique_ptr<BCHCode> bch_code_;
    };

} // namespace flex_next_decoder
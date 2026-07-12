#pragma once

#include "FlexNextDecoder.h"
#include "FlexTypes.h"
#include <cstdint>

namespace flex_next_decoder {

    /**
     * @class FlexSynchronizer
     * @brief FLEX Protocol Synchronization and Mode Detection
     *
     * Detects FLEX sync patterns and decodes transmission parameters.
     *
     * FLEX Sync Code Structure (64-bit):
     * AAAA:BBBBBBBB:CCCC
     *
     * Where:
     * - BBBBBBBB is always 0xA6C6AAAA (sync marker)
     * - AAAA^CCCC should equal 0xFFFF (error detection)
     * - AAAA determines baud rate and FSK levels
     *
     * Supports both positive and negative (inverted) signal polarity.
     * Uses Hamming distance < 4 for error-tolerant pattern matching.
     */
    class FlexSynchronizer : public FlexNextDecoder {
    public:
        /**
         * @brief Constructor
         */
        FlexSynchronizer();

        FlexSynchronizer(int verbosity_level);

        /**
         * @brief Destructor
         */
        ~FlexSynchronizer() override = default;

        // Copy and move operations
        FlexSynchronizer(const FlexSynchronizer&) = default;
        FlexSynchronizer& operator=(const FlexSynchronizer&) = default;
        FlexSynchronizer(FlexSynchronizer&&) = default;
        FlexSynchronizer& operator=(FlexSynchronizer&&) = default;

        //=========================================================================
        // Main Synchronization Interface
        //=========================================================================

        /**
         * @brief Process symbol and detect sync patterns
         * @param symbol 4-level FSK symbol (0-3)
         * @return Sync code if detected, 0 if no sync found
         *
         * Accumulates symbols into 64-bit buffer and checks for FLEX sync patterns.
         * Automatically detects signal polarity (normal or inverted).
         */
        uint32_t processSymbol(uint8_t symbol);

        /**
         * @brief Decode sync code to transmission parameters
         * @param sync_code Sync code from processSymbol()
         * @param sync_info Output structure for decoded parameters
         * @return true if sync code was recognized, false if unknown
         *
         * Decodes sync code to determine:
         * - Baud rate (1600 or 3200 bps)
         * - FSK levels (2 or 4)
         * - Signal polarity (normal or inverted)
         */
        bool decodeSyncMode(uint32_t sync_code, SyncInfo& sync_info);

        //=========================================================================
        // State Management
        //=========================================================================

        /**
         * @brief Reset synchronizer to initial state
         */
        void reset();

        /**
         * @brief Get current sync buffer contents (for debugging)
         * @return Current 64-bit sync buffer
         */
        uint64_t getSyncBuffer() const { return sync_buffer_; }

        /**
         * @brief Get last detected polarity
         * @return true if inverted polarity, false if normal
         */
        bool getLastPolarity() const { return last_polarity_; }

        /**
         * @brief Get symbol count processed since last reset
         * @return Number of symbols processed
         */
        uint32_t getSymbolCount() const { return symbol_count_; }

        // uint32_t getSyncBaud() const { return sync_baud; }

        // void setSyncBaud(uint32_t baud) { sync_baud = baud; }

        //=========================================================================
        // Utility Methods
        //=========================================================================

        /**
         * @brief Check if a sync code matches known FLEX modes
         * @param sync_code Sync code to check
         * @return true if sync code is recognized
         */
        static bool isValidSyncCode(uint32_t sync_code);

        /**
         * @brief Get FlexMode for a given sync code
         * @param sync_code Sync code to look up
         * @return FlexMode if found, or default mode if not found
         */
        static FlexMode getSyncModeInfo(uint32_t sync_code);

        /**
         * @brief Count number of differing bits between two values
         * @param a First value
         * @param b Second value
         * @return Hamming distance (number of differing bits)
         */
        static uint32_t countBitDifferences(uint32_t a, uint32_t b);

    private:
        //=========================================================================
        // Internal Sync Detection Methods
        //=========================================================================

        /**
         * @brief Check 64-bit buffer for FLEX sync pattern
         * @param buffer 64-bit buffer to check
         * @return Sync code if valid pattern found, 0 otherwise
         */
        uint32_t checkSyncPattern(uint64_t buffer);

        /**
         * @brief Validate FLEX sync pattern structure
         * @param buffer 64-bit sync pattern
         * @param marker_out Output for extracted marker
         * @param codehigh_out Output for extracted high code
         * @param codelow_out Output for extracted low code
         * @return true if pattern structure is valid
         */
        /*bool validateSyncStructure(uint64_t buffer,
                                  uint32_t& marker_out,
                                  uint16_t& codehigh_out,
                                  uint16_t& codelow_out) const;
                                  */

        //=========================================================================
        // State Variables
        //=========================================================================

        uint64_t sync_buffer_;  ///< 64-bit rolling sync pattern buffer
        bool last_polarity_;    ///< Last detected polarity (false=normal, true=inverted)
        uint32_t symbol_count_; ///< Number of symbols processed

        //=========================================================================
        // Constants
        //=========================================================================

        static constexpr uint32_t SYNC_MARKER = FLEX_SYNC_MARKER; ///< Expected sync marker value
        static constexpr int HAMMING_THRESHOLD = 4;               ///< Max hamming distance for match
        static constexpr uint16_t OUTER_CODE_XOR = 0xFFFF;        ///< Expected XOR of outer codes

        // Sync pattern bit positions in 64-bit word
        static constexpr int CODEHIGH_SHIFT = 48; ///< Upper 16 bits (AAAA)
        static constexpr int MARKER_SHIFT = 16;   ///< Middle 32 bits (BBBBBBBB)
        static constexpr int CODELOW_SHIFT = 0;   ///< Lower 16 bits (CCCC)

        static constexpr uint64_t CODEHIGH_MASK = 0xFFFF000000000000ULL;
        static constexpr uint64_t MARKER_MASK = 0x0000FFFFFFFF0000ULL;
        static constexpr uint64_t CODELOW_MASK = 0x000000000000FFFFULL;
    };

} // namespace flex_next_decoder
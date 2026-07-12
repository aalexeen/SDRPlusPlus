#pragma once

#include "FlexNextDecoder.h"
#include "FlexTypes.h"
#include <array>
#include <cstdint>

namespace flex_next_decoder {

    // PhaseBuffer is now properly defined in FlexTypes.h with all required methods

    /**
     * @struct DataCollectionStatus
     * @brief Status information about data collection process
     */
    struct DataCollectionStatus {
        uint32_t bit_counter = 0; // Total bits collected
        bool phase_toggle = false; // Phase interleaving toggle (3200 bps)
        uint32_t baud_rate = 1600; // Current baud rate (1600 or 3200)
        uint32_t fsk_levels = 2; // Current FSK levels (2 or 4)
        bool all_phases_idle = false; // All active phases have gone idle

        /**
         * @brief Check if data collection is complete
         * @return true if all active phases are idle
         */
        bool isComplete() const { return all_phases_idle; }
    };

    /**
     * @class FlexDataCollector
     * @brief RAII-managed FLEX phase data collection
     *
     * Collects and manages FLEX protocol phase data according to the interleaving
     * scheme used by different baud rates and FSK levels:
     *
     * - 1600 bps, 2-level: Phase A only
     * - 1600 bps, 4-level: Phase A and B (gray coded)
     * - 3200 bps, 2-level: Phase A and C (interleaved)
     * - 3200 bps, 4-level: Phase A, B, C, D (gray coded + interleaved)
     *
     * Handles symbol-to-bit conversion, deinterleaving, and idle detection.
     */
    class FlexDataCollector : public FlexNextDecoder {
    public:
        /**
         * @brief Constructor - initializes with default settings
         */
        FlexDataCollector();

        /**
         * @brief Constructor with specified verbosity level
         * @param verbosity_level Debug output level
         */
        explicit FlexDataCollector(int verbosity_level);

        /**
         * @brief Destructor - automatic cleanup via RAII
         */
        ~FlexDataCollector() = default;

        // Delete copy operations (manage unique state)
        FlexDataCollector(const FlexDataCollector &) = delete;
        FlexDataCollector &operator=(const FlexDataCollector &) = delete;

        // Allow move operations
        FlexDataCollector(FlexDataCollector &&) = default;
        FlexDataCollector &operator=(FlexDataCollector &&) = default;

        //=========================================================================
        // Data Collection Interface
        //=========================================================================

        /**
         * @brief Process a symbol and update phase buffers
         * @param symbol 4-level FSK symbol (0-3)
         * @return true if all active phases have gone idle
         *
         * Converts symbol to bits according to current FSK encoding,
         * distributes bits to appropriate phase buffers, and checks
         * for idle conditions indicating end of data.
         */
        bool processSymbol(u_char symbol, SyncInfo &sync_info);

        /**
         * @brief Clear all phase buffers and reset collection state
         */
        void reset();

        /**
         * @brief Set transmission parameters for data collection
         * @param baud_rate Baud rate (1600 or 3200)
         * @param fsk_levels FSK levels (2 or 4)
         */
        void setTransmissionMode(uint32_t baud_rate, uint32_t fsk_levels);

        //=========================================================================
        // Data Access Interface
        //=========================================================================

        /**
         * @brief Get phase A buffer (always active)
         * @return Reference to phase A buffer
         */
        const PhaseBuffer &getPhaseA() const { return phase_a_; }

        /**
         * @brief Get phase B buffer (active in 4-level modes)
         * @return Reference to phase B buffer
         */
        const PhaseBuffer &getPhaseB() const { return phase_b_; }

        /**
         * @brief Get phase C buffer (active in 3200 bps modes)
         * @return Reference to phase C buffer
         */
        const PhaseBuffer &getPhaseC() const { return phase_c_; }

        /**
         * @brief Get phase D buffer (active in 3200 bps, 4-level mode)
         * @return Reference to phase D buffer
         */
        const PhaseBuffer &getPhaseD() const { return phase_d_; }

        /**
         * @brief Get current collection status
         * @return Status structure with collection information
         */
        DataCollectionStatus getStatus() const;

        /**
         * @brief Check if all active phases are idle
         * @return true if data collection should be considered complete
         */
        bool areAllActivePhasesIdle() const;

        /**
         * @brief Get active phase count based on current mode
         * @return Number of phases active for current baud/level combination
         */
        uint32_t getActivePhaseCount() const;

    private:
        //=========================================================================
        // Symbol Processing Methods
        //=========================================================================

        /**
         * @brief Convert FSK symbol to phase bits
         * @param sym_rectified Input symbol (0-3)
         * @param bit_a Output bit for phase A
         * @param bit_b Output bit for phase B
         */
        void symbolToBits(uint8_t sym_rectified, bool &bit_a, bool &bit_b, SyncInfo &sync_info);

        /**
         * @brief Calculate deinterleaving index for current bit counter
         * @return Buffer index for current bit position
         */
        uint32_t calculateBufferIndex() const;

        /**
         * @brief Update phase buffers with new bits
         * @param bit_a Bit for phases A/C
         * @param bit_b Bit for phases B/D
         * @param buffer_index
         */
        void updatePhaseBuffers(bool bit_a, bool bit_b, uint32_t buffer_index);

        /**
         * @brief Check for idle patterns in phase buffers
         * @param buffer_index Current buffer index being updated
         */
        void checkForIdlePatterns(uint32_t buffer_index);

        /**
         * @brief Determine if a data word represents an idle pattern
         * @param data_word 32-bit data word to check
         * @return true if word is all 0s or all 1s (idle pattern)
         */
        static bool isIdlePattern(uint32_t data_word);

        //=========================================================================
        // State Variables
        //=========================================================================

        PhaseBuffer phase_a_; // Phase A buffer (always active)
        PhaseBuffer phase_b_; // Phase B buffer (4-level modes)
        PhaseBuffer phase_c_; // Phase C buffer (3200 bps modes)
        PhaseBuffer phase_d_; // Phase D buffer (3200 bps, 4-level)

        uint32_t data_bit_counter_; // Total bits collected
        bool phase_toggle_; // Phase interleaving toggle
        uint32_t baud_rate_; // Current baud rate
        uint32_t fsk_levels_; // Current FSK levels

        //=========================================================================
        // Constants
        //=========================================================================

        static constexpr uint32_t BAUD_1600 = 1600;
        static constexpr uint32_t BAUD_3200 = 3200;
        static constexpr uint32_t LEVELS_2FSK = 2;
        static constexpr uint32_t LEVELS_4FSK = 4;
        static constexpr uint32_t BIT_COUNTER_MASK = 0xFF;
        static constexpr uint32_t INDEX_HIGH_MASK = 0xFFF8;
        static constexpr uint32_t INDEX_LOW_MASK = 0x0007;
        static constexpr uint32_t MSB_MASK = 0x80000000;
    };

} // namespace flex_next_decoder

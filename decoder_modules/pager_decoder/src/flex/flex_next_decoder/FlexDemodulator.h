#pragma once

#include "FlexNextDecoder.h"
#include "FlexStateMachine.h"
#include "FlexTypes.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <ostream>

namespace flex_next_decoder {

    /**
     * @class FlexDemodulator
     * @brief Pure Signal Processing Component for FLEX Protocol
     *
     * Focused implementation extracted from original buildSymbol() function in demod_flex_next.c.
     * Handles ONLY the signal processing pipeline:
     * - DC offset removal and envelope detection
     * - Phase-locked loop for symbol timing recovery
     * - 4-level FSK symbol quantization and modal detection
     * - Lock pattern detection for timing synchronization
     *
     * Integrates with your existing architecture:
     * - FlexStateMachine: Manages protocol state transitions
     * - FlexDataCollector: Handles phase data collection and symbol processing
     * - FlexSynchronizer: Handles sync pattern detection
     * - FlexFrameProcessor: Handles frame processing and BCH error correction
     *
     * This class implements only the buildSymbol() logic and related signal processing
     * from the original C code, following your single-responsibility principle.
     */
    class FlexDemodulator : public FlexNextDecoder {
    public:
        /**
         * @brief Constructor - initialize demodulator parameters
         * @param sample_frequency Input sample rate (typically 22050 Hz)
         */
        explicit FlexDemodulator(uint32_t sample_frequency);
        FlexDemodulator(FlexStateMachine *flex_state_machine, uint32_t sample_frequency);
        FlexDemodulator(FlexStateMachine *flex_state_machine, uint32_t sample_frequency, int verbosity_level);

        /**
         * @brief Destructor - RAII cleanup
         */
        ~FlexDemodulator() = default;

        // Non-copyable but moveable (unique signal state)
        FlexDemodulator(const FlexDemodulator &) = delete;
        FlexDemodulator &operator=(const FlexDemodulator &) = delete;
        FlexDemodulator(FlexDemodulator &&) = default;
        FlexDemodulator &operator=(FlexDemodulator &&) = default;

        //=========================================================================
        // Main Signal Processing Interface (matches original buildSymbol)
        //=========================================================================

        /**
         * @brief Process audio sample and recover symbol timing
         * @param sample Audio sample from SDR++ pipeline
         * @return true when symbol period complete, symbol ready via getModalSymbol()
         *
         * Direct equivalent of buildSymbol() from original C code.
         * Performs complete PLL-based symbol timing recovery and quantization.
         * When true, call getModalSymbol() to get the 4-level FSK symbol (0-3).
         */
        // bool processSample(double sample);

        /**
         * @brief Main symbol timing recovery logic
         * @param sample Current input sample
         * @return true when symbol period complete
         *
         * Direct port of buildSymbol() function from demod_flex_next.c
         */
        bool buildSymbol(float sample);

        /**
         * @brief Get most recently detected symbol
         * @return 4-level FSK symbol (0-3) from completed symbol period
         *
         * Should be called immediately after processSample() returns true.
         * Pass this symbol to FlexDataCollector::processSymbol() for protocol processing.
         */

        /**
         * @brief Determine modal symbol and check for lock pattern
         *
         * Combines modal symbol detection and lock pattern checking from
         * original Flex_Demodulate() function.
         */
        void finalizeSymbol();

        /**
         * @brief Check symbol stream for lock pattern
         *
         * Equivalent to lock pattern detection in original Flex_Demodulate().
         */
        void checkLockPattern();

        uint8_t getModalSymbol() const {
            if (getVerbosityLevel() >= 5) { std::cout << typeid(*this).name() << "getModalSymbol called" << std::endl; }
            return modal_symbol_;
        }

        void timeout();

        //=========================================================================
        // Lock Management (matches original C behavior)
        //=========================================================================

        /**
         * @brief Check if demodulator has timing lock
         * @return true if phase-locked to signal timing
         *
         * Equivalent to flex->Demodulator.locked from original C code.
         */
        bool isLocked() const { return locked_; }

        /**
         * @brief Force lock state (used by external components)
         * @param locked New lock state
         */
        void setLocked(bool locked) { locked_ = locked; }

        /**
         * @brief Reset timing counters (called when lock acquired)
         *
         * Equivalent to clearing symbol_count and sample_count in original C code.
         */
        void resetCounters();

        /**
         * @brief Reset timeout counter (called on zero crossings)
         *
         * Equivalent to flex->Demodulator.timeout = 0 in original C code.
         */
        void resetTimeout() { timeout_counter_ = 0; }

        //=========================================================================
        // Baud Rate Control (matches original C code)
        //=========================================================================

        /**
         * @brief Set symbol timing baud rate
         * @param baud Target baud rate (1600 or 3200)
         *
         * Equivalent to flex->Demodulator.baud = baud in original C code.
         * Note: FIW is always decoded at 1600 bps regardless of data baud.
         */
        void setBaudRate(uint32_t baud) { current_baud_ = baud; }

        /**
         * @brief Get current baud rate setting
         * @return Current baud rate (1600 or 3200)
         */
        uint32_t getBaudRate() const { return current_baud_; }

        //=========================================================================
        // Signal Quality Monitoring (matches original C code)
        //=========================================================================

        /**
         * @brief Get signal envelope estimate
         * @return Current signal amplitude (flex->Modulation.envelope)
         */
        double getEnvelope() const { return envelope_; }

        /**
         * @brief Get measured symbol rate
         * @return Measured symbol rate in Hz (flex->Modulation.symbol_rate)
         */
        double getSymbolRate() const { return symbol_rate_; }

        /**
         * @brief Get DC offset estimate
         * @return Current DC bias (flex->Modulation.zero)
         */
        double getZeroOffset() const { return zero_offset_; }

    private:
        //=========================================================================
        // Core Signal Processing (from original buildSymbol)
        //=========================================================================

        /**
         * @brief Update DC offset removal filter
         * @param sample Raw input sample
         *
         * Equivalent to original IIR filter:
         * flex->Modulation.zero = (flex->Modulation.zero*(FREQ_SAMP*DC_OFFSET_FILTER) + sample) /
         * ((FREQ_SAMP*DC_OFFSET_FILTER) + 1);
         */
        void updateDCOffset(float sample);

        /**
         * @brief Update signal envelope estimate
         * @param sample DC-corrected sample
         *
         * Equivalent to envelope calculation during sync periods in original C code.
         */
        void updateEnvelope(float sample);

        /**
         * @brief Count symbol levels during mid-80% of symbol period
         * @param sample Current sample
         * @param phase_percent Phase position percentage (0-100)
         *
         * Equivalent to symcount[] updates in original C code.
         */
        void countSymbolLevels(float sample);

        /**
         * @brief Process zero crossings for PLL correction
         * @param sample Current sample
         * @param phase_percent Current phase percentage
         * @param phase_max Maximum phase value
         *
         * Direct port of zero crossing logic from original buildSymbol().
         */
        void processZeroCrossing(float sample, double phase_percent, int64_t phase_max);

        //=========================================================================
        // State Variables (matches original C structures)
        //=========================================================================

        FlexStateMachine *state_machine_; ///< Flex state machine (for callbacks)
        // Configuration
        uint32_t sample_frequency_; ///< flex->Demodulator.sample_freq
        uint32_t current_baud_; ///< flex->Demodulator.baud

        // Sample processing state
        double last_sample_; ///< flex->Demodulator.sample_last
        bool locked_; ///< flex->Demodulator.locked
        int64_t phase_; ///< flex->Demodulator.phase
        uint32_t sample_count_; ///< flex->Demodulator.sample_count
        uint32_t symbol_count_; ///< flex->Demodulator.symbol_count

        // Signal measurements
        double zero_offset_; ///< flex->Modulation.zero
        double envelope_; ///< flex->Modulation.envelope
        double envelope_sum_; ///< flex->Demodulator.envelope_sum
        int envelope_count_; ///< flex->Demodulator.envelope_count
        double symbol_rate_; ///< flex->Modulation.symbol_rate

        // Symbol detection
        std::array<int, 4> symbol_counts_; ///< flex->Demodulator.symcount[]
        uint8_t modal_symbol_; ///< Most recently detected symbol
        uint64_t lock_buffer_; ///< flex->Demodulator.lock_buf

        // Error monitoring
        int timeout_counter_; ///< flex->Demodulator.timeout
        int non_consecutive_counter_; ///< flex->Demodulator.nonconsec


        //=========================================================================
        // Constants (from original C #defines)
        //=========================================================================

        static constexpr double SLICE_THRESHOLD = 0.667; ///< SLICE_THRESHOLD
        static constexpr double DC_OFFSET_FILTER = 0.010; ///< DC_OFFSET_FILTER
        static constexpr double PHASE_LOCKED_RATE = 0.045; ///< PHASE_LOCKED_RATE
        static constexpr double PHASE_UNLOCKED_RATE = 0.050; ///< PHASE_UNLOCKED_RATE
        static constexpr int LOCK_LENGTH = 24; ///< LOCK_LEN
        static constexpr int DEMOD_TIMEOUT = 100; ///< DEMOD_TIMEOUT
        static constexpr uint64_t LOCK_PATTERN = 0x6666666666666666ULL; ///< From original C
    };

} // namespace flex_next_decoder

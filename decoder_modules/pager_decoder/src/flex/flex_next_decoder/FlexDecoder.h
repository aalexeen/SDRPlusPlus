#pragma once

#include "FlexNextDecoder.h"
#include "FlexTypes.h"
#include <memory>
#include <cstdint>

namespace flex_next_decoder {

    // Forward declarations for all subsystems
    class FlexDemodulator;
    class FlexStateMachine;
    class FlexSynchronizer;
    class FlexFrameProcessor;
    class FlexDataCollector;
    class FlexErrorCorrector;
    class FlexMessageDecoder;
    class FlexGroupHandler;
    class FlexOutputFormatter;

    /**
     * @class FlexDecoder
     * @brief Facade Pattern - Main FLEX Protocol Decoder Interface
     *
     * Coordinates all FLEX decoder subsystems through a simple, unified interface.
     * Replaces the original monolithic C implementation with a clean, maintainable
     * architecture that hides subsystem complexity behind RAII resource management.
     *
     * This class implements the main coordination logic from the original C functions:
     * - flex_next_demod() -> processSamples()
     * - flex_next_init() -> constructor
     * - flex_next_deinit() -> destructor (RAII)
     * - Flex_New()/Flex_Delete() -> constructor/destructor (RAII)
     *
     * Architecture Integration:
     * - SDR++ Audio Pipeline: processSamples(const float* samples, size_t count)
     * - Multimon-ng Compatible: Supports same interface patterns
     * - State Pattern: FlexStateMachine manages protocol states
     * - Strategy Pattern: FlexMessageDecoder handles different message types
     * - All other patterns: Coordinated through this Facade
     */
    class FlexDecoder : public FlexNextDecoder {
    public:
        /**
         * @brief Constructor - Initialize all FLEX decoder subsystems
         * @param sample_frequency Input sample rate (typically 22050 Hz)
         *
         * Equivalent to original flex_next_init() + Flex_New().
         * Uses RAII to automatically manage all subsystem resources.
         */
        explicit FlexDecoder(uint32_t sample_frequency = 22050);

        /**
         * @brief Constructor - Initialize all FLEX decoder subsystems with specified verbosity level
         * @param sample_frequency Input sample rate (typically 22050 Hz)
         * @param verbosity_level Debug output level
         *
         * Equivalent to original flex_next_init() + Flex_New().
         * Uses RAII to automatically manage all subsystem resources.
         */
        FlexDecoder(uint32_t sample_frequency, int verbosity_level);

        /**
         * @brief Destructor - Automatic cleanup via RAII
         *
         * Equivalent to original flex_next_deinit() + Flex_Delete().
         * All subsystems automatically cleaned up via unique_ptr.
         */
        ~FlexDecoder() override;

        // Non-copyable but moveable (manages unique decoder state)
        FlexDecoder(const FlexDecoder &) = delete;
        FlexDecoder &operator=(const FlexDecoder &) = delete;
        FlexDecoder(FlexDecoder &&) = default;
        FlexDecoder &operator=(FlexDecoder &&) = default;

        //=========================================================================
        // Main Interface (equivalent to original C functions)
        //=========================================================================

        /**
         * @brief Process batch of audio samples from SDR++ pipeline
         * @param samples Audio samples from DSP chain
         * @param count Number of samples to process
         *
         * Direct equivalent of original flex_next_demod() function.
         * Coordinates all subsystems to process samples and produce decoded messages.
         *
         * Processing pipeline:
         * 1. FlexDemodulator: Signal processing and symbol recovery
         * 2. FlexStateMachine: Protocol state management
         * 3. FlexSynchronizer: Sync pattern detection (SYNC1 state)
         * 4. FlexDataCollector: Phase data collection (DATA state)
         * 5. FlexFrameProcessor: Frame processing and BCH correction
         * 6. FlexMessageDecoder: Message parsing and group handling
         * 7. FlexOutputFormatter: Final output formatting
         */
        void processSamples(const float *samples, size_t count);

        /**
         * @brief Process single audio sample (for compatibility)
         * @param sample Single audio sample
         *
         * Convenience function for single-sample processing.
         */
        void processSample(float sample);

        /**
         * @brief Reset all decoder subsystems to initial state
         *
         * Coordinates reset across all subsystems. Called when:
         * - Sync is lost and need to restart
         * - Frequency/mode changes
         * - User manually resets decoder
         */
        void reset();

        //=========================================================================
        // Configuration Interface
        //=========================================================================

        /**
         * @brief Set verbosity level for debugging output
         * @param level Verbosity level (0=silent, 1=normal, 2=verbose, 3=debug)
         *
         * Propagates verbosity setting to all subsystems that support it.
         */
        void setVerbosityLevel(int level);

        /**
         * @brief Get current decoder state
         * @return Current FlexState (Sync1, FIW, Sync2, Data)
         */
        FlexState getCurrentState() const;

        /**
         * @brief Check if decoder is locked to signal timing
         * @return true if demodulator has timing lock
         */
        bool isLocked() const;

        /**
         * @brief Get signal quality metrics
         * @return Signal quality information (envelope, symbol rate, etc.)
         */
        struct SignalQuality {
            double envelope = 0.0;
            double symbol_rate = 0.0;
            double dc_offset = 0.0;
            bool locked = false;
            FlexState state = FlexState::Sync1;
        };

        SignalQuality getSignalQuality() const;

    private:
        //=========================================================================
        // Subsystem Coordination (main processing pipeline)
        //=========================================================================

        /**
         * @brief Process single sample through entire pipeline
         * @param sample Audio sample from SDR++
         *
         * Core coordination function that orchestrates all subsystems.
         * Implements the main processing loop from original Flex_Demodulate().
         */
        void processSingleSample(float sample);

        /**
         * @brief Handle symbol processing based on current state
         * @param symbol 4-level FSK symbol from demodulator
         *
         * State-specific symbol processing that coordinates between:
         * - FlexSynchronizer (SYNC1 state)
         * - Frame Information Word processing (FIW state)
         * - Data collection (DATA state)
         */
        void processSymbol(uint8_t symbol);

        /**
         * @brief Handle SYNC1 state - sync pattern detection
         * @param symbol Current symbol
         */
        void handleSync1State(uint8_t symbol);

        /**
         * @brief Handle FIW state - Frame Information Word processing
         * @param symbol Current symbol
         * @param sym_rectified
         * @param sync_info
         */
        void handleFIWState(uint8_t symbol, u_char sym_rectified, SyncInfo &sync_info);

        /**
         * @brief Handle SYNC2 state - second sync header
         * @param symbol Current symbol
         */
        void handleSync2State(uint8_t symbol);

        /**
         * @brief Handle DATA state - message data collection
         * @param symbol Current symbol
         */
        void handleDataState(uint8_t symbol);

        /**
         * @brief Process completed frame data
         *
         * Called when data collection is complete. Coordinates:
         * - FlexFrameProcessor: BCH error correction and frame parsing
         * - FlexMessageDecoder: Message content decoding
         * - FlexOutputFormatter: Final output generation
         */
        void processCompletedFrame();

        //=========================================================================
        // Subsystem Management (RAII-managed components)
        //=========================================================================

        // Core signal processing
        std::unique_ptr<FlexDemodulator> demodulator_; ///< Signal processing and symbol recovery
        std::unique_ptr<FlexStateMachine> state_machine_; ///< Protocol state management
        std::unique_ptr<FlexSynchronizer> synchronizer_; ///< Sync pattern detection

        // Frame and data processing
        std::unique_ptr<FlexFrameProcessor> frame_processor_; ///< Frame processing and BCH correction
        std::unique_ptr<FlexDataCollector> data_collector_; ///< Phase data collection and interleaving
        std::shared_ptr<FlexErrorCorrector> error_corrector_; ///< BCH error correction

        // Message processing
        std::shared_ptr<FlexMessageDecoder> message_decoder_; ///< Message parsing (Strategy pattern)
        std::shared_ptr<FlexGroupHandler> group_handler_; ///< Group messaging support
        std::unique_ptr<FlexOutputFormatter> output_formatter_; ///< Final output formatting

        //=========================================================================
        // State Variables
        //=========================================================================

        uint32_t sample_frequency_; ///< Input sample rate

        // Frame processing state
        uint32_t fiw_count_; ///< FIW bit counter
        uint32_t fiw_raw_data_; ///< Accumulated FIW data
        uint32_t sync2_count_; ///< SYNC2 symbol counter
        uint32_t data_count_; ///< DATA symbol counter

        SyncInfo sync_info_; ///< Sync pattern info

        // Timing parameters (from original C constants)
        static constexpr uint32_t FIW_DOTTING_BITS = 16; ///< FIW preamble bits
        static constexpr uint32_t FIW_TOTAL_BITS = 48; ///< Total FIW bits
        static constexpr uint32_t SYNC2_DURATION_MS = 25; ///< SYNC2 duration
        static constexpr uint32_t DATA_DURATION_MS = 1760; ///< DATA duration
    };

} // namespace flex_next_decoder

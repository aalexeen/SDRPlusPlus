#include "FlexDecoder.h"
#include "FlexDemodulator.h"
#include "FlexStateMachine.h"
#include "FlexSynchronizer.h"
#include "FlexFrameProcessor.h"
#include "FlexDataCollector.h"
#include "FlexErrorCorrector.h"
#include "FlexMessageDecoder.h"
#include "FlexGroupHandler.h"
#include "FlexOutputFormatter.h"
#include <iostream>

namespace flex_next_decoder {

    FlexDecoder::FlexDecoder(uint32_t sample_frequency) :
        FlexNextDecoder(2), sample_frequency_(sample_frequency), fiw_count_(0), fiw_raw_data_(0), sync2_count_(0),
        data_count_(0) {

        // Initialize all subsystems using RAII (equivalent to original Flex_New)
        try {
            // State machine with callbacks to coordinate subsystems
            state_machine_ = std::make_unique<FlexStateMachine>();
            std::cout << "FLEX_NEXT: StateMachine initialized" << std::endl;

            // Core signal processing subsystems
            demodulator_ = std::make_unique<FlexDemodulator>(state_machine_.get(), sample_frequency);
            std::cout << "FLEX_NEXT: Demodulator initialized (sample_freq=" << sample_frequency << ")" << std::endl;

            synchronizer_ = std::make_unique<FlexSynchronizer>();
            std::cout << "FLEX_NEXT: Synchronizer initialized" << std::endl;
            data_collector_ = std::make_unique<FlexDataCollector>();
            std::cout << "FLEX_NEXT: DataCollector initialized" << std::endl;

            // Error correction (equivalent to BCH initialization in original C)
            error_corrector_ = std::make_shared<FlexErrorCorrector>();
            std::cout << "FLEX_NEXT: ErrorCorrector initialized" << std::endl;

            // Message processing subsystems
            group_handler_ = std::make_shared<FlexGroupHandler>();
            std::cout << "FLEX_NEXT: GroupHandler initialized" << std::endl;

            // Output formatting
            output_formatter_ = std::make_unique<FlexOutputFormatter>();
            std::cout << "FLEX_NEXT: OutputFormatter initialized" << std::endl;

            message_decoder_ = std::make_shared<FlexMessageDecoder>(group_handler_, std::move(output_formatter_));
            std::cout << "FLEX_NEXT: MessageDecoder initialized" << std::endl;

            // Frame processing (depends on error corrector and message decoder)
            frame_processor_ = std::make_unique<FlexFrameProcessor>(error_corrector_, message_decoder_, group_handler_);
            std::cout << "FLEX_NEXT: FrameProcessor initialized" << std::endl;

            if (getVerbosityLevel() >= 2) {
                std::cout << "FLEX_NEXT: Decoder initialized (sample_freq=" << sample_frequency << ")" << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "FLEX_NEXT: Failed to initialize decoder: " << e.what() << std::endl;
            throw;
        }
    }

    FlexDecoder::FlexDecoder(uint32_t sample_frequency, int verbosity_level) :
        FlexNextDecoder(verbosity_level) // Initialize base class with specified verbosity level
        ,
        sample_frequency_(sample_frequency), fiw_count_(0), fiw_raw_data_(0), sync2_count_(0), data_count_(0) {

        // Initialize all subsystems using RAII (equivalent to original Flex_New)
        try {
            // State machine with callbacks to coordinate subsystems
            state_machine_ = std::make_unique<FlexStateMachine>(verbosity_level);
            std::cout << "FLEX_NEXT: StateMachine initialized" << std::endl;

            // Core signal processing subsystems
            demodulator_ = std::make_unique<FlexDemodulator>(state_machine_.get(), sample_frequency, verbosity_level);
            std::cout << "FLEX_NEXT: Demodulator initialized (sample_freq=" << sample_frequency << ")" << std::endl;

            synchronizer_ = std::make_unique<FlexSynchronizer>();
            std::cout << "FLEX_NEXT: Synchronizer initialized" << std::endl;
            data_collector_ = std::make_unique<FlexDataCollector>(verbosity_level);
            std::cout << "FLEX_NEXT: DataCollector initialized" << std::endl;

            // Error correction (equivalent to BCH initialization in original C)
            error_corrector_ = std::make_shared<FlexErrorCorrector>(verbosity_level);
            std::cout << "FLEX_NEXT: ErrorCorrector initialized" << std::endl;

            // Message processing subsystems
            group_handler_ = std::make_shared<FlexGroupHandler>(verbosity_level);
            std::cout << "FLEX_NEXT: GroupHandler initialized" << std::endl;

            // Output formatting
            output_formatter_ = std::make_unique<FlexOutputFormatter>(verbosity_level);
            std::cout << "FLEX_NEXT: OutputFormatter initialized" << std::endl;

            message_decoder_ =
                    std::make_shared<FlexMessageDecoder>(group_handler_, std::move(output_formatter_), verbosity_level);
            std::cout << "FLEX_NEXT: MessageDecoder initialized" << std::endl;

            // Frame processing (depends on error corrector and message decoder)
            frame_processor_ = std::make_unique<FlexFrameProcessor>(error_corrector_, message_decoder_, group_handler_,
                                                                    verbosity_level);
            std::cout << "FLEX_NEXT: FrameProcessor initialized" << std::endl;

            if (getVerbosityLevel() >= 2) {
                std::cout << "FLEX_NEXT: Decoder initialized (sample_freq=" << sample_frequency << ")" << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "FLEX_NEXT: Failed to initialize decoder: " << e.what() << std::endl;
            throw;
        }
    }

    FlexDecoder::~FlexDecoder() = default;

    void FlexDecoder::processSamples(const float *samples, size_t count) {
        // Direct equivalent of original flex_next_demod() function
        if (!samples) return;

        for (size_t i = 0; i < count; ++i) { processSingleSample(samples[i]); }
    }

    void FlexDecoder::processSample(float sample) { processSingleSample(sample); }

    void FlexDecoder::processSingleSample(float sample) {
        // Core coordination function - equivalent to original Flex_Demodulate() call chain
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "processSingleSample called" << std::endl;
        }

        // The baud rate of first syncword and FIW is always 1600, so set that
        // rate to start.
        demodulator_->setBaudRate(1600);

        // Step 1: Signal processing and symbol recovery
        if (demodulator_->buildSymbol(sample)) {
            // Symbol period complete - get the detected symbol
            demodulator_->finalizeSymbol();

            if (demodulator_->isLocked()) {
                // Step 2: Process symbol through state machine coordination
                processSymbol(demodulator_->getModalSymbol());
            } else {
                if (getVerbosityLevel() >= 3) {
                    std::cout << "FLEX_NEXT: Symbol not locked" << std::endl;
                }
                demodulator_->checkLockPattern(); // checked
            }

            demodulator_->timeout(); // checked
        }

        // original code report_state - changing Previous state to Current in state machine. Keep it in mind to implement later.
    }

    void FlexDecoder::processSymbol(uint8_t symbol) { // checked, original code flex_sym
        // Get current state from state machine
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": "
                      << "processSymbol called with symbol: " << static_cast<int>(symbol) << std::endl;
        }
        FlexState current_state = state_machine_->getCurrentState();
        u_char sym_rectified = synchronizer_->getLastPolarity() ? 3 - symbol : symbol;

        // State-specific symbol processing (from original flex_sym function)
        switch (current_state) {
            case FlexState::Sync1:
                handleSync1State(symbol);
                break;

            case FlexState::FIW:
                handleFIWState(symbol, sym_rectified, sync_info_);
                break;

            case FlexState::Sync2:
                handleSync2State(symbol);
                break;

            case FlexState::Data:
                handleDataState(symbol);
                break;
        }
    }

    void FlexDecoder::handleSync1State(uint8_t symbol) { // checked
        // Use FlexSynchronizer to detect sync patterns (from original flex_sync)
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "handleSync1State called" << std::endl;
        }
        uint32_t sync_code = synchronizer_->processSymbol(symbol); // checked

        if (sync_code != 0) {
            // Sync pattern detected - decode transmission mode
            if (synchronizer_->decodeSyncMode(sync_code, sync_info_)) {

                // Update demodulator with detected baud rate
                // demodulator_->setBaudRate(sync_info.baud_rate); // it is no necessary in original code

                // Configure data collector for transmission mode
                // data_collector_->setTransmissionMode(sync_info.baud_rate, sync_info.levels); // it is no necessary in
                // original code

                if (getVerbosityLevel() >= 2) {
                    std::cout << "FLEX_NEXT: SyncInfoWord: sync_code=0x" << std::hex << sync_code
                              << " baud=" << sync_info_.baud_rate << " levels=" << sync_info_.levels
                              << " polarity=" << (sync_info_.polarity ? "NEG" : "POS") << std::dec << std::endl;
                }

                // Transition to FIW state
                state_machine_->changeState(FlexState::FIW);
                // fiw_count_ = 0;
                // fiw_raw_data_ = 0;
            }
        } else {
            // No sync pattern detected - transition to SYNC1 state
            state_machine_->changeState(FlexState::Sync1);
        }

        fiw_count_ = 0;
        fiw_raw_data_ = 0;
        state_machine_->setFIWCount(0);
    }

    void FlexDecoder::handleFIWState(uint8_t symbol, u_char sym_rectified, SyncInfo &sync_info) { // checked so so
        // Process Frame Information Word (from original decode_fiw logic)
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "handleFIWState called" << std::endl;
        }
        fiw_count_++;
        state_machine_->setFIWCount(fiw_count_);

        // Skip 16 bits of dotting, then accumulate 32 bits of FIW
        if (fiw_count_ >= FIW_DOTTING_BITS) {
            // Accumulate FIW data (2FSK only for FIW)
            fiw_raw_data_ = (fiw_raw_data_ >> 1) | ((sym_rectified > 1) ? 0x80000000 : 0);
        }

        if (fiw_count_ == FIW_TOTAL_BITS) {
            // FIW complete - attempt BCH error correction
            uint32_t corrected_fiw = fiw_raw_data_;
            bool fiw_valid = error_corrector_->fixErrors(corrected_fiw, 'F'); // checked

            if (fiw_valid) {
                // Extract FIW fields (from original decode_fiw)
                uint32_t cycle_no = (corrected_fiw >> 4) & 0xF;
                uint32_t frame_no = (corrected_fiw >> 8) & 0x7F;
                uint32_t fix3 = (corrected_fiw >> 15) & 0x3F;

                // Validate checksum
                uint32_t checksum = (corrected_fiw & 0xF) + ((corrected_fiw >> 4) & 0xF) +
                                    ((corrected_fiw >> 8) & 0xF) + ((corrected_fiw >> 12) & 0xF) +
                                    ((corrected_fiw >> 16) & 0xF) + ((corrected_fiw >> 20) & 0x01);

                if ((checksum & 0xF) == 0xF) {
                    if (getVerbosityLevel() >= 2) {
                        int time_seconds = cycle_no * 4 * 60 + frame_no * 4 * 60 / 128;
                        std::cout << "FLEX_NEXT: FrameInfoWord: cycleno=" << cycle_no << " frameno=" << frame_no
                                  << " fix3=" << fix3 << " time=" << (time_seconds / 60) << ":" << (time_seconds % 60)
                                  << std::endl;
                    }

                    // Update frame processor with sync and FIW information
                    if (frame_processor_) { frame_processor_->updateSyncInfo(sync_info_, corrected_fiw); }

                    // Check for missed group messages
                    std::vector<int> missed_groups = group_handler_->checkAndCleanupMissedGroups(cycle_no, frame_no);
                    for (int group_bit: missed_groups) {
                        if (getVerbosityLevel() >= 3) {
                            std::cout << "FLEX_NEXT: Missed group message for group bit " << group_bit << std::endl;
                        }
                    }

                    // Transition to SYNC2 state
                    state_machine_->changeState(FlexState::Sync2);
                    sync2_count_ = 0;
                    state_machine_->setSync2Count(sync2_count_);
                    demodulator_->setBaudRate(sync_info_.baud_rate); // Reset to default
                } else {
                    if (getVerbosityLevel() >= 3) { std::cout << "FLEX_NEXT: Bad FIW checksum" << std::endl; }
                    state_machine_->changeState(FlexState::Sync1);
                }
            } else {
                if (getVerbosityLevel() >= 3) {
                    std::cout << "FLEX_NEXT: Unable to decode FIW, too much data corruption" << std::endl;
                }
                state_machine_->changeState(FlexState::Sync1);
            }
        }
    }

    void FlexDecoder::handleSync2State(uint8_t symbol) { // checked
        // SYNC2 is 25ms of idle bits at current baud rate (from original logic)
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "handleSync2State called" << std::endl;
        }
        /*if (symbol > 1) {
            // SYNC2 detected - transition to DATA state
            state_machine_->changeState(FlexState::Data);
            data_count_ = 0;
            data_collector_->reset(); // Clear phase buffers
        }*/
        sync2_count_++;

        uint32_t baud_rate = demodulator_->getBaudRate();
        uint32_t sync2_symbols = baud_rate * SYNC2_DURATION_MS / 1000;

        if (sync2_count_ == sync2_symbols) { // change >= to == as in original code
            // SYNC2 complete - transition to DATA state
            state_machine_->changeState(FlexState::Data);
            data_count_ = 0;
            data_collector_->reset(); // Clear phase buffers

            if (getVerbosityLevel() >= 2) { std::cout << "FLEX_NEXT: State: DATA" << std::endl; }
        }
    }

    void FlexDecoder::handleDataState(uint8_t symbol) { // checked
        // Process data symbols through data collector (from original read_data)
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "handleDataState called" << std::endl;
        }
        bool all_idle = data_collector_->processSymbol(symbol); // original code read_data
        data_count_++;

        uint32_t baud_rate = demodulator_->getBaudRate();
        uint32_t max_data_symbols = baud_rate * DATA_DURATION_MS / 1000;

        // Check for end of data period
        if (data_count_ == max_data_symbols || all_idle) { // change >= to == as in original code
            if (getVerbosityLevel() >= 4) { std::cout << "FLEX_NEXT: Data count: " << data_count_ << std::endl; }
            // Data collection complete - process the frame
            processCompletedFrame(); // original code decode_data

            // Return to SYNC1 state for next frame
            state_machine_->changeState(FlexState::Sync1);
            demodulator_->setBaudRate(1600); // Reset to default
            data_count_ = 0;
        }
    }

    void FlexDecoder::processCompletedFrame() { // checked
        // Process completed frame data through frame processor
        uint32_t baud_rate = demodulator_->getBaudRate(); // original code uses Sync structure
        uint32_t fsk_levels = data_collector_->getStatus().fsk_levels; // original code uses Sync structure

        // Extract cycle and frame numbers from last FIW
        uint32_t cycle_no = (fiw_raw_data_ >> 4) & 0xF;
        uint32_t frame_no = (fiw_raw_data_ >> 8) & 0x7F;

        // Process frame through all active phases
        auto result = frame_processor_->processFrame( // in original code decode_phase
                *data_collector_, baud_rate, fsk_levels, cycle_no, frame_no);

        if (getVerbosityLevel() >= 2) {
            std::cout << "FLEX_NEXT: Frame processing complete: " << result.successful_messages << "/"
                      << result.total_messages << " messages decoded" << std::endl;
        }

        // Output formatting happens automatically through callbacks in frame processor
    }

    void FlexDecoder::reset() {
        // Coordinate reset across all subsystems
        if (demodulator_) demodulator_->resetCounters();
        if (state_machine_) state_machine_->reset();
        if (synchronizer_) synchronizer_->reset();
        if (data_collector_) data_collector_->reset();
        if (group_handler_) group_handler_->reset();

        // Reset internal state
        fiw_count_ = 0;
        fiw_raw_data_ = 0;
        sync2_count_ = 0;
        data_count_ = 0;

        if (getVerbosityLevel() >= 2) { std::cout << "FLEX_NEXT: Decoder reset" << std::endl; }
    }

    void FlexDecoder::setVerbosityLevel(int level) {
        // Update base class verbosity level
        FlexNextDecoder::setVerbosityLevel(level);
        // Propagate to subsystems that support verbosity
        // (Individual subsystems can check getVerbosityLevel() via getter if needed)
    }

    FlexState FlexDecoder::getCurrentState() const {
        return state_machine_ ? state_machine_->getCurrentState() : FlexState::Sync1;
    }

    bool FlexDecoder::isLocked() const { return demodulator_ ? demodulator_->isLocked() : false; }

    FlexDecoder::SignalQuality FlexDecoder::getSignalQuality() const {
        SignalQuality quality;

        if (demodulator_) {
            quality.envelope = demodulator_->getEnvelope();
            quality.symbol_rate = demodulator_->getSymbolRate();
            quality.dc_offset = demodulator_->getZeroOffset();
            quality.locked = demodulator_->isLocked();
        }

        if (state_machine_) { quality.state = state_machine_->getCurrentState(); }

        return quality;
    }

} // namespace flex_next_decoder

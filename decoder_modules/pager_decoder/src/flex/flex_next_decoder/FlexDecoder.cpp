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

    FlexDecoder::FlexDecoder(uint32_t sample_frequency)
        : sample_frequency_(sample_frequency), verbosity_level_(1), fiw_count_(0), fiw_raw_data_(0), sync2_count_(0), data_count_(0) {

        // Initialize all subsystems using RAII (equivalent to original Flex_New)
        try {
            // Core signal processing subsystems
            demodulator_ = std::make_unique<FlexDemodulator>(sample_frequency);
            synchronizer_ = std::make_unique<FlexSynchronizer>();
            data_collector_ = std::make_unique<FlexDataCollector>();

            // Error correction (equivalent to BCH initialization in original C)
            error_corrector_ = std::make_shared<FlexErrorCorrector>();

            // Message processing subsystems
            group_handler_ = std::make_shared<FlexGroupHandler>();
            message_decoder_ = std::make_shared<FlexMessageDecoder>(group_handler_);

            // Frame processing (depends on error corrector and message decoder)
            frame_processor_ = std::make_unique<FlexFrameProcessor>(
                error_corrector_, message_decoder_, group_handler_);

            // State machine with callbacks to coordinate subsystems
            state_machine_ = std::make_unique<FlexStateMachine>();

            // Output formatting
            output_formatter_ = std::make_unique<FlexOutputFormatter>();

            if (verbosity_level_ >= 2) {
                std::cout << "FLEX_NEXT: Decoder initialized (sample_freq="
                          << sample_frequency << ")" << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "FLEX_NEXT: Failed to initialize decoder: " << e.what() << std::endl;
            throw;
        }
    }

    FlexDecoder::~FlexDecoder() = default;

    void FlexDecoder::processSamples(const float* samples, size_t count) {
        // Direct equivalent of original flex_next_demod() function
        if (!samples) return;

        for (size_t i = 0; i < count; ++i) {
            processSingleSample(samples[i]);
        }
    }

    void FlexDecoder::processSample(float sample) {
        processSingleSample(sample);
    }

    void FlexDecoder::processSingleSample(float sample) {
        // Core coordination function - equivalent to original Flex_Demodulate() call chain

        // Step 1: Signal processing and symbol recovery
        if (demodulator_->processSample(static_cast<double>(sample))) {
            // Symbol period complete - get the detected symbol
            uint8_t symbol = demodulator_->getModalSymbol();

            // Step 2: Process symbol through state machine coordination
            processSymbol(symbol);
        }
    }

    void FlexDecoder::processSymbol(uint8_t symbol) {
        // Get current state from state machine
        FlexState current_state = state_machine_->getCurrentState();

        // State-specific symbol processing (from original flex_sym function)
        switch (current_state) {
        case FlexState::Sync1:
            handleSync1State(symbol);
            break;

        case FlexState::FIW:
            handleFIWState(symbol);
            break;

        case FlexState::Sync2:
            handleSync2State(symbol);
            break;

        case FlexState::Data:
            handleDataState(symbol);
            break;
        }
    }

    void FlexDecoder::handleSync1State(uint8_t symbol) {
        // Use FlexSynchronizer to detect sync patterns (from original flex_sync)
        uint32_t sync_code = synchronizer_->processSymbol(symbol);

        if (sync_code != 0) {
            // Sync pattern detected - decode transmission mode
            SyncInfo sync_info;
            if (synchronizer_->decodeSyncMode(sync_code, sync_info)) {

                // Update demodulator with detected baud rate
                demodulator_->setBaudRate(sync_info.baud_rate);

                // Configure data collector for transmission mode
                data_collector_->setTransmissionMode(sync_info.baud_rate, sync_info.levels);

                if (verbosity_level_ >= 2) {
                    std::cout << "FLEX_NEXT: SyncInfoWord: sync_code=0x" << std::hex << sync_code
                              << " baud=" << sync_info.baud_rate
                              << " levels=" << sync_info.levels
                              << " polarity=" << (sync_info.polarity ? "NEG" : "POS")
                              << std::dec << std::endl;
                }

                // Transition to FIW state
                state_machine_->changeState(FlexState::FIW);
                fiw_count_ = 0;
                fiw_raw_data_ = 0;
            }
        }
    }

    void FlexDecoder::handleFIWState(uint8_t symbol) {
        // Process Frame Information Word (from original decode_fiw logic)
        fiw_count_++;

        // Skip 16 bits of dotting, then accumulate 32 bits of FIW
        if (fiw_count_ >= FIW_DOTTING_BITS) {
            // Accumulate FIW data (2FSK only for FIW)
            fiw_raw_data_ = (fiw_raw_data_ >> 1) | ((symbol > 1) ? 0x80000000 : 0);
        }

        if (fiw_count_ == FIW_TOTAL_BITS) {
            // FIW complete - attempt BCH error correction
            uint32_t corrected_fiw = fiw_raw_data_;
            bool fiw_valid = error_corrector_->fixErrors(corrected_fiw, 'F');

            if (fiw_valid) {
                // Extract FIW fields (from original decode_fiw)
                uint32_t cycle_no = (corrected_fiw >> 4) & 0xF;
                uint32_t frame_no = (corrected_fiw >> 8) & 0x7F;

                // Validate checksum
                uint32_t checksum = (corrected_fiw & 0xF) +
                                    ((corrected_fiw >> 4) & 0xF) +
                                    ((corrected_fiw >> 8) & 0xF) +
                                    ((corrected_fiw >> 12) & 0xF) +
                                    ((corrected_fiw >> 16) & 0xF) +
                                    ((corrected_fiw >> 20) & 0x01);

                if ((checksum & 0xF) == 0xF) {
                    if (verbosity_level_ >= 2) {
                        int time_seconds = cycle_no * 4 * 60 + frame_no * 4 * 60 / 128;
                        std::cout << "FLEX_NEXT: FrameInfoWord: cycleno=" << cycle_no
                                  << " frameno=" << frame_no
                                  << " time=" << (time_seconds / 60) << ":" << (time_seconds % 60)
                                  << std::endl;
                    }

                    // Check for missed group messages
                    std::vector<int> missed_groups = group_handler_->checkAndCleanupMissedGroups(cycle_no, frame_no);
                    for (int group_bit : missed_groups) {
                        if (verbosity_level_ >= 3) {
                            std::cout << "FLEX_NEXT: Missed group message for group bit " << group_bit << std::endl;
                        }
                    }

                    // Transition to SYNC2 state
                    state_machine_->changeState(FlexState::Sync2);
                    sync2_count_ = 0;
                }
                else {
                    if (verbosity_level_ >= 3) {
                        std::cout << "FLEX_NEXT: Bad FIW checksum" << std::endl;
                    }
                    state_machine_->changeState(FlexState::Sync1);
                }
            }
            else {
                if (verbosity_level_ >= 3) {
                    std::cout << "FLEX_NEXT: Unable to decode FIW, too much data corruption" << std::endl;
                }
                state_machine_->changeState(FlexState::Sync1);
            }
        }
    }

    void FlexDecoder::handleSync2State(uint8_t symbol) {
        // SYNC2 is 25ms of idle bits at current baud rate (from original logic)
        sync2_count_++;

        uint32_t baud_rate = demodulator_->getBaudRate();
        uint32_t sync2_symbols = baud_rate * SYNC2_DURATION_MS / 1000;

        if (sync2_count_ >= sync2_symbols) {
            // SYNC2 complete - transition to DATA state
            state_machine_->changeState(FlexState::Data);
            data_count_ = 0;
            data_collector_->reset(); // Clear phase buffers

            if (verbosity_level_ >= 2) {
                std::cout << "FLEX_NEXT: State: DATA" << std::endl;
            }
        }
    }

    void FlexDecoder::handleDataState(uint8_t symbol) {
        // Process data symbols through data collector (from original read_data)
        bool all_idle = data_collector_->processSymbol(symbol);
        data_count_++;

        uint32_t baud_rate = demodulator_->getBaudRate();
        uint32_t max_data_symbols = baud_rate * DATA_DURATION_MS / 1000;

        // Check for end of data period
        if (data_count_ >= max_data_symbols || all_idle) {
            // Data collection complete - process the frame
            processCompletedFrame();

            // Return to SYNC1 state for next frame
            state_machine_->changeState(FlexState::Sync1);
            demodulator_->setBaudRate(1600); // Reset to default
            data_count_ = 0;
        }
    }

    void FlexDecoder::processCompletedFrame() {
        // Process completed frame data through frame processor
        uint32_t baud_rate = demodulator_->getBaudRate();
        uint32_t fsk_levels = data_collector_->getStatus().fsk_levels;

        // Extract cycle and frame numbers from last FIW
        uint32_t cycle_no = (fiw_raw_data_ >> 4) & 0xF;
        uint32_t frame_no = (fiw_raw_data_ >> 8) & 0x7F;

        // Process frame through all active phases
        auto result = frame_processor_->processFrame(
            *data_collector_, baud_rate, fsk_levels, cycle_no, frame_no);

        if (verbosity_level_ >= 3) {
            std::cout << "FLEX_NEXT: Frame processing complete: "
                      << result.successful_messages << "/" << result.total_messages
                      << " messages decoded" << std::endl;
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

        if (verbosity_level_ >= 2) {
            std::cout << "FLEX_NEXT: Decoder reset" << std::endl;
        }
    }

    void FlexDecoder::setVerbosityLevel(int level) {
        verbosity_level_ = level;
        // Propagate to subsystems that support verbosity
        // (Individual subsystems can check verbosity_level_ via getter if needed)
    }

    FlexState FlexDecoder::getCurrentState() const {
        return state_machine_ ? state_machine_->getCurrentState() : FlexState::Sync1;
    }

    bool FlexDecoder::isLocked() const {
        return demodulator_ ? demodulator_->isLocked() : false;
    }

    FlexDecoder::SignalQuality FlexDecoder::getSignalQuality() const {
        SignalQuality quality;

        if (demodulator_) {
            quality.envelope = demodulator_->getEnvelope();
            quality.symbol_rate = demodulator_->getSymbolRate();
            quality.dc_offset = demodulator_->getZeroOffset();
            quality.locked = demodulator_->isLocked();
        }

        if (state_machine_) {
            quality.state = state_machine_->getCurrentState();
        }

        return quality;
    }

} // namespace flex_next_decoder
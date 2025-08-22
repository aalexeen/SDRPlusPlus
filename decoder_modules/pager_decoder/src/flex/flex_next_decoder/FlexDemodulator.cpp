#include "FlexDemodulator.h"
#include <cmath>
#include <iostream>

namespace flex_next_decoder {
    FlexDemodulator::FlexDemodulator(FlexStateMachine *flex_state_machine, uint32_t sample_frequency) :
        state_machine_(flex_state_machine), sample_frequency_(sample_frequency),
        current_baud_(1600), // Always start at 1600 bps (original: flex->Demodulator.baud = 1600)
        last_sample_(0.0), locked_(false), phase_(0), sample_count_(0), symbol_count_(0), zero_offset_(0.0),
        envelope_(0.0), envelope_sum_(0.0), envelope_count_(0), symbol_rate_(0.0), modal_symbol_(0), lock_buffer_(0),
        timeout_counter_(0), non_consecutive_counter_(0) {
        symbol_counts_.fill(0);
    }

    FlexDemodulator::FlexDemodulator(FlexStateMachine *flex_state_machine, uint32_t sample_frequency,
                                     int verbosity_level) :
        FlexNextDecoder(verbosity_level), // Initialize base class with specified verbosity level
        state_machine_(flex_state_machine), sample_frequency_(sample_frequency),
        current_baud_(1600), // Always start at 1600 bps (original: flex->Demodulator.baud = 1600)
        last_sample_(0.0), locked_(false), phase_(0), sample_count_(0), symbol_count_(0), zero_offset_(0.0),
        envelope_(0.0), envelope_sum_(0.0), envelope_count_(0), symbol_rate_(0.0), modal_symbol_(0), lock_buffer_(0),
        timeout_counter_(0), non_consecutive_counter_(0) {
        symbol_counts_.fill(0);
    }

    /*bool FlexDemodulator::processSample(double sample) {
        // Direct equivalent of original buildSymbol() call from Flex_Demodulate()
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "processSample called" << std::endl;
        }
        return buildSymbol(sample);
    }*/

    void FlexDemodulator::resetCounters() {
        // Equivalent to original C code when lock is acquired:
        // flex->Demodulator.symbol_count = 0;
        // flex->Demodulator.sample_count = 0;
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "resetCounters called" << std::endl;
        }
        symbol_count_ = 0;
        sample_count_ = 0;
    }

    bool FlexDemodulator::buildSymbol(float sample) {
        // Direct port of buildSymbol() function from demod_flex_next.c
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "buildSymbol called" << std::endl;
        }
        const int64_t phase_max = 100 * sample_frequency_;
        // Maximum value for phase (calculated to divide by sample frequency without remainder)
        const int64_t phase_rate = phase_max * current_baud_ / sample_frequency_; // Increment per baseband sample
        const double phase_percent = 100.0 * phase_ / phase_max;

        // Update the sample counter
        sample_count_++;

        // Remove DC offset (IIR filter)
        if (state_machine_->getCurrentState() == FlexState::Sync1) { updateDCOffset(sample); }
        sample -= zero_offset_;

        if (locked_) {
            // During synchronization, establish signal envelope
            if (state_machine_->getCurrentState() == FlexState::Sync1) { updateEnvelope(sample); }
        } else {
            // Reset and hold in initial state (from original C code)
            envelope_ = 0.0;
            envelope_sum_ = 0.0;
            envelope_count_ = 0;
            current_baud_ = 1600;
            timeout_counter_ = 0;
            non_consecutive_counter_ = 0;
            state_machine_->changeState(FlexState::Sync1);
            if (getVerbosityLevel() >= 5) {
                std::cout << typeid(*this).name() << ": " << "resetCounters called" << std::endl;
            }
        }

        // Count symbol levels during MID 80% SYMBOL PERIOD
        if (phase_percent > 10.0 && phase_percent < 90.0) { countSymbolLevels(sample, phase_percent); }

        // Handle ZERO CROSSINGS for PLL
        processZeroCrossing(sample, phase_percent, phase_max);
        last_sample_ = sample;

        // END OF SYMBOL PERIOD check
        phase_ += phase_rate;

        if (phase_ > phase_max) {
            phase_ -= phase_max;

            // Symbol period complete - finalize symbol (from original Flex_Demodulate)
            finalizeSymbol();

            return true; // Symbol period complete
        }

        return false; // Still building symbol
    }

    void FlexDemodulator::updateDCOffset(float sample) { // checked
        // Direct port from original C code:
        // flex->Modulation.zero = (flex->Modulation.zero*(FREQ_SAMP*DC_OFFSET_FILTER) + sample) /
        // ((FREQ_SAMP*DC_OFFSET_FILTER) + 1);
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "updateDCOffset called" << std::endl;
        }
        const double filter_term = sample_frequency_ * DC_OFFSET_FILTER;
        zero_offset_ = (zero_offset_ * filter_term + sample) / (filter_term + 1.0);
    }

    void FlexDemodulator::updateEnvelope(float sample) { // checked
        // Direct port from original C code:
        // flex->Demodulator.envelope_sum += fabs(sample);
        // flex->Demodulator.envelope_count++;
        // flex->Modulation.envelope = flex->Demodulator.envelope_sum / flex->Demodulator.envelope_count;
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "updateEnvelope called" << std::endl;
        }
        envelope_sum_ += std::abs(sample);
        envelope_count_++;
        envelope_ = envelope_sum_ / envelope_count_;
    }

    void FlexDemodulator::countSymbolLevels(double sample, double phase_percent) {
        // Direct port from original C code symbol counting logic
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "countSymbolLevels called" << std::endl;
        }
        if (sample > 0.0) {
            if (sample > envelope_ * SLICE_THRESHOLD) {
                symbol_counts_[3]++; // Level 3 (highest positive)
            } else {
                symbol_counts_[2]++; // Level 2 (low positive)
            }
        } else {
            if (sample < -envelope_ * SLICE_THRESHOLD) {
                symbol_counts_[0]++; // Level 0 (lowest negative)
            } else {
                symbol_counts_[1]++; // Level 1 (low negative)
            }
        }
    }

    void FlexDemodulator::processZeroCrossing(double sample, double phase_percent, int64_t phase_max) {
        // Direct port of zero crossing logic from original buildSymbol()
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "processZeroCrossing called" << std::endl;
        }
        bool zero_crossing = (last_sample_ < 0.0 && sample >= 0.0) || (last_sample_ >= 0.0 && sample < 0.0);

        if (zero_crossing) {
            // The phase error has a direction towards the closest symbol boundary
            double phase_error;
            if (phase_percent < 50.0) {
                phase_error = phase_;
            } else {
                phase_error = phase_ - phase_max;
            }

            // Phase lock with the signal
            if (locked_) {
                phase_ -= static_cast<int64_t>(phase_error * PHASE_LOCKED_RATE);
            } else {
                phase_ -= static_cast<int64_t>(phase_error * PHASE_UNLOCKED_RATE);
            }

            // If too many zero crossings occur within mid 80% then indicate lock lost
            if (phase_percent > 10.0 && phase_percent < 90.0) {
                non_consecutive_counter_++;
                if (non_consecutive_counter_ > 20 && locked_) {
                    std::cout << "FLEX_NEXT: Synchronisation Lost\n";
                    locked_ = false;
                }
            } else {
                non_consecutive_counter_ = 0;
            }

            timeout_counter_ = 0;
        }
    }

    void FlexDemodulator::finalizeSymbol() {
        // Direct port from original Flex_Demodulate() function:
        // Combines symbol detection, rate calculation, and lock pattern checking

        // Determine the modal symbol (most frequent during symbol period)
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "finalizeSymbol called" << std::endl;
        }
        int max_count = 0;
        modal_symbol_ = 0;

        for (uint8_t j = 0; j < 4; j++) {
            if (symbol_counts_[j] > max_count) {
                modal_symbol_ = j;
                max_count = symbol_counts_[j];
            }
        }

        // Reset symbol counts for next symbol period
        symbol_counts_.fill(0);

        // Update symbol rate calculation (from original C code)
        symbol_count_++;
        symbol_rate_ = static_cast<double>(symbol_count_ * sample_frequency_) / sample_count_;

        // Reset non-consecutive counter on successful symbol
        non_consecutive_counter_ = 0;

        if (locked_) {
            // Symbol is ready for processing by FlexDataCollector
            // In original C code, this called flex_sym(flex, modal_symbol)
            // but that's now handled by your FlexDataCollector::processSymbol()
        } else {
            // Check for lock pattern when not locked
            checkLockPattern();
        }

        // Timeout check (from original C code)
        timeout_counter_++;
        if (timeout_counter_ > DEMOD_TIMEOUT) {
            std::cout << "FLEX_NEXT: Timeout\n";
            locked_ = false;
        }
    }

    void FlexDemodulator::checkLockPattern() {
        // Direct port from original Flex_Demodulate() function lock detection

        // Shift symbols into buffer, symbols are converted so that max and min symbols map to 1
        // (each contain a single 1 bit)
        if (getVerbosityLevel() >= 5) {
            std::cout << typeid(*this).name() << ": " << "checkLockPattern called" << std::endl;
        }
        lock_buffer_ = (lock_buffer_ << 2) | (modal_symbol_ ^ 0x1);

        uint64_t lock_pattern = lock_buffer_ ^ LOCK_PATTERN;
        uint64_t lock_mask = (1ULL << (2 * LOCK_LENGTH)) - 1;

        if ((lock_pattern & lock_mask) == 0 || ((~lock_pattern) & lock_mask) == 0) {
            std::cout << "FLEX_NEXT: Locked\n";
            locked_ = true;

            // Clear the synchronisation buffer (from original C code)
            lock_buffer_ = 0;
            resetCounters();
        }
    }
} // namespace flex_next_decoder

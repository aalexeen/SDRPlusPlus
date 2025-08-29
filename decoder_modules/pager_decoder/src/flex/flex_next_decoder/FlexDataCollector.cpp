#include "FlexDataCollector.h"
#include <algorithm>

namespace flex_next_decoder {

    //=============================================================================
    // FlexDataCollector Implementation
    //=============================================================================

    FlexDataCollector::FlexDataCollector() :
        data_bit_counter_(0), phase_toggle_(false), baud_rate_(BAUD_1600), fsk_levels_(LEVELS_2FSK) {

        // All phase buffers are automatically initialized to zero by PhaseBuffer constructor
    }

    FlexDataCollector::FlexDataCollector(int verbosity_level) :
        FlexNextDecoder(verbosity_level) // Initialize base class with specified verbosity level
        ,
        data_bit_counter_(0), phase_toggle_(false), baud_rate_(BAUD_1600), fsk_levels_(LEVELS_2FSK) {

        // All phase buffers are automatically initialized to zero by PhaseBuffer constructor
    }

    // read_data - rectified version of the process symbol
    bool FlexDataCollector::processSymbol(u_char sym_rectified, SyncInfo &sync_info) { // checked
        // Convert symbol to phase bits according to FSK encoding
        bool bit_a, bit_b;
        symbolToBits(sym_rectified, bit_a, bit_b, sync_info);

        // Handle phase toggle for different baud rates
        // baud_rate_ = sync_info.baud_rate;
        if (baud_rate_ == BAUD_1600) {
            phase_toggle_ = false; // No interleaving at 1600 bps
        }

        // Calculate buffer index using deinterleaving algorithm
        uint32_t buffer_index = calculateBufferIndex();

        // Update phase buffers based on current toggle state
        updatePhaseBuffers(bit_a, bit_b, buffer_index);

        // Check for idle patterns when complete words are formed
        if ((data_bit_counter_ & BIT_COUNTER_MASK) == BIT_COUNTER_MASK) { checkForIdlePatterns(buffer_index); }

        // Increment bit counter based on mode
        if (baud_rate_ == BAUD_1600 || !phase_toggle_) { // using struck Sync in original code
            data_bit_counter_++;
        }

        // Return true if all active phases have gone idle
        return areAllActivePhasesIdle();
    }

    void FlexDataCollector::reset() { // checked
        // Clear all phase buffers
        phase_a_.clear();
        phase_b_.clear();
        phase_c_.clear();
        phase_d_.clear();

        // Reset collection state
        data_bit_counter_ = 0;
        phase_toggle_ = false;
    }

    void FlexDataCollector::setTransmissionMode(uint32_t baud_rate, uint32_t fsk_levels) {
        baud_rate_ = baud_rate;
        fsk_levels_ = fsk_levels;

        // Reset phase toggle when mode changes
        phase_toggle_ = false;
    }

    DataCollectionStatus FlexDataCollector::getStatus() const {
        DataCollectionStatus status;
        status.bit_counter = data_bit_counter_;
        status.phase_toggle = phase_toggle_;
        status.baud_rate = baud_rate_;
        status.fsk_levels = fsk_levels_;
        status.all_phases_idle = areAllActivePhasesIdle();

        return status;
    }

    bool FlexDataCollector::areAllActivePhasesIdle() const { // checked
        // Determine which phases are active based on transmission mode
        bool idle = false;

        if (baud_rate_ == BAUD_1600) {
            if (fsk_levels_ == LEVELS_2FSK) {
                // 1600 bps, 2-level: Only Phase A active
                idle = phase_a_.isIdle();
            } else {
                // 1600 bps, 4-level: Phase A and B active
                idle = phase_a_.isIdle() && phase_b_.isIdle();
            }
        } else {
            // 3200 bps modes
            if (fsk_levels_ == LEVELS_2FSK) {
                // 3200 bps, 2-level: Phase A and C active
                idle = phase_a_.isIdle() && phase_c_.isIdle();
            } else {
                // 3200 bps, 4-level: All phases active
                idle = phase_a_.isIdle() && phase_b_.isIdle() && phase_c_.isIdle() && phase_d_.isIdle();
            }
        }

        return idle;
    }

    uint32_t FlexDataCollector::getActivePhaseCount() const {
        if (baud_rate_ == BAUD_1600) {
            return (fsk_levels_ == LEVELS_2FSK) ? 1 : 2;
        } else {
            return (fsk_levels_ == LEVELS_2FSK) ? 2 : 4;
        }
    }

    //=============================================================================
    // Private Methods Implementation
    //=============================================================================

    void FlexDataCollector::symbolToBits(u_char sym_rectified, bool &bit_a, bool &bit_b,
                                         SyncInfo &sync_info) { // checked
        // Phase A bit: '1' for symbols > 1, '0' otherwise
        bit_a = (sym_rectified > 1);

        // Phase B bit: only used in 4-level FSK
        bit_b = false;
        fsk_levels_ = sync_info.levels;
        if (fsk_levels_ == LEVELS_4FSK) {
            // Gray code mapping for 4-level FSK:
            // Symbol 0: Phase A=1, Phase B=1
            // Symbol 1: Phase A=1, Phase B=0
            // Symbol 2: Phase A=0, Phase B=0
            // Symbol 3: Phase A=0, Phase B=1
            bit_b = (sym_rectified == 1) || (sym_rectified == 2);
        }
    }

    uint32_t FlexDataCollector::calculateBufferIndex() const { // checked
        // Original algorithm: ((flex->Data.data_bit_counter>>5)&0xFFF8) | (flex->Data.data_bit_counter&0x0007)
        // This creates a deinterleaving pattern where:
        // Bits 0, 1, and 2 map straight through to give a 0-7 sequence that repeats 32 times
        // before moving to 8-15 repeating 32 times, etc.

        uint32_t high_part = (data_bit_counter_ >> 5) & INDEX_HIGH_MASK;
        uint32_t low_part = data_bit_counter_ & INDEX_LOW_MASK;

        return high_part | low_part;
    }

    void FlexDataCollector::updatePhaseBuffers(bool bit_a, bool bit_b, uint32_t buffer_index) { // checked
        // uint32_t buffer_index = calculateBufferIndex();

        // Ensure buffer index is within bounds
        /*if (buffer_index >= PHASE_WORDS) {
            return; // Skip if index would overflow buffer
        }*/

        if (!phase_toggle_) {
            // Update Phase A and B
            phase_a_.buffer[buffer_index] = (phase_a_.buffer[buffer_index] >> 1) | (bit_a ? MSB_MASK : 0);
            phase_b_.buffer[buffer_index] = (phase_b_.buffer[buffer_index] >> 1) | (bit_b ? MSB_MASK : 0);

            // Toggle for 3200 bps interleaving
            // if (baud_rate_ == BAUD_3200) {
            phase_toggle_ = true;
            //}
        } else {
            // Update Phase C and D (3200 bps interleaved)
            phase_c_.buffer[buffer_index] = (phase_c_.buffer[buffer_index] >> 1) | (bit_a ? MSB_MASK : 0);
            phase_d_.buffer[buffer_index] = (phase_d_.buffer[buffer_index] >> 1) | (bit_b ? MSB_MASK : 0);

            phase_toggle_ = false;
        }
    }

    void FlexDataCollector::checkForIdlePatterns(uint32_t buffer_index) { // checked
        // Only check when a complete 32-bit word has been formed
        // if (buffer_index >= PHASE_WORDS) { return; }

        if (!phase_toggle_) {
            // Check Phase A and B for idle patterns
            if (isIdlePattern(phase_a_.buffer[buffer_index])) { phase_a_.idle_count++; }
            if (isIdlePattern(phase_b_.buffer[buffer_index])) { phase_b_.idle_count++; }
        } else {
            // Check Phase C and D for idle patterns
            if (isIdlePattern(phase_c_.buffer[buffer_index])) { phase_c_.idle_count++; }
            if (isIdlePattern(phase_d_.buffer[buffer_index])) { phase_d_.idle_count++; }
        }
    }

    bool FlexDataCollector::isIdlePattern(uint32_t data_word) { // checked
        // Idle patterns are all zeros or all ones
        return (data_word == 0x00000000) || (data_word == 0xFFFFFFFF);
    }

} // namespace flex_next_decoder

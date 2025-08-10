#pragma once

#include "FlexTypes.h"
#include <array>

namespace flex_next_decoder {

    class FlexDemodulator {
    public:
        explicit FlexDemodulator(uint32_t sample_frequency);
        ~FlexDemodulator() = default;

        bool processSample(double sample);
        uint8_t getModalSymbol() const { return modal_symbol_; }

        bool isLocked() const { return locked_; }
        void setLocked(bool locked) { locked_ = locked; }

        void resetCounters();
        void setTimeout();

        // Getters for modulation info
        double getSymbolRate() const { return symbol_rate_; }
        double getEnvelope() const { return envelope_; }
        double getZeroOffset() const { return zero_offset_; }

        void setBaudRate(uint32_t baud) { current_baud_ = baud; }

    private:
        bool buildSymbol(double sample);
        void updateEnvelope(double sample);
        void checkLockPattern();

        uint32_t sample_frequency_;
        uint32_t current_baud_ = 1600;

        double last_sample_ = 0.0;
        bool locked_ = false;
        int64_t phase_ = 0;
        uint32_t sample_count_ = 0;
        uint32_t symbol_count_ = 0;

        double envelope_sum_ = 0.0;
        int envelope_count_ = 0;
        double envelope_ = 0.0;
        double zero_offset_ = 0.0;
        double symbol_rate_ = 0.0;

        uint64_t lock_buffer_ = 0;
        std::array<int, 4> symbol_counts_{};
        uint8_t modal_symbol_ = 0;

        int timeout_counter_ = 0;
        int non_consecutive_counter_ = 0;

        static constexpr double SLICE_THRESHOLD = 0.667;
        static constexpr double DC_OFFSET_FILTER = 0.010;
        static constexpr double PHASE_LOCKED_RATE = 0.045;
        static constexpr double PHASE_UNLOCKED_RATE = 0.050;
        static constexpr int LOCK_LENGTH = 24;
        static constexpr int DEMOD_TIMEOUT = 100;
    };

} // namespace flex_next_decoder
#pragma once

#include "FlexTypes.h"

namespace flex_next_decoder {

    class FlexStateMachine {
    public:
        FlexStateMachine() = default;
        ~FlexStateMachine() = default;

        FlexState getCurrentState() const { return current_state_; }
        FlexState getPreviousState() const { return previous_state_; }

        void setState(FlexState state);
        void reset();

        // State-specific counters
        uint32_t getFIWCount() const { return fiw_count_; }
        void incrementFIWCount() { ++fiw_count_; }
        void resetFIWCount() { fiw_count_ = 0; }

        uint32_t getSync2Count() const { return sync2_count_; }
        void incrementSync2Count() { ++sync2_count_; }
        void resetSync2Count() { sync2_count_ = 0; }

        uint32_t getDataCount() const { return data_count_; }
        void incrementDataCount() { ++data_count_; }
        void resetDataCount() { data_count_ = 0; }

        bool hasStateChanged() const { return current_state_ != previous_state_; }

    private:
        FlexState current_state_ = FlexState::Sync1;
        FlexState previous_state_ = FlexState::Sync1;

        uint32_t fiw_count_ = 0;
        uint32_t sync2_count_ = 0;
        uint32_t data_count_ = 0;
    };

} // namespace flex_next_decoder
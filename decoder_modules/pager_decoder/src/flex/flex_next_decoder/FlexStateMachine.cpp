#include "FlexStateMachine.h"
#include <iostream>

namespace flex_next_decoder {

FlexStateMachine::FlexStateMachine() 
    : current_state_(FlexState::Sync1)
    , previous_state_(FlexState::Sync1)
    , fiw_count_(0)
    , sync2_count_(0)
    , data_count_(0) {
}

void FlexStateMachine::setState(FlexState new_state) {
    if (new_state != current_state_) {
        previous_state_ = current_state_;
        current_state_ = new_state;
        
        // Report state change immediately
        reportStateChange();
    }
}

void FlexStateMachine::reset() {
    setState(FlexState::Sync1);
    fiw_count_ = 0;
    sync2_count_ = 0; 
    data_count_ = 0;
}

void FlexStateMachine::reportStateChange() {
    if (hasStateChanged()) {
        std::cout << "FLEX_NEXT: State: " << getCurrentStateName() << std::endl;
        // Update previous state to current so we don't report again
        previous_state_ = current_state_;
    }
}

//=============================================================================
// State Transition Logic Helpers
//=============================================================================

bool FlexStateMachine::shouldTransitionToFIW(bool sync_found) {
    if (current_state_ != FlexState::Sync1) {
        return false;
    }
    
    if (sync_found) {
        // Reset FIW counter for new frame
        resetFIWCount();
        return true;
    }
    
    return false;
}

FlexState FlexStateMachine::shouldTransitionFromFIW(bool fiw_decode_success) {
    if (current_state_ != FlexState::FIW) {
        return current_state_;  // No transition
    }
    
    // Only check transition when we've collected all FIW bits
    if (fiw_count_ >= FIW_TOTAL_BITS) {
        if (fiw_decode_success) {
            // Success: move to SYNC2 and reset its counter
            resetSync2Count();
            return FlexState::Sync2;
        } else {
            // Failure: go back to SYNC1
            return FlexState::Sync1;
        }
    }
    
    return FlexState::FIW;  // Stay in FIW, still collecting bits
}

bool FlexStateMachine::shouldTransitionToData(uint32_t baud_rate) {
    if (current_state_ != FlexState::Sync2) {
        return false;
    }
    
    // Calculate required SYNC2 symbols based on baud rate
    // SYNC2 is 25ms of idle bits: 25ms * baud_rate / 1000ms
    uint32_t required_sync2_count = (baud_rate * SYNC2_DURATION_MS) / 1000;
    
    if (sync2_count_ >= required_sync2_count) {
        // Reset data counter for new data phase
        resetDataCount();
        return true;
    }
    
    return false;
}

bool FlexStateMachine::shouldTransitionFromData(uint32_t baud_rate, bool idle_detected) {
    if (current_state_ != FlexState::Data) {
        return false;
    }
    
    // Transition on idle detection OR when data duration is complete
    if (idle_detected) {
        return true;
    }
    
    // Calculate required DATA symbols based on baud rate  
    // DATA is 1760ms: 1760ms * baud_rate / 1000ms
    uint32_t required_data_count = (baud_rate * DATA_DURATION_MS) / 1000;
    
    return (data_count_ >= required_data_count);
}

//=============================================================================
// Utility Methods
//=============================================================================

std::string FlexStateMachine::getCurrentStateName() const {
    return getStateName(current_state_);
}

std::string FlexStateMachine::getStateName(FlexState state) {
    switch (state) {
        case FlexState::Sync1:
            return "SYNC1";
        case FlexState::FIW:
            return "FIW";
        case FlexState::Sync2:
            return "SYNC2";
        case FlexState::Data:
            return "DATA";
        default:
            return "UNKNOWN";
    }
}

} // namespace flex_next_decoder
#include "FlexStateMachine.h"
#include <iostream>

namespace flex_next_decoder {

//=============================================================================
// FlexStateMachine Implementation (Context)
//=============================================================================

FlexStateMachine::FlexStateMachine(const FlexStateCallbacks& callbacks)
    : previous_state_(FlexState::Sync1)
    , fiw_count_(0)
    , sync2_count_(0)
    , data_count_(0)
    , baud_rate_(1600)
    , fiw_raw_data_(0)
    , callbacks_(callbacks) {

    initializeStates();
}

FlexStateMachine::FlexStateMachine()
    : previous_state_(FlexState::Sync1)
    , fiw_count_(0)
    , sync2_count_(0)
    , data_count_(0)
    , baud_rate_(1600)
    , fiw_raw_data_(0) {

    initializeStates();
}

void FlexStateMachine::initializeStates() {
    // ✅ Create all state objects
    states_[0] = std::make_unique<Sync1State>();
    states_[1] = std::make_unique<FIWState>();
    states_[2] = std::make_unique<Sync2State>();
    states_[3] = std::make_unique<DataState>();

    // ✅ Start in SYNC1 state
    current_state_ = states_[0].get();
    current_state_->onEnter(*this);
}

void FlexStateMachine::setCallbacks(const FlexStateCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void FlexStateMachine::processSymbol(uint8_t symbol) {
    // ✅ Delegate to current state (core of State Pattern)
    FlexState next_state = current_state_->processSymbol(*this, symbol);

    // ✅ Transition if state changed
    if (next_state != current_state_->getStateType()) {
        changeState(next_state);
    }
}

void FlexStateMachine::changeState(FlexState new_state) {
    if (new_state != current_state_->getStateType()) {
        // ✅ Exit current state
        current_state_->onExit(*this);
        previous_state_ = current_state_->getStateType();

        // ✅ Enter new state
        size_t state_index = static_cast<size_t>(new_state);
        current_state_ = states_[state_index].get();
        current_state_->onEnter(*this);

        // ✅ Report state change
        reportStateChange();
    }
}

void FlexStateMachine::reset() {
    changeState(FlexState::Sync1);
    fiw_count_ = 0;
    sync2_count_ = 0;
    data_count_ = 0;
    baud_rate_ = 1600;
    fiw_raw_data_ = 0;
}

FlexState FlexStateMachine::getCurrentState() const {
    return current_state_->getStateType();
}

std::string FlexStateMachine::getCurrentStateName() const {
    return current_state_->getStateName();
}

void FlexStateMachine::reportStateChange() {
    std::cout << "FLEX_NEXT: State: " << getCurrentStateName() << std::endl;
}

//=============================================================================
// Sync1State Implementation
//=============================================================================

void Sync1State::onEnter(FlexStateMachine& context) {
    // Reset when entering SYNC1 (start of new frame)
    context.resetFIWCount();
    context.setFIWRawData(0);
}

FlexState Sync1State::processSymbol(FlexStateMachine& context, uint8_t symbol) {
    // ✅ Look for sync pattern
    const auto& callbacks = context.getCallbacks();
    if (!callbacks.detect_sync) {
        return FlexState::Sync1;  // No sync detector available
    }

    uint32_t sync_code = callbacks.detect_sync(symbol);

    if (sync_code != 0) {
        // ✅ Sync found! Decode the mode and transition to FIW
        if (callbacks.decode_sync_mode) {
            callbacks.decode_sync_mode(sync_code);
        }

        std::cout << "FLEX_NEXT: Sync detected, code=0x" << std::hex << sync_code << std::dec << std::endl;
        return FlexState::FIW;
    }

    // ✅ Stay in SYNC1, keep looking
    return FlexState::Sync1;
}

//=============================================================================
// FIWState Implementation
//=============================================================================

void FIWState::onEnter(FlexStateMachine& context) {
    // Reset FIW processing
    context.resetFIWCount();
    context.setFIWRawData(0);
    std::cout << "FLEX_NEXT: Starting FIW collection" << std::endl;
}

FlexState FIWState::processSymbol(FlexStateMachine& context, uint8_t symbol) {
    // ✅ Increment FIW bit counter
    context.incrementFIWCount();

    // ✅ Accumulate FIW data after dotting bits
    if (context.getFIWCount() >= FlexStateMachine::FIW_DOTTING_BITS) {
        const auto& callbacks = context.getCallbacks();
        if (callbacks.accumulate_fiw) {
            uint32_t fiw_data = context.getFIWRawData();
            callbacks.accumulate_fiw(symbol, fiw_data);
            context.setFIWRawData(fiw_data);
        }
    }

    // ✅ Check if we have all FIW bits
    if (context.getFIWCount() >= FlexStateMachine::FIW_TOTAL_BITS) {
        // ✅ Process FIW and decide next state
        const auto& callbacks = context.getCallbacks();
        bool fiw_success = false;

        if (callbacks.process_fiw) {
            fiw_success = callbacks.process_fiw(context.getFIWRawData());
        }

        if (fiw_success) {
            std::cout << "FLEX_NEXT: FIW decoded successfully" << std::endl;
            return FlexState::Sync2;
        } else {
            std::cout << "FLEX_NEXT: FIW decode failed, returning to SYNC1" << std::endl;
            return FlexState::Sync1;
        }
    }

    // ✅ Stay in FIW, still collecting bits
    return FlexState::FIW;
}

//=============================================================================
// Sync2State Implementation
//=============================================================================

void Sync2State::onEnter(FlexStateMachine& context) {
    // Reset SYNC2 counter
    context.resetSync2Count();
    std::cout << "FLEX_NEXT: Starting SYNC2 processing at " << context.getBaudRate() << " bps" << std::endl;
}

FlexState Sync2State::processSymbol(FlexStateMachine& context, uint8_t symbol) {
    // ✅ Count SYNC2 symbols
    context.incrementSync2Count();

    // ✅ Calculate required SYNC2 duration based on baud rate
    uint32_t baud_rate = context.getBaudRate();
    uint32_t required_count = (baud_rate * FlexStateMachine::SYNC2_DURATION_MS) / 1000;

    // ✅ Check if SYNC2 period is complete
    if (context.getSync2Count() >= required_count) {
        std::cout << "FLEX_NEXT: SYNC2 complete, starting data collection" << std::endl;
        return FlexState::Data;
    }

    // ✅ Stay in SYNC2
    return FlexState::Sync2;
}

//=============================================================================
// DataState Implementation
//=============================================================================

void DataState::onEnter(FlexStateMachine& context) {
    // Reset data counter and clear phase buffers
    context.resetDataCount();

    const auto& callbacks = context.getCallbacks();
    if (callbacks.clear_phase_data) {
        callbacks.clear_phase_data();
    }

    std::cout << "FLEX_NEXT: Data collection started" << std::endl;
}

FlexState DataState::processSymbol(FlexStateMachine& context, uint8_t symbol) {
    // ✅ Read data symbol and check for idle
    const auto& callbacks = context.getCallbacks();
    bool idle_detected = false;

    if (callbacks.read_data) {
        idle_detected = callbacks.read_data(symbol);
    }

    // ✅ Count data symbols
    context.incrementDataCount();

    // ✅ Check for transition conditions
    bool should_transition = idle_detected;

    if (!should_transition) {
        // ✅ Check if data duration is complete
        uint32_t baud_rate = context.getBaudRate();
        uint32_t required_count = (baud_rate * FlexStateMachine::DATA_DURATION_MS) / 1000;
        should_transition = (context.getDataCount() >= required_count);
    }

    if (should_transition) {
        std::cout << "FLEX_NEXT: Data collection complete"
                  << (idle_detected ? " (idle detected)" : " (timeout)") << std::endl;
        return FlexState::Sync1;  // Return to start for next frame
    }

    // ✅ Stay in DATA
    return FlexState::Data;
}

void DataState::onExit(FlexStateMachine& context) {
    // ✅ Process collected data when leaving DATA state
    const auto& callbacks = context.getCallbacks();
    if (callbacks.process_collected_data) {
        callbacks.process_collected_data();
    }

    // ✅ Reset baud rate to default for next frame
    context.setBaudRate(1600);

    std::cout << "FLEX_NEXT: Processing collected FLEX data" << std::endl;
}

} // namespace flex_next_decoder
#pragma once

#include "FlexTypes.h"
#include <string>
#include <iostream>

namespace flex_next_decoder {

/**
 * @class FlexStateMachine
 * @brief FLEX Protocol State Machine
 *
 * Manages the four-state FLEX decoding process:
 * 1. SYNC1 - Looking for initial sync pattern
 * 2. FIW   - Processing Frame Information Word
 * 3. SYNC2 - Processing second sync header
 * 4. DATA  - Processing data payload
 *
 * Tracks state-specific counters and handles state transitions.
 * Provides logging when states change for debugging.
 */
class FlexStateMachine {
public:
    /**
     * @brief Constructor - initializes to SYNC1 state
     */
    FlexStateMachine();

    /**
     * @brief Destructor
     */
    ~FlexStateMachine() = default;

    // Copy and move operations
    FlexStateMachine(const FlexStateMachine&) = default;
    FlexStateMachine& operator=(const FlexStateMachine&) = default;
    FlexStateMachine(FlexStateMachine&&) = default;
    FlexStateMachine& operator=(FlexStateMachine&&) = default;

    //=========================================================================
    // State Access Methods
    //=========================================================================

    /**
     * @brief Get current state
     * @return Current FlexState
     */
    FlexState getCurrentState() const { return current_state_; }

    /**
     * @brief Get previous state
     * @return Previous FlexState
     */
    FlexState getPreviousState() const { return previous_state_; }

    /**
     * @brief Check if state has changed since last call to reportStateChange()
     * @return true if state changed
     */
    bool hasStateChanged() const { return current_state_ != previous_state_; }

    //=========================================================================
    // State Transition Methods
    //=========================================================================

    /**
     * @brief Change to new state and report if changed
     * @param new_state State to transition to
     */
    void setState(FlexState new_state);

    /**
     * @brief Reset state machine to initial state
     */
    void reset();

    /**
     * @brief Report state change if it occurred (for logging)
     */
    void reportStateChange();

    //=========================================================================
    // State-Specific Counter Methods
    //=========================================================================

    // FIW State Counters
    uint32_t getFIWCount() const { return fiw_count_; }
    void incrementFIWCount() { ++fiw_count_; }
    void resetFIWCount() { fiw_count_ = 0; }

    // SYNC2 State Counters
    uint32_t getSync2Count() const { return sync2_count_; }
    void incrementSync2Count() { ++sync2_count_; }
    void resetSync2Count() { sync2_count_ = 0; }

    // DATA State Counters
    uint32_t getDataCount() const { return data_count_; }
    void incrementDataCount() { ++data_count_; }
    void resetDataCount() { data_count_ = 0; }

    //=========================================================================
    // State Transition Logic Helpers
    //=========================================================================

    /**
     * @brief Check if ready to transition from SYNC1 to FIW
     * @param sync_found true if sync pattern was detected
     * @return true if should transition to FIW
     */
    bool shouldTransitionToFIW(bool sync_found);

    /**
     * @brief Check if ready to transition from FIW to SYNC2 or back to SYNC1
     * @param fiw_decode_success true if FIW was successfully decoded
     * @return Next state (SYNC2 if success, SYNC1 if failure)
     */
    FlexState shouldTransitionFromFIW(bool fiw_decode_success);

    /**
     * @brief Check if ready to transition from SYNC2 to DATA
     * @param baud_rate Current baud rate for timing calculation
     * @return true if should transition to DATA
     */
    bool shouldTransitionToData(uint32_t baud_rate);

    /**
     * @brief Check if ready to transition from DATA back to SYNC1
     * @param baud_rate Current baud rate for timing calculation
     * @param idle_detected true if idle condition detected
     * @return true if should transition back to SYNC1
     */
    bool shouldTransitionFromData(uint32_t baud_rate, bool idle_detected);

    //=========================================================================
    // Utility Methods
    //=========================================================================

    /**
     * @brief Get string representation of current state
     * @return State name as string
     */
    std::string getCurrentStateName() const;

    /**
     * @brief Get string representation of any state
     * @param state State to get name for
     * @return State name as string
     */
    static std::string getStateName(FlexState state);

private:
    //=========================================================================
    // State Variables
    //=========================================================================

    FlexState current_state_;          ///< Current state
    FlexState previous_state_;         ///< Previous state (for change detection)

    //=========================================================================
    // State-Specific Counters
    //=========================================================================

    uint32_t fiw_count_;              ///< Bit counter for FIW state (0-48)
    uint32_t sync2_count_;            ///< Symbol counter for SYNC2 state
    uint32_t data_count_;             ///< Symbol counter for DATA state

    //=========================================================================
    // Constants for State Transitions
    //=========================================================================

    static constexpr uint32_t FIW_DOTTING_BITS = 16;     ///< Bits to skip before FIW data
    static constexpr uint32_t FIW_TOTAL_BITS = 48;       ///< Total FIW bits (16 + 32)
    static constexpr uint32_t SYNC2_DURATION_MS = 25;    ///< SYNC2 duration in milliseconds
    static constexpr uint32_t DATA_DURATION_MS = 1760;   ///< DATA duration in milliseconds
};

} // namespace flex_next_decoder
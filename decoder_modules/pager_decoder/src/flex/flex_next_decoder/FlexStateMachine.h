#pragma once

#include "FlexTypes.h"
#include <memory>
#include <array>
#include <functional>
#include <string>

namespace flex_next_decoder {

    // Forward declarations
    class FlexStateMachine;
    class IFlexState;

    /**
     * @brief Callbacks for state machine to interact with external components
     *
     * These allow states to trigger actions without tight coupling to specific components.
     * The main decoder will provide these callbacks when creating the state machine.
     */
    struct FlexStateCallbacks {
        std::function<uint32_t(uint8_t)> detect_sync;           // Returns sync_code or 0
        std::function<void(uint32_t)> decode_sync_mode;         // Process sync code
        std::function<void(uint8_t, uint32_t&)> accumulate_fiw; // Accumulate FIW data
        std::function<bool(uint32_t)> process_fiw;              // Process FIW, return success
        std::function<void()> clear_phase_data;                 // Clear data buffers
        std::function<bool(uint8_t)> read_data;                 // Read data, return idle
        std::function<void()> process_collected_data;           // Process final data
    };

    /**
     * @interface IFlexState
     * @brief State Pattern interface for FLEX decoder states
     *
     * Each concrete state implements its own symbol processing logic
     * and decides when to transition to other states.
     */
    class IFlexState {
    public:
        virtual ~IFlexState() = default;

        /**
         * @brief Process a symbol in this state
         * @param context Reference to state machine context
         * @param symbol Symbol to process (0-3)
         * @return Next state to transition to (or current state to stay)
         */
        virtual FlexState processSymbol(FlexStateMachine& context, uint8_t symbol) = 0;

        /**
         * @brief Called when entering this state
         * @param context Reference to state machine context
         */
        virtual void onEnter(FlexStateMachine& context) {}

        /**
         * @brief Called when exiting this state
         * @param context Reference to state machine context
         */
        virtual void onExit(FlexStateMachine& context) {}

        /**
         * @brief Get the state type this object represents
         * @return FlexState enum value
         */
        virtual FlexState getStateType() const = 0;

        /**
         * @brief Get human-readable state name
         * @return State name string
         */
        virtual std::string getStateName() const = 0;
    };

    /**
     * @class FlexStateMachine
     * @brief Context class for State Pattern implementation
     *
     * Delegates symbol processing to current state object.
     * States can access context data and trigger transitions.
     */
    class FlexStateMachine {
    public:
        /**
         * @brief Constructor with callbacks for external component interaction
         * @param callbacks Functions for interacting with other FLEX components
         */
        explicit FlexStateMachine(const FlexStateCallbacks& callbacks);

        /**
         * @brief Default constructor (callbacks must be set later)
         */
        FlexStateMachine();

        /**
         * @brief Destructor
         */
        ~FlexStateMachine() = default;

        // Delete copy operations (unique_ptr members)
        FlexStateMachine(const FlexStateMachine&) = delete;
        FlexStateMachine& operator=(const FlexStateMachine&) = delete;

        // Allow move operations
        FlexStateMachine(FlexStateMachine&&) = default;
        FlexStateMachine& operator=(FlexStateMachine&&) = default;

        //=========================================================================
        // Main State Pattern Interface
        //=========================================================================

        /**
         * @brief Process symbol through current state
         * @param symbol Symbol to process (0-3)
         */
        void processSymbol(uint8_t symbol);

        /**
         * @brief Set callbacks for external component interaction
         * @param callbacks Callback functions
         */
        void setCallbacks(const FlexStateCallbacks& callbacks);

        void setState(size_t index);

        //=========================================================================
        // State Transition Methods (called by states)
        //=========================================================================

        /**
         * @brief Transition to new state
         * @param new_state State to transition to
         */
        void changeState(FlexState new_state);

        /**
         * @brief Reset to initial state
         */
        void reset();

        //=========================================================================
        // Context Data Access (for states)
        //=========================================================================

        // State queries
        FlexState getCurrentState() const;
        FlexState getPreviousState() const { return previous_state_; }
        std::string getCurrentStateName() const;

        // Counter access
        uint32_t getFIWCount() const { return fiw_count_; }
        void incrementFIWCount() { ++fiw_count_; }
        void setFIWCount(uint32_t count) { fiw_count_ = count; }
        void resetFIWCount() { fiw_count_ = 0; }

        uint32_t getSync2Count() const { return sync2_count_; }
        void incrementSync2Count() { ++sync2_count_; }
        void setSync2Count(uint32_t count) { sync2_count_ = count; }
        void resetSync2Count() { sync2_count_ = 0; }

        uint32_t getDataCount() const { return data_count_; }
        void incrementDataCount() { ++data_count_; }
        void resetDataCount() { data_count_ = 0; }

        // Shared data access
        uint32_t getBaudRate() const { return baud_rate_; }
        void setBaudRate(uint32_t baud) { baud_rate_ = baud; }

        uint32_t getFIWRawData() const { return fiw_raw_data_; }
        void setFIWRawData(uint32_t data) { fiw_raw_data_ = data; }

        // Callback access (for states to call external components)
        const FlexStateCallbacks& getCallbacks() const { return callbacks_; }

    private:
        //=========================================================================
        // State Pattern Implementation
        //=========================================================================

        IFlexState* current_state_;                         ///< Current active state
        std::array<std::unique_ptr<IFlexState>, 4> states_; ///< All state objects
        FlexState previous_state_;                          ///< Previous state for logging

        //=========================================================================
        // Context Data
        //=========================================================================

        // State-specific counters
        uint32_t fiw_count_;   ///< FIW bit counter (0-48)
        uint32_t sync2_count_; ///< SYNC2 symbol counter
        uint32_t data_count_;  ///< DATA symbol counter

        // Shared state data
        uint32_t baud_rate_;    ///< Current baud rate (1600/3200)
        uint32_t fiw_raw_data_; ///< Accumulated FIW data

        // External component callbacks
        FlexStateCallbacks callbacks_;

        int verbosity_level_;       ///< Debug output level

        //=========================================================================
        // Helper Methods
        //=========================================================================

        void initializeStates();
        void reportStateChange();

        //=========================================================================
        // Constants
        //=========================================================================

        static constexpr uint32_t FIW_DOTTING_BITS = 16;   ///< Bits to skip before FIW
        static constexpr uint32_t FIW_TOTAL_BITS = 48;     ///< Total FIW bits
        static constexpr uint32_t SYNC2_DURATION_MS = 25;  ///< SYNC2 duration (ms)
        static constexpr uint32_t DATA_DURATION_MS = 1760; ///< DATA duration (ms)

        // State objects need access to private members
        friend class Sync1State;
        friend class FIWState;
        friend class Sync2State;
        friend class DataState;
    };

    //=============================================================================
    // Concrete State Classes
    //=============================================================================

    /**
     * @class Sync1State
     * @brief SYNC1 state - looking for sync patterns
     */
    class Sync1State : public IFlexState {
    public:
        FlexState processSymbol(FlexStateMachine& context, uint8_t symbol) override;
        void onEnter(FlexStateMachine& context) override;
        FlexState getStateType() const override { return FlexState::Sync1; }
        std::string getStateName() const override { return "SYNC1"; }
    };

    /**
     * @class FIWState
     * @brief FIW state - processing Frame Information Word
     */
    class FIWState : public IFlexState {
    public:
        FlexState processSymbol(FlexStateMachine& context, uint8_t symbol) override;
        void onEnter(FlexStateMachine& context) override;
        FlexState getStateType() const override { return FlexState::FIW; }
        std::string getStateName() const override { return "FIW"; }
    };

    /**
     * @class Sync2State
     * @brief SYNC2 state - processing second sync header
     */
    class Sync2State : public IFlexState {
    public:
        FlexState processSymbol(FlexStateMachine& context, uint8_t symbol) override;
        void onEnter(FlexStateMachine& context) override;
        FlexState getStateType() const override { return FlexState::Sync2; }
        std::string getStateName() const override { return "SYNC2"; }
    };

    /**
     * @class DataState
     * @brief DATA state - processing data payload
     */
    class DataState : public IFlexState {
    public:
        FlexState processSymbol(FlexStateMachine& context, uint8_t symbol) override;
        void onEnter(FlexStateMachine& context) override;
        void onExit(FlexStateMachine& context) override;
        FlexState getStateType() const override { return FlexState::Data; }
        std::string getStateName() const override { return "DATA"; }
    };

} // namespace flex_next_decoder
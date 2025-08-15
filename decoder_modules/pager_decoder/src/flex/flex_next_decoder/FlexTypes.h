#pragma once

#include <cstdint>
#include <array>
#include <string>

namespace flex_next_decoder {

    constexpr size_t PHASE_WORDS = 88;
    constexpr size_t GROUP_BITS = 17;
    constexpr size_t MAX_ALN_LENGTH = 512;
    constexpr uint32_t FLEX_SYNC_MARKER = 0xA6C6AAAAul;

    // Signal processing constants
    constexpr double SLICE_THRESHOLD = 0.667;
    constexpr double DC_OFFSET_FILTER = 0.010;
    constexpr double PHASE_LOCKED_RATE = 0.045;
    constexpr double PHASE_UNLOCKED_RATE = 0.050;
    constexpr int LOCK_LENGTH = 24;
    constexpr int DEMOD_TIMEOUT = 100;
    constexpr int IDLE_THRESHOLD = 0;

    // Sample rate and filtering
    constexpr uint32_t FREQ_SAMP = 22050;
    constexpr int FILTER_LENGTH = 1;

    // Group messaging
    constexpr int CAPCODES_INDEX = 0;
    constexpr int REPORT_GROUP_CODES = 1;

    // Group capcode ranges
    constexpr int64_t GROUP_CAPCODE_MIN = 2029568;
    constexpr int64_t GROUP_CAPCODE_MAX = 2029583;
    constexpr int64_t MAX_CAPCODE = 4297068542LL;

    constexpr std::array<char, 17> FLEX_BCD = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
        ' ', 'U', ' ', '-', ']', '[', '\0'
    };

    struct FlexMode {
        uint32_t sync_code;
        uint32_t baud_rate;
        uint32_t levels;
    };

    constexpr std::array<FlexMode, 5> FLEX_MODES = { {
        { 0x870C, 1600, 2 }, // 1600 bps, 2-level FSK
        { 0xB068, 1600, 4 }, // 1600 bps, 4-level FSK
        { 0x7B18, 3200, 2 }, // 3200 bps, 2-level FSK
        { 0xDEA0, 3200, 4 }, // 3200 bps, 4-level FSK
        { 0x4C7C, 3200, 4 }  // 3200 bps, 4-level FSK (alternate)
    } };

    enum class FlexState : uint8_t {
        Sync1,
        FIW,
        Sync2,
        Data
    };

    enum class MessageType : uint8_t {
        Secure = 0,
        ShortInstruction = 1,
        Tone = 2,
        StandardNumeric = 3,
        SpecialNumeric = 4,
        Alphanumeric = 5,
        Binary = 6,
        NumberedNumeric = 7
    };

    struct DemodulatorState {
        uint32_t sample_frequency = 22050;
        double last_sample = 0.0;
        bool locked = false;
        int64_t phase = 0;
        uint32_t sample_count = 0;
        uint32_t symbol_count = 0;
        double envelope_sum = 0.0;
        int envelope_count = 0;
        uint64_t lock_buffer = 0;
        std::array<int, 4> symbol_counts{};
        int timeout_counter = 0;
        int non_consecutive_counter = 0;
        uint32_t current_baud = 1600;
    };

    struct ModulationState {
        double symbol_rate = 0.0;
        double envelope = 0.0;
        double zero_offset = 0.0;
    };

    struct PhaseBuffer {
        std::array<uint32_t, PHASE_WORDS> buffer{};
        int idle_count = 0;

        /**
         * @brief Clear all data and reset idle count
         */
        void clear() {
            buffer.fill(0);
            idle_count = 0;
        }

        /**
         * @brief Check if buffer contains only idle patterns
         * @return true if idle count exceeds threshold
         */
        bool isIdle() const { // checked
            return idle_count > IDLE_THRESHOLD;
        }

        /**
         * @brief Get read-only access to buffer data
         * @return Pointer to buffer data
         */
        const uint32_t* data() const {
            return buffer.data();
        }

        /**
         * @brief Get mutable access to buffer data (for internal use)
         * @return Pointer to buffer data
         */
        uint32_t* data() {
            return buffer.data();
        }

        /**
         * @brief Get buffer size
         * @return Number of words in buffer
         */
        constexpr size_t size() const {
            return PHASE_WORDS;
        }
    };

    struct PhaseData {
        bool phase_toggle = false;
        uint32_t data_bit_counter = 0;
        PhaseBuffer phase_a;
        PhaseBuffer phase_b;
        PhaseBuffer phase_c;
        PhaseBuffer phase_d;
    };

    struct GroupInfo {
        std::array<int64_t, 1000> capcodes{};
        int capcode_count = 0;
        int target_frame = -1;
        int target_cycle = -1;
    };

    using GroupHandlerArray = std::array<GroupInfo, GROUP_BITS>;

    struct StateMachineData {
        uint32_t fiw_count = 0;
        uint32_t sync2_count = 0;
        uint32_t data_count = 0;
        FlexState current_state = FlexState::Sync1;
        FlexState previous_state = FlexState::Sync1;
    };

    enum class FragmentFlag : char {
        Unknown = '?',
        Complete = 'K',    // OK to display
        Fragment = 'F',    // Needs continuation
        Continuation = 'C' // Completes fragments
    };

    struct ParsedMessage {
        std::string content;
        FragmentFlag fragment_flag = FragmentFlag::Unknown;
        bool is_group_message = false;
        int group_bit = -1;
    };

    struct SyncInfo {
        uint32_t sync_code = 0;
        uint32_t baud_rate = 0;
        uint32_t levels = 0;
        bool polarity = false;
        uint64_t sync_buffer = 0;
    };

    struct FrameInfo {
        uint32_t raw_data = 0;
        uint32_t checksum = 0;
        uint32_t cycle_number = 0;
        uint32_t frame_number = 0;
        uint32_t fix3 = 0;
    };

    struct MessageInfo {
        MessageType type = MessageType::Tone;
        bool long_address = false;
        int64_t capcode = 0;
    };

} // namespace flex_next_decoder
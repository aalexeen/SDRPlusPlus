#pragma once

#include "../FlexTypes.h"
#include <string>
#include <vector>
#include <cstdint>

namespace flex_next_decoder {

    /**
     * @struct MessageParseInput
     * @brief Input parameters for message parsing
     */
    struct MessageParseInput {
        // Message metadata
        MessageType type = MessageType::Tone;
        bool long_address = false;
        int64_t capcode = 0;

        // Phase data
        const uint32_t *phase_data = nullptr;
        uint32_t phase_data_size = 0;

        // Message location in phase data
        uint32_t message_word_start = 0; // mw1 - start of message data
        uint32_t message_length = 0; // len - length in words
        uint32_t vector_word_index = 0; // j - vector word index

        // Fragment information
        uint32_t fragment_number = 0; // frag (0-3)
        bool continuation_flag = false; // cont

        // Group messaging
        bool is_group_message = false;
        int group_bit = -1;

        // Frame information (for context)
        uint32_t cycle_number = 0;
        uint32_t frame_number = 0;

        // Sync information (needed for SyncInfo structure)
        uint32_t baud_rate = 1600; // FLEX baud rate (1600 or 3200)
        uint32_t levels = 2; // FSK levels (2 or 4)
        bool polarity = false; // Signal polarity (normal/inverted)

        // Phase identification (needed for output formatting)
        char phase_id = 'A'; // Phase identifier ('A', 'B', 'C', 'D')

        // Optional: Additional sync details for debugging/statistics
        uint32_t sync_code = 0; // Original sync code detected
        double symbol_rate = 0.0; // Actual symbol rate measured
        double envelope = 0.0; // Signal envelope level
        double zero_level = 0.0; // DC zero level
    };

    /**
     * @struct GroupMessageData
     * @brief Group message information
     */
    struct GroupMessageData {
        int group_bit = -1;
        std::vector<int64_t> capcodes;

        bool isEmpty() const;
    };

    /**
     * @struct MessageParseResult
     * @brief Result of message parsing operation
     */
    struct MessageParseResult {
        bool success = false;
        std::string content; // Parsed message content
        FragmentFlag fragment_flag = FragmentFlag::Unknown;
        GroupMessageData group_data; // Group message info if applicable
        std::string error_message; // Error description if parsing failed

        bool hasError() const;
    };

    /**
     * @interface IMessageParser
     * @brief Strategy Pattern interface for FLEX message parsing
     *
     * Each concrete parser implements message-type-specific decoding logic:
     * - AlphanumericParser: 7-bit ASCII text messages (ALN/SEC)
     * - NumericParser: BCD-encoded numeric messages (NUM/SPN/NPN)
     * - ToneParser: Tone-only and short numeric messages (TON)
     * - BinaryParser: Raw binary data (BIN) and unknown types
     */
    class IMessageParser {
    public:
        /**
         * @brief Virtual destructor for proper cleanup
         */
        virtual ~IMessageParser() = default;

        /**
         * @brief Parse message according to type-specific encoding
         * @param input Structured input parameters
         * @return Parsed message result with content and metadata
         */
        virtual MessageParseResult parseMessage(const MessageParseInput &input) const = 0;

        /**
         * @brief Check if this parser can handle the given message type
         * @param type Message type to check
         * @return true if this parser supports the message type
         */
        virtual bool canParse(MessageType type) const = 0;

        /**
         * @brief Get human-readable name of this parser
         * @return Parser name string
         */
        virtual std::string getParserName() const = 0;

        /**
         * @brief Get supported message types
         * @return Vector of supported MessageType values
         */
        virtual std::vector<MessageType> getSupportedTypes() const = 0;

    protected:
        /**
         * @brief Calculate fragment flag based on fragment and continuation bits
         * @param fragment_number Fragment number (0-3)
         * @param continuation_flag Continuation flag
         * @return Appropriate fragment flag
         */
        FragmentFlag calculateFragmentFlag(uint32_t fragment_number, bool continuation_flag) const;

        /**
         * @brief Validate input parameters for parsing
         * @param input Input parameters to validate
         * @return Error message if invalid, empty string if valid
         */
        std::string validateInput(const MessageParseInput &input) const;

        /**
         * @brief Safe character addition with encoding for special chars
         * @param ch Character to add
         * @param buffer Output buffer
         * @param max_size Maximum buffer size
         * @return Number of characters added (0, 1, or 2)
         */
        static uint32_t addCharacterSafe(unsigned char ch, std::string &buffer, uint32_t max_size);
    };

} // namespace flex_next_decoder

#pragma once

#include "IMessageParser.h"
#include "../FlexTypes.h"
#include <string>
#include <vector>

namespace flex_next_decoder {

/**
 * @class AlphanumericParser
 * @brief Parser for Alphanumeric and Secure FLEX messages
 *
 * Handles FLEX_PAGETYPE_ALPHANUMERIC and FLEX_PAGETYPE_SECURE message types.
 * These messages use 7-bit ASCII encoding with 3 characters packed per 32-bit data word.
 *
 * Data Word Format (21 bits used):
 * Bits 20-14: Character 3 (7 bits)
 * Bits 13-7:  Character 2 (7 bits)
 * Bits 6-0:   Character 1 (7 bits)
 *
 * Supports message fragmentation and group messaging functionality.
 */
class AlphanumericParser : public IMessageParser {
public:
    /**
     * @brief Constructor
     */
    AlphanumericParser() = default;

    /**
     * @brief Destructor
     */
    ~AlphanumericParser() override = default;

    // Delete copy operations (not needed for stateless parser)
    AlphanumericParser(const AlphanumericParser&) = delete;
    AlphanumericParser& operator=(const AlphanumericParser&) = delete;

    // Allow move operations
    AlphanumericParser(AlphanumericParser&&) = default;
    AlphanumericParser& operator=(AlphanumericParser&&) = default;

    /**
     * @brief Parse alphanumeric message from phase data
     * @param input Message parsing parameters
     * @return Parsed message result with 7-bit ASCII content
     */
    MessageParseResult parseMessage(const MessageParseInput& input) const override;

    /**
     * @brief Check if this parser supports the message type
     * @param type Message type to check
     * @return true for Alphanumeric and Secure types
     */
    bool canParse(MessageType type) const override;

    /**
     * @brief Get parser name
     * @return "AlphanumericParser"
     */
    std::string getParserName() const override;

    /**
     * @brief Get supported message types
     * @return Vector containing Alphanumeric and Secure types
     */
    std::vector<MessageType> getSupportedTypes() const override;

private:
    /**
     * @brief Process group message data
     * @param input Input parameters containing group information
     * @return GroupMessageData structure
     */
    GroupMessageData processGroupMessage(const MessageParseInput& input) const;

    /**
     * @brief Maximum alphanumeric message length from FlexTypes.h
     */
    static constexpr uint32_t MAX_MESSAGE_LENGTH = MAX_ALN_LENGTH;
};

} // namespace flex_next_decoder
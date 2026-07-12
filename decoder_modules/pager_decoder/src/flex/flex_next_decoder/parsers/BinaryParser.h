#pragma once

#include "IMessageParser.h"
#include "../FlexTypes.h"
#include <string>
#include <vector>

namespace flex_next_decoder {

/**
 * @class BinaryParser
 * @brief Parser for Binary FLEX messages and unknown message types
 *
 * Handles FLEX_PAGETYPE_BINARY message types and serves as the default
 * fallback parser for unknown message types.
 *
 * Provides simple hexadecimal representation of raw data words with
 * space separation between words. No special decoding is performed -
 * the raw 32-bit data words are output as 8-digit hex values.
 *
 * This parser is also used when the message type is not recognized
 * by any other parser, providing a raw data view for debugging.
 */
class BinaryParser : public IMessageParser {
public:
    /**
     * @brief Constructor
     */
    BinaryParser() = default;

    /**
     * @brief Destructor
     */
    ~BinaryParser() override = default;

    // Delete copy operations (not needed for stateless parser)
    BinaryParser(const BinaryParser&) = delete;
    BinaryParser& operator=(const BinaryParser&) = delete;

    // Allow move operations
    BinaryParser(BinaryParser&&) = default;
    BinaryParser& operator=(BinaryParser&&) = default;

    /**
     * @brief Parse binary message from phase data
     * @param input Message parsing parameters
     * @return Parsed message result with hexadecimal representation
     */
    MessageParseResult parseMessage(const MessageParseInput& input) const override;

    /**
     * @brief Check if this parser supports the message type
     * @param type Message type to check
     * @return true for Binary type, or can serve as fallback for any type
     */
    bool canParse(MessageType type) const override;

    /**
     * @brief Check if this parser can serve as fallback for unknown types
     * @return true - binary parser can handle any message type as fallback
     */
    bool canParseAsDefault() const;

    /**
     * @brief Get parser name
     * @return "BinaryParser"
     */
    std::string getParserName() const override;

    /**
     * @brief Get supported message types
     * @return Vector containing Binary type
     */
    std::vector<MessageType> getSupportedTypes() const override;

    /**
     * @brief Parse any message type as binary (fallback mode)
     * @param input Message parsing parameters
     * @return Parsed message result with raw hex data
     */
    MessageParseResult parseAsDefault(const MessageParseInput& input) const;
};

} // namespace flex_next_decoder
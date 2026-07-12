#pragma once

#include "IMessageParser.h"
#include "../FlexTypes.h"
#include <string>
#include <vector>

namespace flex_next_decoder {

/**
 * @class NumericParser
 * @brief Parser for Numeric FLEX messages
 *
 * Handles FLEX_PAGETYPE_STANDARD_NUMERIC, FLEX_PAGETYPE_SPECIAL_NUMERIC,
 * and FLEX_PAGETYPE_NUMBERED_NUMERIC message types.
 *
 * These messages use 4-bit BCD (Binary Coded Decimal) encoding where
 * digits are extracted from the bit stream using a sliding window approach.
 *
 * BCD Character Set: "0123456789 U -]["
 * - 0x0C is used as a fill character (not displayed)
 * - Numbered numeric pages have 10 additional header bits
 */
class NumericParser : public IMessageParser {
public:
    /**
     * @brief Constructor
     */
    NumericParser() = default;

    /**
     * @brief Destructor
     */
    ~NumericParser() override = default;

    // Delete copy operations (not needed for stateless parser)
    NumericParser(const NumericParser&) = delete;
    NumericParser& operator=(const NumericParser&) = delete;

    // Allow move operations
    NumericParser(NumericParser&&) = default;
    NumericParser& operator=(NumericParser&&) = default;

    /**
     * @brief Parse numeric message from phase data
     * @param input Message parsing parameters
     * @return Parsed message result with BCD-decoded content
     */
    MessageParseResult parseMessage(const MessageParseInput& input) const override;

    /**
     * @brief Check if this parser supports the message type
     * @param type Message type to check
     * @return true for all numeric message types
     */
    bool canParse(MessageType type) const override;

    /**
     * @brief Get parser name
     * @return "NumericParser"
     */
    std::string getParserName() const override;

    /**
     * @brief Get supported message types
     * @return Vector containing all numeric message types
     */
    std::vector<MessageType> getSupportedTypes() const override;

private:
    /**
     * @brief BCD fill character that should not be displayed
     */
    static constexpr unsigned char BCD_FILL_CHAR = 0x0C;
};

} // namespace flex_next_decoder
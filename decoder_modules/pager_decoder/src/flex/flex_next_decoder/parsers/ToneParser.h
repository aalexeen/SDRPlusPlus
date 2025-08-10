#pragma once

#include "IMessageParser.h"
#include "../FlexTypes.h"
#include <string>
#include <vector>

namespace flex_next_decoder {

/**
 * @class ToneParser
 * @brief Parser for Tone-only and Short Numeric FLEX messages
 *
 * Handles FLEX_PAGETYPE_TONE message types, which can be either:
 * 1. Pure tone-only messages (no data content)
 * 2. Short numeric messages embedded in the vector word
 *
 * Short numeric messages have BCD digits encoded directly in the
 * vector word bits rather than in separate data words.
 *
 * Vector Word Format for Short Numeric:
 * - Message type in bits 7-8 (0 = short numeric, 1 = tone-only)
 * - BCD digits in specific bit positions (9, 13, 17 for normal address)
 * - Additional digits in next vector word for long addresses
 */
class ToneParser : public IMessageParser {
public:
    /**
     * @brief Constructor
     */
    ToneParser() = default;

    /**
     * @brief Destructor
     */
    ~ToneParser() override = default;

    // Delete copy operations (not needed for stateless parser)
    ToneParser(const ToneParser&) = delete;
    ToneParser& operator=(const ToneParser&) = delete;

    // Allow move operations
    ToneParser(ToneParser&&) = default;
    ToneParser& operator=(ToneParser&&) = default;

    /**
     * @brief Parse tone message from vector word
     * @param input Message parsing parameters
     * @return Parsed message result (empty for pure tone, BCD digits for short numeric)
     */
    MessageParseResult parseMessage(const MessageParseInput& input) const override;

    /**
     * @brief Check if this parser supports the message type
     * @param type Message type to check
     * @return true for Tone message type
     */
    bool canParse(MessageType type) const override;

    /**
     * @brief Get parser name
     * @return "ToneParser"
     */
    std::string getParserName() const override;

    /**
     * @brief Get supported message types
     * @return Vector containing Tone type
     */
    std::vector<MessageType> getSupportedTypes() const override;

private:
    /**
     * @brief Extract short numeric digits from vector word
     * @param vector_word Vector word containing embedded digits
     * @param long_address True if this is a long address message
     * @param input Full input context for accessing additional vector words
     * @return String containing BCD-decoded digits
     */
    std::string extractShortNumeric(uint32_t vector_word, bool long_address,
                                   const MessageParseInput& input) const;
};

} // namespace flex_next_decoder
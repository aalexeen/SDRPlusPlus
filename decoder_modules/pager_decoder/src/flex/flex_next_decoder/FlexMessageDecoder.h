#pragma once

#include "FlexTypes.h"
#include "parsers/IMessageParser.h"
#include "parsers/AlphanumericParser.h"
#include "parsers/NumericParser.h"
#include "parsers/ToneParser.h"
#include "parsers/BinaryParser.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

namespace flex_next_decoder {

/**
 * @class FlexMessageDecoder
 * @brief Strategy Pattern coordinator for FLEX message parsing
 *
 * Manages multiple message parsers and selects the appropriate one
 * based on message type. Provides a unified interface for parsing
 * all FLEX message types with automatic fallback handling.
 *
 * Uses RAII for automatic parser lifecycle management and provides
 * efficient parser selection through a lookup table.
 */
class FlexMessageDecoder {
public:
    /**
     * @brief Constructor - initializes all message parsers
     */
    FlexMessageDecoder();

    /**
     * @brief Destructor - automatic cleanup via RAII
     */
    ~FlexMessageDecoder() = default;

    // Delete copy operations (unique_ptr members)
    FlexMessageDecoder(const FlexMessageDecoder&) = delete;
    FlexMessageDecoder& operator=(const FlexMessageDecoder&) = delete;

    // Allow move operations
    FlexMessageDecoder(FlexMessageDecoder&&) = default;
    FlexMessageDecoder& operator=(FlexMessageDecoder&&) = default;

    /**
     * @brief Parse message using appropriate strategy
     * @param input Message parsing parameters
     * @return Parsed message result
     */
    MessageParseResult parseMessage(const MessageParseInput& input) const;

    /**
     * @brief Get parser for specific message type
     * @param type Message type to get parser for
     * @return Pointer to appropriate parser, or nullptr if not found
     */
    const IMessageParser* getParserForType(MessageType type) const;

    /**
     * @brief Check if a message type is supported
     * @param type Message type to check
     * @return true if a parser exists for this type
     */
    bool isTypeSupported(MessageType type) const;

    /**
     * @brief Get list of all supported message types
     * @return Vector of all supported MessageType values
     */
    std::vector<MessageType> getSupportedTypes() const;

    /**
     * @brief Get information about all available parsers
     * @return Vector of parser names and their supported types
     */
    std::vector<std::pair<std::string, std::vector<MessageType>>> getParserInfo() const;

    /**
     * @brief Get the default fallback parser
     * @return Reference to binary parser used for unknown types
     */
    const BinaryParser& getDefaultParser() const;

private:
    /**
     * @brief Initialize all message parsers and build lookup table
     */
    void initializeParsers();

    /**
     * @brief Build the parser lookup table
     */
    void buildParserMap();

    //=========================================================================
    // Parser Instances (RAII Management)
    //=========================================================================

    std::unique_ptr<AlphanumericParser> alphanumeric_parser_;
    std::unique_ptr<NumericParser> numeric_parser_;
    std::unique_ptr<ToneParser> tone_parser_;
    std::unique_ptr<BinaryParser> binary_parser_;

    //=========================================================================
    // Parser Selection Table
    //=========================================================================

    std::unordered_map<MessageType, IMessageParser*> parser_map_;
};

} // namespace flex_next_decoder
#include "BinaryParser.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace flex_next_decoder {

MessageParseResult BinaryParser::parseMessage(const MessageParseInput& input) const {
    MessageParseResult result;
    
    // Validate input parameters
    std::string validation_error = validateInput(input);
    if (!validation_error.empty()) {
        result.success = false;
        result.error_message = validation_error;
        return result;
    }

    // Check message bounds
    if (input.message_word_start + input.message_length > input.phase_data_size) {
        result.success = false;
        result.error_message = "Message extends beyond phase data";
        return result;
    }

    try {
        // Calculate fragment flag (binary messages typically don't fragment)
        result.fragment_flag = calculateFragmentFlag(input.fragment_number, input.continuation_flag);

        // Build hex representation
        std::ostringstream hex_stream;
        hex_stream << std::hex << std::uppercase << std::setfill('0');

        for (uint32_t i = 0; i < input.message_length; i++) {
            uint32_t data_word = input.phase_data[input.message_word_start + i];
            
            // Output as 8-digit hex value
            hex_stream << std::setw(8) << data_word;
            
            // Add space separator between words (except for last word)
            if (i < (input.message_length - 1)) {
                hex_stream << " ";
            }
        }

        result.content = hex_stream.str();
        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Exception during binary parsing: " + std::string(e.what());
    }

    return result;
}

bool BinaryParser::canParse(MessageType type) const {
    return type == MessageType::Binary;
}

bool BinaryParser::canParseAsDefault() const {
    return true;
}

std::string BinaryParser::getParserName() const {
    return "BinaryParser";
}

std::vector<MessageType> BinaryParser::getSupportedTypes() const {
    return {MessageType::Binary};
}

MessageParseResult BinaryParser::parseAsDefault(const MessageParseInput& input) const {
    // Use the same logic as normal parsing, but don't validate message type
    MessageParseResult result;
    
    if (input.phase_data == nullptr || input.phase_data_size == 0) {
        result.success = false;
        result.error_message = "Invalid phase data for binary fallback parsing";
        return result;
    }

    if (input.message_word_start + input.message_length > input.phase_data_size) {
        result.success = false;
        result.error_message = "Message extends beyond phase data";
        return result;
    }

    try {
        result.fragment_flag = calculateFragmentFlag(input.fragment_number, input.continuation_flag);

        std::ostringstream hex_stream;
        hex_stream << std::hex << std::uppercase << std::setfill('0');

        for (uint32_t i = 0; i < input.message_length; i++) {
            uint32_t data_word = input.phase_data[input.message_word_start + i];
            hex_stream << std::setw(8) << data_word;
            
            if (i < (input.message_length - 1)) {
                hex_stream << " ";
            }
        }

        result.content = hex_stream.str();
        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Exception during binary fallback parsing: " + std::string(e.what());
    }

    return result;
}

} // namespace flex_next_decoder
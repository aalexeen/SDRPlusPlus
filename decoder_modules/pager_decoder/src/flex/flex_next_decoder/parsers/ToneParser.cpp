#include "ToneParser.h"
#include <stdexcept>

namespace flex_next_decoder {

MessageParseResult ToneParser::parseMessage(const MessageParseInput& input) const {
    MessageParseResult result;
    
    // Validate input parameters
    std::string validation_error = validateInput(input);
    if (!validation_error.empty()) {
        result.success = false;
        result.error_message = validation_error;
        return result;
    }

    if (input.vector_word_index >= input.phase_data_size) {
        result.success = false;
        result.error_message = "Vector word index out of bounds";
        return result;
    }

    try {
        // Calculate fragment flag (tone messages typically don't fragment)
        result.fragment_flag = calculateFragmentFlag(input.fragment_number, input.continuation_flag);

        uint32_t vector_word = input.phase_data[input.vector_word_index];
        
        // Check message type: bits 7-8, masked with 0x03
        // 0 = short numeric, 1 = pure tone-only
        uint32_t message_type_bits = (vector_word >> 7) & 0x03;
        
        std::string content;
        
        if (message_type_bits == 0) {
            // Short numeric message - extract BCD digits from vector word
            content = extractShortNumeric(vector_word, input.long_address, input);
        } else {
            // Pure tone-only message - no data content
            content = ""; // Empty content for tone-only
        }

        result.content = std::move(content);
        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Exception during tone parsing: " + std::string(e.what());
    }

    return result;
}

bool ToneParser::canParse(MessageType type) const {
    return type == MessageType::Tone;
}

std::string ToneParser::getParserName() const {
    return "ToneParser";
}

std::vector<MessageType> ToneParser::getSupportedTypes() const {
    return {MessageType::Tone};
}

std::string ToneParser::extractShortNumeric(uint32_t vector_word, bool long_address, 
                                           const MessageParseInput& input) const {
    std::string content;
    content.reserve(16); // Reserve space for typical short numeric length
    
    // Extract digits from primary vector word
    // Original code: for (i=9; i<=17; i+=4)
    for (int bit_pos = 9; bit_pos <= 17; bit_pos += 4) {
        unsigned char digit = (vector_word >> bit_pos) & 0x0F;
        if (digit < FLEX_BCD.size()) {
            content += FLEX_BCD[digit];
        }
    }
    
    // For long addresses, extract additional digits from next vector word
    if (long_address) {
        uint32_t next_vector_index = input.vector_word_index + 1;
        if (next_vector_index < input.phase_data_size) {
            uint32_t next_vector_word = input.phase_data[next_vector_index];
            
            // Extract digits from next vector word
            // Original code: for (i=0; i<=16; i+=4)
            for (int bit_pos = 0; bit_pos <= 16; bit_pos += 4) {
                unsigned char digit = (next_vector_word >> bit_pos) & 0x0F;
                if (digit < FLEX_BCD.size()) {
                    content += FLEX_BCD[digit];
                }
            }
        }
    }
    
    return content;
}

} // namespace flex_next_decoder
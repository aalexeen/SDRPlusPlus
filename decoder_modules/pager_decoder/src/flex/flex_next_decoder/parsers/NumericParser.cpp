#include "NumericParser.h"
#include <stdexcept>

namespace flex_next_decoder {

MessageParseResult NumericParser::parseMessage(const MessageParseInput& input) const {
    MessageParseResult result;
    
    // Validate input parameters
    std::string validation_error = validateInput(input);
    if (!validation_error.empty()) {
        result.success = false;
        result.error_message = validation_error;
        return result;
    }

    try {
        // Calculate fragment flag (numeric messages typically don't fragment)
        result.fragment_flag = calculateFragmentFlag(input.fragment_number, input.continuation_flag);

        // Extract numeric content
        std::string content;
        content.reserve(64); // Reserve reasonable space for numeric content

        // Calculate message boundaries from vector word
        // Original: int w1 = phaseptr[j] >> 7; int w2 = w1 >> 7;
        uint32_t vector_word = input.phase_data[input.vector_word_index];
        uint32_t w1 = (vector_word >> 7) & 0x7F;
        uint32_t w2 = (w1 >> 7) & 0x07;
        w1 = w1 & 0x7F;
        w2 = w2 + w1;  // numeric message is 7 words max

        // Check bounds
        if (w2 >= input.phase_data_size) {
            result.success = false;
            result.error_message = "Numeric message extends beyond phase data";
            return result;
        }

        // Get first data word
        uint32_t data_word;
        uint32_t start_word;
        if (!input.long_address) {
            data_word = input.phase_data[w1];
            start_word = w1 + 1;
            w2++;
        } else {
            data_word = input.phase_data[input.vector_word_index + 1];
            start_word = w1;
        }

        // Initialize bit extraction
        unsigned char digit = 0;
        uint32_t bit_count = 4; // Initial bit counter

        // Skip header bits based on message type
        if (input.type == MessageType::NumberedNumeric) {
            bit_count += 10; // Skip 10 header bits for numbered numeric
        } else {
            bit_count += 2;  // Skip 2 header bits for standard/special numeric
        }

        // Process all words in the numeric message
        for (uint32_t word_index = start_word; word_index <= w2; word_index++) {
            // Process 21 bits per word (FLEX word size)
            for (int bit_index = 0; bit_index < 21; bit_index++) {
                // Shift LSB from data word into digit
                digit = (digit >> 1) & 0x0F;
                if (data_word & 0x01) {
                    digit ^= 0x08;
                }
                data_word >>= 1;

                // Check if we have accumulated 4 bits (complete BCD digit)
                if (--bit_count == 0) {
                    // Convert BCD digit to character (skip fill characters)
                    if (digit != BCD_FILL_CHAR && digit < FLEX_BCD.size()) {
                        content += FLEX_BCD[digit];
                    }
                    bit_count = 4; // Reset for next digit
                }
            }
            
            // Load next data word for processing
            if (word_index < input.phase_data_size) {
                data_word = input.phase_data[word_index];
            }
        }

        result.content = std::move(content);
        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Exception during numeric parsing: " + std::string(e.what());
    }

    return result;
}

bool NumericParser::canParse(MessageType type) const {
    return type == MessageType::StandardNumeric ||
           type == MessageType::SpecialNumeric ||
           type == MessageType::NumberedNumeric;
}

std::string NumericParser::getParserName() const {
    return "NumericParser";
}

std::vector<MessageType> NumericParser::getSupportedTypes() const {
    return {
        MessageType::StandardNumeric,
        MessageType::SpecialNumeric,
        MessageType::NumberedNumeric
    };
}

} // namespace flex_next_decoder
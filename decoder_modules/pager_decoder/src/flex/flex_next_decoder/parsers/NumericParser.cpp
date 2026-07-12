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

        // Calculate message boundaries from vector word.
        // Matches C parse_numeric (demod_flex_next.c:2239-2242): w2 must be
        // derived from the UNMASKED w1 so bits 14-16 (word count) survive.
        // Masking w1 to 0x7F before the >>7 zeroed the count → truncated msgs.
        //   int w1 = phaseptr[j] >> 7;
        //   int w2 = w1 >> 7;
        //   w1 = w1 & 0x7f;
        //   w2 = (w2 & 0x07) + w1;
        uint32_t vector_word = input.phase_data[input.vector_word_index];
        uint32_t w1 = vector_word >> 7;
        uint32_t w2 = w1 >> 7;
        w1 = w1 & 0x7F;
        w2 = (w2 & 0x07) + w1;  // w2 = start + word_count - 1

        // Check bounds
        if (w2 >= input.phase_data_size) {
            result.success = false;
            result.error_message = "Numeric message extends beyond phase data";
            return result;
        }

        // Get first data word. dw_bad tracks the BCH status of the word
        // currently loaded in data_word (reference demod_flex_next.c:2256-2265):
        // when set, every BCD digit extracted from that word is emitted as '?'.
        uint32_t data_word;
        uint32_t start_word;
        bool dw_bad;
        if (!input.long_address) {
            dw_bad = input.word_error && input.word_error[w1];
            data_word = input.phase_data[w1];
            start_word = w1 + 1;
            w2++;
        } else {
            uint32_t first = input.vector_word_index + 1;
            dw_bad = input.word_error && input.word_error[first];
            data_word = input.phase_data[first];
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
                    if (dw_bad) {
                        // Uncorrectable word (reference demod_flex_next.c:2351-2352):
                        // emit '?' for every BCD position of this word.
                        content += '?';
                    } else if (digit < FLEX_BCD.size()) {
                        // Output ALL BCD digits, including 0x0C space-fill
                        // (PARSE-11 / reference demod_flex_next.c:2354-2358):
                        // the K checksum covers every BCD position, so dropping
                        // fill characters would desync it. FLEX_BCD[0x0C] = ' '.
                        content += FLEX_BCD[digit];
                    }
                    bit_count = 4; // Reset for next digit
                }
            }

            // Load next data word for processing, updating dw_bad to match
            // (reference demod_flex_next.c:2368-2370).
            if (word_index < input.phase_data_size) {
                dw_bad = input.word_error && input.word_error[word_index];
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
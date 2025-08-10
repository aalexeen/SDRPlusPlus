#include "AlphanumericParser.h"
#include <stdexcept>

namespace flex_next_decoder {

MessageParseResult AlphanumericParser::parseMessage(const MessageParseInput& input) const {
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
        // Calculate fragment flag
        result.fragment_flag = calculateFragmentFlag(input.fragment_number, input.continuation_flag);

        // Extract alphanumeric content
        std::string content;
        content.reserve(input.message_length * 3 + 10); // Reserve space for efficiency

        for (uint32_t i = 0; i < input.message_length; i++) {
            uint32_t data_word = input.phase_data[input.message_word_start + i];
            
            // Extract three 7-bit characters from the 21-bit data word
            unsigned char char1 = data_word & 0x7F;         // Bits 6-0
            unsigned char char2 = (data_word >> 7) & 0x7F;  // Bits 13-7
            unsigned char char3 = (data_word >> 14) & 0x7F; // Bits 20-14

            // Skip first character for certain fragment conditions
            // Original logic: if (i > 0 || frag != 0x03)
            if (i > 0 || input.fragment_number != 0x03) {
                addCharacterSafe(char1, content, MAX_MESSAGE_LENGTH);
            }
            
            addCharacterSafe(char2, content, MAX_MESSAGE_LENGTH);
            addCharacterSafe(char3, content, MAX_MESSAGE_LENGTH);

            // Check for buffer overflow protection
            if (content.size() >= MAX_MESSAGE_LENGTH - 10) {
                result.error_message = "Message length exceeds maximum allowed size";
                break;
            }
        }

        // Handle group message data
        if (input.is_group_message) {
            result.group_data = processGroupMessage(input);
        }

        result.content = std::move(content);
        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Exception during parsing: " + std::string(e.what());
    }

    return result;
}

bool AlphanumericParser::canParse(MessageType type) const {
    return type == MessageType::Alphanumeric || type == MessageType::Secure;
}

std::string AlphanumericParser::getParserName() const {
    return "AlphanumericParser";
}

std::vector<MessageType> AlphanumericParser::getSupportedTypes() const {
    return {MessageType::Alphanumeric, MessageType::Secure};
}

GroupMessageData AlphanumericParser::processGroupMessage(const MessageParseInput& input) const {
    GroupMessageData group_data;
    
    if (!input.is_group_message || input.group_bit < 0) {
        return group_data; // Empty group data
    }

    group_data.group_bit = input.group_bit;
    
    // NOTE: AlphanumericParser only identifies group messages and sets the group_bit.
    // The actual capcode list population should be done by the calling code using
    // FlexGroupHandler::processGroupMessage(). This maintains separation of concerns
    // where the parser handles message content and the group handler manages group state.
    //
    // Integration example (in calling code like FlexMessageDecoder or main decoder):
    //
    // if (!result.group_data.isEmpty()) {
    //     GroupMessageInfo group_info = group_handler.processGroupMessage(result.group_data.group_bit);
    //     if (group_info.isValid()) {
    //         result.group_data.capcodes = std::move(group_info.capcodes);
    //     }
    // }
    
    return group_data;
}

} // namespace flex_next_decoder
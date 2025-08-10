#include "IMessageParser.h"

namespace flex_next_decoder {

//=============================================================================
// GroupMessageData Implementation
//=============================================================================

bool GroupMessageData::isEmpty() const {
    return group_bit == -1 || capcodes.empty();
}

//=============================================================================
// MessageParseResult Implementation
//=============================================================================

bool MessageParseResult::hasError() const {
    return !success || !error_message.empty();
}

//=============================================================================
// IMessageParser Protected Methods Implementation
//=============================================================================

FragmentFlag IMessageParser::calculateFragmentFlag(uint32_t fragment_number, bool continuation_flag) const {
    if (!continuation_flag && fragment_number == 3) {
        return FragmentFlag::Complete;    // 'K' - OK to display
    } else if (!continuation_flag && fragment_number != 3) {
        return FragmentFlag::Continuation; // 'C' - Completes fragments
    } else if (continuation_flag) {
        return FragmentFlag::Fragment;     // 'F' - Needs continuation
    }
    return FragmentFlag::Unknown;
}

std::string IMessageParser::validateInput(const MessageParseInput& input) const {
    if (input.phase_data == nullptr) {
        return "Phase data pointer is null";
    }
    
    if (input.phase_data_size == 0) {
        return "Phase data size is zero";
    }
    
    if (!canParse(input.type)) {
        return "Message type not supported by this parser";
    }
    
    return ""; // Valid
}

uint32_t IMessageParser::addCharacterSafe(unsigned char ch, std::string& buffer, uint32_t max_size) {
    if (buffer.size() >= max_size) {
        return 0; // Buffer full
    }
    
    // Encode special characters for safety and readability
    if (ch == 0x09 && buffer.size() < (max_size - 1)) {  // '\t'
        buffer += "\\t";
        return 2;
    }
    if (ch == 0x0a && buffer.size() < (max_size - 1)) {  // '\n'
        buffer += "\\n";
        return 2;
    }
    if (ch == 0x0d && buffer.size() < (max_size - 1)) {  // '\r'
        buffer += "\\r";
        return 2;
    }
    if (ch == '%' && buffer.size() < (max_size - 1)) {   // Prevent format string vulns
        buffer += "%%";
        return 2;
    }
    
    // Only store ASCII printable characters
    if (ch >= 32 && ch <= 126) {
        buffer += static_cast<char>(ch);
        return 1;
    }
    
    return 0; // Character not added
}

} // namespace flex_next_decoder
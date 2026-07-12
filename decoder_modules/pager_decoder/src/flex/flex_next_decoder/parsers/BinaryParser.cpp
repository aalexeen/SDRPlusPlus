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
        // Calculate fragment flag (HEX/Binary uses the same F/C/K flags as ALN)
        result.fragment_flag = calculateFragmentFlag(input.fragment_number, input.continuation_flag);

        result.content = extractHexContent(input);
        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Exception during binary parsing: " + std::string(e.what());
    }

    return result;
}

std::string BinaryParser::extractHexContent(const MessageParseInput& input) {
    // Port of demod_flex_next.c parse_binary (HEX/Binary body decode).
    //
    // Body layout (Section 3.10.1.2). Data is a continuous LSB-first bit stream
    // packed across 21-bit words and rendered as 4-bit nibbles. Because 21 is
    // not a multiple of 4, nibbles STRADDLE word boundaries — the nibble
    // accumulator and bit counter run continuously across the whole data-word
    // loop and are NEVER reset per word.
    //
    // On the initial fragment (F=11) the first message word is hdr2 (pure
    // control fields R/M/D/H/B/I/s/S, no data nibbles) and is skipped. The
    // caller's message_word_start/message_length follow the same convention as
    // the ALN parser (verified byte-exact), so we start directly from them.
    const uint32_t* phase = input.phase_data;
    const uint8_t* werr = input.word_error;
    const bool is_initial = (input.fragment_number == 0x03); // F=11
    const bool is_last = !input.continuation_flag;           // C=0 (K or final C)

    uint32_t data_start = input.message_word_start;
    uint32_t len = input.message_length;

    // Skip hdr2 on the initial fragment (control-only, contributes no nibbles).
    if (is_initial && len > 0) {
        data_start += 1;
        len -= 1;
    }

    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    std::string hex;
    int bit_count = 0;          // continuous across all data words
    unsigned char nibble_acc = 0;

    for (uint32_t w = data_start; w < data_start + len && w < input.phase_data_size; w++) {
        if (werr && werr[w]) {
            // Lost word: advance bit_count by 21 (keeping nibble alignment for
            // later words) and emit '?' at each completed nibble boundary. Ref
            // demod_flex_next.c:2652-2662.
            for (int b = 0; b < 21; b++) {
                int nibble_pos = bit_count % 4;
                if (nibble_pos == 0) { nibble_acc = 0; }
                if (nibble_pos == 3) { hex.push_back('?'); }
                bit_count++;
            }
            continue;
        }
        uint32_t dw = phase[w];
        for (int b = 0; b < 21; b++) {
            int nibble_pos = bit_count % 4;
            if (nibble_pos == 0) { nibble_acc = 0; }
            nibble_acc |= ((dw >> b) & 1u) << nibble_pos;
            if (nibble_pos == 3) { hex.push_back(HEX_DIGITS[nibble_acc & 0xF]); }
            bit_count++;
        }
    }

    // Strip termination fill from the last/complete fragment (C=0).
    // Ref demod_flex_next.c:2688-2726. Two rules, at most one applies:
    //   (3) whole last word all-0/all-1 → drop it, truncate to the nibble count
    //       before that word;
    //   (2) else partial fill in the last word → scan back from bit 20, the top
    //       run of identical bits is fill; keep nibbles up to the first change.
    if (is_last && len > 0) {
        uint32_t last_w = data_start + len - 1;
        int hi = static_cast<int>(hex.size());
        bool handled = false;

        if (last_w > data_start && last_w < input.phase_data_size && !(werr && werr[last_w])) {
            uint32_t lw = phase[last_w] & 0x1FFFFF;
            if (lw == 0x000000u || lw == 0x1FFFFFu) {
                int bits_before = static_cast<int>(last_w - data_start) * 21;
                int nibs_before = bits_before / 4;
                if (nibs_before > 0 && nibs_before <= hi) {
                    hi = nibs_before;
                    handled = true;
                }
            }
        }

        if (!handled && last_w < input.phase_data_size && !(werr && werr[last_w])) {
            uint32_t lw = phase[last_w];
            uint32_t top_bit = (lw >> 20) & 1u;
            int fill_start = 21;
            for (int b = 20; b >= 0; b--) {
                if (((lw >> b) & 1u) != top_bit) { break; }
                fill_start = b;
            }
            if (fill_start < 21 && fill_start > 0) {
                int bits_before = static_cast<int>(last_w - data_start) * 21;
                int real_bits = bits_before + fill_start;
                int real_nibbles = (real_bits + 3) / 4;
                if (real_nibbles < hi) { hi = real_nibbles; }
            }
        }

        if (hi >= 0 && hi < static_cast<int>(hex.size())) { hex.resize(hi); }
    }

    return hex;
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
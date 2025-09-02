
#include "FlexOutputFormatter.h"
#include "FlexTypes.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace flex_next_decoder {
    FlexOutputFormatter::FlexOutputFormatter() : FlexNextDecoder(2) {}

    FlexOutputFormatter::FlexOutputFormatter(int verbosity_level) : FlexNextDecoder(verbosity_level) {}

    void FlexOutputFormatter::setMessageCallback(std::function<void(int64_t, int, const std::string&)> callback) {
        message_callback_ = std::move(callback);
    }

    void FlexOutputFormatter::outputMessage(const ParsedMessage &message, const MessageInfo &msg_info,
                                            const SyncInfo &sync_info, const FrameInfo &frame_info, char phase_id,
                                            const std::vector<int64_t> &group_capcodes) {

        // Get fragment flag character
        char fragment_flag = '?';
        switch (message.fragment_flag) {
            case FragmentFlag::Complete:
                fragment_flag = 'K'; // OK to display
                break;
            case FragmentFlag::Fragment:
                fragment_flag = 'F'; // Fragment needing continuation
                break;
            case FragmentFlag::Continuation:
                fragment_flag = 'C'; // Continuation of fragments
                break;
            case FragmentFlag::Unknown:
            default:
                fragment_flag = '?';
                break;
        }

        // Format main header: FLEX_NEXT|baud/levels|cycle.frame.phase|capcode|flags|type|
        std::string header = formatHeader(msg_info, sync_info, frame_info, phase_id, fragment_flag);
        
        // Build complete formatted message
        std::ostringstream formatted_message;
        formatted_message << header;

        // Output message type and fragment info
        std::string msg_type = getMessageTypeString(msg_info.type);
        formatted_message << msg_type << "|";

        // Handle fragment/continuation info for alphanumeric messages
        if (msg_info.type == MessageType::Alphanumeric || msg_info.type == MessageType::Secure) {
            // Output fragment info: frag.cont.flag|
            uint32_t frag_num = (msg_info.fragment_number & 0x3);
            uint32_t cont_flag = (msg_info.continuation_flag ? 1 : 0);
            formatted_message << frag_num << "." << cont_flag << "." << fragment_flag << "|";
        }

        // Handle group message output
        if (!group_capcodes.empty()) {
            // Output all group capcodes before the message content
            for (size_t i = 0; i < group_capcodes.size(); i++) {
                formatted_message << std::setfill('0') << std::setw(10) << group_capcodes[i] << "|";
            }
        }

        // Output message content
        if (!message.content.empty()) { 
            formatted_message << message.content; 
        }

        // Get the complete formatted message
        std::string complete_message = formatted_message.str();

        // Console output (keep existing behavior)
        std::cout << complete_message << std::endl;

        // NEW: Send to GUI via callback if available
        if (message_callback_ && !message.content.empty()) {
            message_callback_(msg_info.capcode, static_cast<int>(msg_info.type), complete_message);
        }

        // Optional debug output at higher verbosity levels
        if (getVerbosityLevel() >= 3 && !message.content.empty()) {
            std::cout << "DEBUG: Message parsed successfully" << std::endl;
        }
    }

// ... existing code ...
    std::string FlexOutputFormatter::formatHeader(const MessageInfo &msg_info, const SyncInfo &sync_info,
                                                  const FrameInfo &frame_info, char phase_id,
                                                  char fragment_flag) const {
        std::ostringstream header;

        // Format: FLEX_NEXT|baud/levels|cycle.frame.phase|capcode|flags|type|
        header << "FLEX_NEXT|";

        // Baud/levels: e.g., "1600/2" or "3200/4"
        header << sync_info.baud_rate << "/" << sync_info.levels << "|";

        // Cycle.Frame.Phase: e.g., "02.123.A"
        header << std::setfill('0') << std::setw(2) << frame_info.cycle_number << "." << std::setw(3)
               << frame_info.frame_number << "." << phase_id << "|";

        // Capcode: 10-digit zero-padded
        header << std::setfill('0') << std::setw(10) << msg_info.capcode << "|";

        // Flags: [L/S][G/S] - Long/Short address, Group/Single message
        char addr_flag = msg_info.long_address ? 'L' : 'S';
        char group_flag = msg_info.is_group_message ? 'G' : 'S';
        header << addr_flag << group_flag << "|";

        // Message type number
        header << static_cast<int>(msg_info.type) << "|";

        return header.str();
    }

    std::string FlexOutputFormatter::getMessageTypeString(MessageType type) const {
        switch (type) {
            case MessageType::Secure:
                return "SEC"; // Secure (treated like alphanumeric)
            case MessageType::ShortInstruction:
                return "SIN"; // Short Instruction
            case MessageType::Tone:
                return "TON"; // Tone Only
            case MessageType::StandardNumeric:
                return "NUM"; // Standard Numeric
            case MessageType::SpecialNumeric:
                return "SNM"; // Special Numeric
            case MessageType::Alphanumeric:
                return "ALN"; // Alphanumeric
            case MessageType::Binary:
                return "BIN"; // Binary
            case MessageType::NumberedNumeric:
                return "NNU"; // Numbered Numeric
            default:
                return "UNK"; // Unknown
        }
    }

} // namespace flex_next_decoder

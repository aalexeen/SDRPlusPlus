#include "decoder.h"
#include <algorithm>
#include <cctype>

// Implementation details that don't need to be in the header
namespace {
    // Format a FLEX message for display
    std::string formatFlexMessage(const std::string& name, int64_t address, int type, const std::string& data) {
        std::stringstream ss;
        ss << "FLEX[" << name << "]: Addr=" << address << ", Type=" << type << ", Data=\"" << data << "\"";
        return ss.str();
    }


}

// Message type to string conversion
std::string messageTypeToString(int type) {
    switch (type) {
    case 0:
        return "Secure";
    case 1:
        return "ShortInstruction";
    case 2:
        return "Tone";
    case 3:
        return "StandardNumeric";
    case 4:
        return "SpecialNumeric";
    case 5:
        return "Alphanumeric";
    case 6:
        return "Binary";
    case 7:
        return "NumberedNumeric";
    default:
        return "Unknown";
    }
}

/*
// Message handling with the new decoder architecture
// This would be called by the flex_next_decoder through a callback mechanism
void FLEXDecoder::handleFlexMessage(int64_t address, int type, const std::string& data) {
    try {
        // Input validation
        if (data.length() > 1000) {
            flog::warn("FLEX message too long, truncating (decoder: '{}')", name_);
            return;
        }

        if (address < 0) {
            flog::warn("Invalid FLEX address: {} (decoder: '{}')", address, name_);
            return;
        }

        // Update statistics
        messagesDecoded_.fetch_add(1);

        // Format message for storage and display
        std::string formatted_message = formatFlexMessage(name_, address, type, data);

        // Store message for display
        {
            std::lock_guard<std::mutex> lock(messagesMutex_);
            messages_.emplace_back(std::move(formatted_message));

            // Limit message history to prevent memory bloat
            if (messages_.size() > MAX_MESSAGES) {
                messages_.erase(messages_.begin());
            }
        }

        // Console output for immediate feedback
        printf("FLEX[%s]: Addr=%ld Type=%s Data=%s\n",
               name_.c_str(), address, messageTypeToString(type).c_str(), data.c_str());

        // SDR++ logging
        flog::info("FLEX[{}] Message - Addr: {}, Type: {} ({}), Data: {}",
                   name_, address, type, messageTypeToString(type), data);
    }
    catch (const std::exception& e) {
        flog::error("Error handling FLEX message in '{}': {}", name_, e.what());
        errorCount_.fetch_add(1);
    }

}*/
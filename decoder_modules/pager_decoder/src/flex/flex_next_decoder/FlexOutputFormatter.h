#pragma once

#include "FlexNextDecoder.h"
#include "FlexTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace flex_next_decoder {

    struct ParsedMessage;

    class FlexOutputFormatter : public FlexNextDecoder {
    public:
        FlexOutputFormatter();
        FlexOutputFormatter(int verbosity_level);
        ~FlexOutputFormatter() override = default;

        // New: Set callback for GUI messages
        void setMessageCallback(std::function<void(int64_t, int, const std::string&)> callback);

        void outputMessage(const ParsedMessage& message,
                           const MessageInfo& msg_info,
                           const SyncInfo& sync_info,
                           const FrameInfo& frame_info,
                           char phase_id,
                           const std::vector<int64_t>& group_capcodes = {});

    private:
        std::string formatHeader(const MessageInfo& msg_info,
                                 const SyncInfo& sync_info,
                                 const FrameInfo& frame_info,
                                 char phase_id,
                                 char fragment_flag) const;

        std::string getMessageTypeString(MessageType type) const;
        
        // New: Message callback for GUI integration
        std::function<void(int64_t, int, const std::string&)> message_callback_;
    };

} // namespace flex_next_decoder
#pragma once

#include "../FlexTypes.h"
#include <string>

namespace flex_next_decoder {

    struct ParsedMessage {
        std::string content;
        char fragment_flag = '?';  // K, F, C
        bool is_group_message = false;
        int group_bit = -1;
    };

    class IMessageParser {
    public:
        virtual ~IMessageParser() = default;

        virtual ParsedMessage parseMessage(
            const uint32_t* phase_data,
            const MessageInfo& msg_info,
            const SyncInfo& sync_info,
            const FrameInfo& frame_info,
            uint32_t start_word,
            uint32_t length) = 0;
    };

} // namespace flex_next_decoder
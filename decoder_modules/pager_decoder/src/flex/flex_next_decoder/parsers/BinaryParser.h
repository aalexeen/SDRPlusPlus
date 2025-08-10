#pragma once

#include "IMessageParser.h"

namespace flex_next_decoder {

    class BinaryParser : public IMessageParser {
    public:
        BinaryParser() = default;
        ~BinaryParser() override = default;

        ParsedMessage parseMessage(
            const uint32_t* phase_data,
            const MessageInfo& msg_info,
            const SyncInfo& sync_info,
            const FrameInfo& frame_info,
            uint32_t start_word,
            uint32_t length) override;
    };

} // namespace flex_next_decoder
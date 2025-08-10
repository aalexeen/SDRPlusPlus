#pragma once

#include "IMessageParser.h"

namespace flex_next_decoder {

    class NumericParser : public IMessageParser {
    public:
        NumericParser() = default;
        ~NumericParser() override = default;

        ParsedMessage parseMessage(
            const uint32_t* phase_data,
            const MessageInfo& msg_info,
            const SyncInfo& sync_info,
            const FrameInfo& frame_info,
            uint32_t start_word,
            uint32_t length) override;

    private:
        static constexpr char FLEX_BCD[17] = "0123456789 U -][";
    };

} // namespace flex_next_decoder
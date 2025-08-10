#pragma once

#include "IMessageParser.h"

namespace flex_next_decoder {

    class AlphanumericParser : public IMessageParser {
    public:
        AlphanumericParser() = default;
        ~AlphanumericParser() override = default;

        ParsedMessage parseMessage(
            const uint32_t* phase_data,
            const MessageInfo& msg_info,
            const SyncInfo& sync_info,
            const FrameInfo& frame_info,
            uint32_t start_word,
            uint32_t length) override;

    private:
        size_t addCharacter(char ch, char* buffer, size_t index) const;
        char determineFragmentFlag(int fragment, int continuation) const;
    };

} // namespace flex_next_decoder
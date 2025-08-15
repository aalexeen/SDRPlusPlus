#pragma once

#include "FlexNextDecoder.h"
#include "FlexTypes.h"
#include <string>
#include <vector>

namespace flex_next_decoder {

    struct ParsedMessage;

    class FlexOutputFormatter : public FlexNextDecoder {
    public:
        FlexOutputFormatter() = default;
        FlexOutputFormatter(int verbosity_level);
        ~FlexOutputFormatter() = default;

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

        int verbosity_level_; ///< Debug output level
    };

} // namespace flex_next_decoder
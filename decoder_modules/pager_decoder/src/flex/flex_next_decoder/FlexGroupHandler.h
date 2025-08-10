#pragma once

#include "FlexTypes.h"
#include <array>
#include <vector>

namespace flex_next_decoder {

    class FlexGroupHandler {
    public:
        FlexGroupHandler() = default;
        ~FlexGroupHandler() = default;

        void addCapcodeToGroup(int group_bit, int64_t capcode,
                              int assigned_frame, const FrameInfo& current_frame);

        std::vector<int64_t> getGroupCapcodes(int group_bit);
        void clearGroup(int group_bit);

        void checkMissedGroupMessages(const FrameInfo& frame_info);

    private:
        struct GroupInfo {
            std::array<int64_t, 1000> capcodes{};
            int capcode_count = 0;
            int target_frame = -1;
            int target_cycle = -1;
        };

        std::array<GroupInfo, GROUP_BITS> groups_;

        bool shouldResetGroup(const GroupInfo& group, const FrameInfo& frame_info) const;
        void reportMissedGroup(int group_bit, const GroupInfo& group);
    };

} // namespace flex_next_decoder
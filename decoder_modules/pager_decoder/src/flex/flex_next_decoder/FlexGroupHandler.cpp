#include "FlexGroupHandler.h"
#include <algorithm>
#include <iostream>

namespace flex_next_decoder {

    //=============================================================================
    // FlexGroupHandler Implementation
    //=============================================================================

    FlexGroupHandler::FlexGroupHandler() { reset(); }
    FlexGroupHandler::FlexGroupHandler(int verbosity_level) : FlexNextDecoder(verbosity_level) { reset(); }


    bool FlexGroupHandler::registerCapcodeToGroup(int64_t capcode, uint32_t vector_word, uint32_t current_cycle,
                                                  uint32_t current_frame) { // checked so so
        // Extract group information from Vector Information Word
        // Original: unsigned int iAssignedFrame = (int)((viw >> 10) & 0x7f);
        // Original: int groupbit = (int)((viw >> 17) & 0x7f);
        uint32_t assigned_frame = (vector_word >> 10) & 0x7F;
        int group_bit = static_cast<int>((vector_word >> 17) & 0x7F);

        // Validate group bit
        if (group_bit < 0 || group_bit >= GROUP_BITS) { return false; }

        // Add capcode to group
        GroupCapcodeList &group = groups_[group_bit];

        // Check if we're exceeding maximum capcodes per group
        if (group.capcodes.size() >= MAX_CAPCODES_PER_GROUP) { return false; }

        group.addCapcode(capcode);
        group.target_frame = static_cast<int>(assigned_frame);

        // Calculate target cycle using original algorithm
        group.target_cycle = static_cast<int>(calculateTargetCycle(assigned_frame, current_cycle, current_frame));

        return true;
    }

    bool FlexGroupHandler::isGroupCapcode(int64_t capcode) {
        return capcode >= GROUP_CAPCODE_MIN && capcode <= GROUP_CAPCODE_MAX;
    }

    int FlexGroupHandler::getGroupBit(int64_t capcode) {
        if (!isGroupCapcode(capcode)) { return -1; }
        return static_cast<int>(capcode - GROUP_CAPCODE_MIN);
    }

    GroupMessageInfo FlexGroupHandler::processGroupMessage(int group_bit) {
        GroupMessageInfo info;

        // Validate group bit
        if (group_bit < 0 || group_bit >= GROUP_BITS) {
            return info; // Invalid
        }

        GroupCapcodeList &group = groups_[group_bit];

        // Check if group has pending capcodes
        if (!group.hasPendingCapcodes()) {
            return info; // No pending capcodes
        }

        // Copy capcodes to result
        info.group_bit = group_bit;
        info.capcodes = group.capcodes;
        info.is_valid = true;

        // Clear the group (original behavior - reset after processing)
        clearGroup(group_bit);

        return info;
    }

    std::vector<int> FlexGroupHandler::checkAndCleanupMissedGroups(uint32_t current_cycle,
                                                                   uint32_t current_frame) { // checked
        std::vector<int> missed_groups;

        // Check each group for missed messages
        // Original logic from decode_fiw() function
        for (int group_bit = 0; group_bit < GROUP_BITS; group_bit++) {
            GroupCapcodeList &group = groups_[group_bit];

            // Only check groups with pending capcodes
            if (!group.hasPendingCapcodes()) { continue; }

            bool should_expire = shouldExpireGroup(group_bit, current_cycle, current_frame);

            if (should_expire) {
                missed_groups.push_back(group_bit);
                clearGroup(group_bit);
            }
        }

        return missed_groups;
    }

    void FlexGroupHandler::reset() {
        for (auto &group: groups_) { group.clear(); }
    }

    const GroupCapcodeList &FlexGroupHandler::getGroupInfo(int group_bit) const {
        static const GroupCapcodeList empty_group;

        if (group_bit < 0 || group_bit >= GROUP_BITS) { return empty_group; }

        return groups_[group_bit];
    }

    uint32_t FlexGroupHandler::getActiveGroupCount() const {
        uint32_t count = 0;
        for (const auto &group: groups_) {
            if (group.hasPendingCapcodes()) { count++; }
        }
        return count;
    }

    bool FlexGroupHandler::hasGroupPending(int group_bit) const {
        if (group_bit < 0 || group_bit >= GROUP_BITS) { return false; }

        return groups_[group_bit].hasPendingCapcodes();
    }

    //=============================================================================
    // Private Methods Implementation
    //=============================================================================

    uint32_t FlexGroupHandler::calculateTargetCycle(uint32_t assigned_frame, uint32_t current_cycle,
                                                    uint32_t current_frame) const { // checked so so
        // Original algorithm from registerCapcodeToGroup
        // if(iAssignedFrame > flex->FIW.frameno)
        // {
        //     flex->GroupHandler.GroupCycle[groupbit] = (int)flex->FIW.cycleno;
        // }
        // else
        // {
        //     if(flex->FIW.cycleno == 15)
        //     {
        //         flex->GroupHandler.GroupCycle[groupbit] = 0;
        //     }
        //     else
        //     {
        //         flex->GroupHandler.GroupCycle[groupbit] = (int)flex->FIW.cycleno++;
        //     }
        // }

        if (assigned_frame > current_frame) {
            // Message frame is in this cycle
            return current_cycle;
        } else {
            // Message frame is in the next cycle
            if (current_cycle == 15) {
                return 0; // Wrap around to cycle 0
            } else {
                return current_cycle + 1;
            }
        }
    }

    bool FlexGroupHandler::shouldExpireGroup(int group_bit, uint32_t current_cycle,
                                             uint32_t current_frame) const { // checked
        const GroupCapcodeList &group = groups_[group_bit];

        if (!group.hasPendingCapcodes()) { return false; }

        // Original logic from decode_fiw() function
        bool should_reset = false;

        // Check if its expected in this frame
        if (static_cast<int>(current_cycle) == group.target_cycle) {
            if (group.target_frame < static_cast<int>(current_frame)) { should_reset = true; }
        }
        // Check if we should have sent a group message in the previous cycle
        else if (current_cycle == 0) {
            if (group.target_cycle == 15) { should_reset = true; }
        }
        // If we are waiting for the cycle to roll over then don't reset yet
        else if (current_cycle == 15 && group.target_cycle == 0) {
            should_reset = false;
        }
        // Otherwise if the target cycle is less than the current cycle, reset
        else if (group.target_cycle < static_cast<int>(current_cycle)) {
            should_reset = true;
        }

        return should_reset;
    }

    void FlexGroupHandler::clearGroup(int group_bit) { // checked
        if (group_bit >= 0 && group_bit < GROUP_BITS) { groups_[group_bit].clear(); }
    }

} // namespace flex_next_decoder

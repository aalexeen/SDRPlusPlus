#pragma once

#include "FlexTypes.h"
#include <array>
#include <vector>
#include <cstdint>

namespace flex_next_decoder {

/**
 * @struct GroupCapcodeList
 * @brief Storage for capcodes associated with a group bit
 */
struct GroupCapcodeList {
    std::vector<int64_t> capcodes;
    int target_frame = -1;
    int target_cycle = -1;

    /**
     * @brief Check if group has pending capcodes
     * @return true if capcodes are registered and waiting
     */
    bool hasPendingCapcodes() const {
        return !capcodes.empty() && target_frame >= 0;
    }

    /**
     * @brief Clear all capcodes and reset target frame/cycle
     */
    void clear() {
        capcodes.clear();
        target_frame = -1;
        target_cycle = -1;
    }

    /**
     * @brief Add capcode to the group
     * @param capcode Capcode to add
     */
    void addCapcode(int64_t capcode) {
        capcodes.push_back(capcode);
    }

    /**
     * @brief Get number of capcodes in group
     * @return Number of capcodes
     */
    size_t size() const {
        return capcodes.size();
    }
};

/**
 * @struct GroupMessageInfo
 * @brief Information about a group message processing request
 */
struct GroupMessageInfo {
    int group_bit = -1;
    std::vector<int64_t> capcodes;
    bool is_valid = false;

    /**
     * @brief Check if group message info is valid
     * @return true if group bit is valid and capcodes exist
     */
    bool isValid() const {
        return is_valid && group_bit >= 0 && group_bit < GROUP_BITS && !capcodes.empty();
    }
};

/**
 * @class FlexGroupHandler
 * @brief Manages FLEX group messaging functionality
 *
 * FLEX group messaging allows multiple pagers to receive the same message
 * using a two-phase process:
 * 1. Short Instruction messages register capcodes to group bits
 * 2. Alphanumeric messages sent to group capcode range trigger delivery
 *
 * Group capcode range: 2029568-2029583 (16 group bits + 1 for indexing)
 * Each group can contain up to 1000 capcodes
 */
class FlexGroupHandler {
public:
    /**
     * @brief Constructor
     */
    FlexGroupHandler();

    /**
     * @brief Destructor
     */
    ~FlexGroupHandler() = default;

    // Delete copy operations (manages unique state)
    FlexGroupHandler(const FlexGroupHandler&) = delete;
    FlexGroupHandler& operator=(const FlexGroupHandler&) = delete;

    // Allow move operations
    FlexGroupHandler(FlexGroupHandler&&) = default;
    FlexGroupHandler& operator=(FlexGroupHandler&&) = default;

    //=========================================================================
    // Group Message Registration
    //=========================================================================

    /**
     * @brief Process Short Instruction message to register capcode to group
     * @param capcode Capcode to register
     * @param vector_word Vector Information Word containing group and frame info
     * @param current_cycle Current cycle number (0-15)
     * @param current_frame Current frame number (0-127)
     * @return true if registration was successful
     */
    bool registerCapcodeToGroup(int64_t capcode, uint32_t vector_word,
                               uint32_t current_cycle, uint32_t current_frame);

    /**
     * @brief Check if capcode is in group message range
     * @param capcode Capcode to check
     * @return true if capcode is in group range (2029568-2029583)
     */
    static bool isGroupCapcode(int64_t capcode);

    /**
     * @brief Get group bit from group capcode
     * @param capcode Group capcode (must be in group range)
     * @return Group bit (0-15) or -1 if invalid
     */
    static int getGroupBit(int64_t capcode);

    //=========================================================================
    // Group Message Processing
    //=========================================================================

    /**
     * @brief Process group message and return associated capcodes
     * @param group_bit Group bit (0-15)
     * @return GroupMessageInfo with capcodes or invalid if no group found
     */
    GroupMessageInfo processGroupMessage(int group_bit);

    /**
     * @brief Check for missed group messages and clean up expired entries
     * @param current_cycle Current cycle number (0-15)
     * @param current_frame Current frame number (0-127)
     * @return Vector of group bits that had missed messages (for logging)
     */
    std::vector<int> checkAndCleanupMissedGroups(uint32_t current_cycle, uint32_t current_frame);

    //=========================================================================
    // State Management
    //=========================================================================

    /**
     * @brief Clear all group data
     */
    void reset();

    /**
     * @brief Get group information for debugging
     * @param group_bit Group bit to query (0-15)
     * @return Const reference to group capcode list
     */
    const GroupCapcodeList& getGroupInfo(int group_bit) const;

    /**
     * @brief Get number of active groups
     * @return Number of groups with pending capcodes
     */
    uint32_t getActiveGroupCount() const;

    /**
     * @brief Check if specific group has pending capcodes
     * @param group_bit Group bit to check
     * @return true if group has pending capcodes
     */
    bool hasGroupPending(int group_bit) const;

private:
    //=========================================================================
    // Internal Processing Methods
    //=========================================================================

    /**
     * @brief Calculate target cycle for group message delivery
     * @param assigned_frame Frame number from Short Instruction
     * @param current_cycle Current cycle number
     * @param current_frame Current frame number
     * @return Target cycle number (0-15)
     */
    uint32_t calculateTargetCycle(uint32_t assigned_frame, uint32_t current_cycle, uint32_t current_frame) const;

    /**
     * @brief Check if group message should be expired
     * @param group_bit Group bit to check
     * @param current_cycle Current cycle number
     * @param current_frame Current frame number
     * @return true if group should be expired
     */
    bool shouldExpireGroup(int group_bit, uint32_t current_cycle, uint32_t current_frame) const;

    /**
     * @brief Clear specific group data
     * @param group_bit Group bit to clear
     */
    void clearGroup(int group_bit);

    int verbosity_level_;       ///< Debug output level

    //=========================================================================
    // State Variables
    //=========================================================================

    std::array<GroupCapcodeList, GROUP_BITS> groups_;

    //=========================================================================
    // Constants
    //=========================================================================

    static constexpr int64_t GROUP_CAPCODE_MIN = 2029568;
    static constexpr int64_t GROUP_CAPCODE_MAX = 2029583;
    static constexpr size_t MAX_CAPCODES_PER_GROUP = 1000;
    static constexpr uint32_t MAX_CYCLES = 16;
    static constexpr uint32_t MAX_FRAMES = 128;
};

} // namespace flex_next_decoder
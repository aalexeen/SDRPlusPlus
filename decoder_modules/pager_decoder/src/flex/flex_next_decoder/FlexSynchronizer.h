#pragma once

#include "FlexTypes.h"

namespace flex_next_decoder {

    class FlexSynchronizer {
    public:
        FlexSynchronizer() = default;
        ~FlexSynchronizer() = default;

        uint32_t processSymbol(uint8_t symbol);
        void decodeMode(uint32_t sync_code, SyncInfo& sync_info);

        void reset();

    private:
        uint32_t checkSyncPattern(uint64_t buffer) const;
        uint32_t countBits(uint32_t data) const;

        uint64_t sync_buffer_ = 0;

        struct FlexMode {
            uint32_t sync_code;
            uint32_t baud_rate;
            uint32_t levels;
        };

        static constexpr std::array<FlexMode, 5> FLEX_MODES = {{
            {0x870C, 1600, 2},
            {0xB068, 1600, 4},
            {0x7B18, 3200, 2},
            {0xDEA0, 3200, 4},
            {0x4C7C, 3200, 4}
        }};
    };

} // namespace flex_next_decoder
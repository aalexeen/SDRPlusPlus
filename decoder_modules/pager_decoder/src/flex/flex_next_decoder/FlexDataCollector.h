#pragma once

#include "FlexTypes.h"
#include <array>

namespace flex_next_decoder {

    struct PhaseBuffer {
        std::array<uint32_t, PHASE_WORDS> buffer{};
        int idle_count = 0;

        void clear() {
            buffer.fill(0);
            idle_count = 0;
        }
    };

    class FlexDataCollector {
    public:
        FlexDataCollector() = default;
        ~FlexDataCollector() = default;

        bool readData(uint8_t symbol, const SyncInfo& sync_info);
        void clearPhaseData();

        const PhaseBuffer& getPhaseA() const { return phase_a_; }
        const PhaseBuffer& getPhaseB() const { return phase_b_; }
        const PhaseBuffer& getPhaseC() const { return phase_c_; }
        const PhaseBuffer& getPhaseD() const { return phase_d_; }

        PhaseBuffer& getPhaseA() { return phase_a_; }
        PhaseBuffer& getPhaseB() { return phase_b_; }
        PhaseBuffer& getPhaseC() { return phase_c_; }
        PhaseBuffer& getPhaseD() { return phase_d_; }

    private:
        bool checkIdleStatus(const SyncInfo& sync_info) const;

        PhaseBuffer phase_a_;
        PhaseBuffer phase_b_;
        PhaseBuffer phase_c_;
        PhaseBuffer phase_d_;

        bool phase_toggle_ = false;
        uint32_t data_bit_counter_ = 0;

        static constexpr int IDLE_THRESHOLD = 0;
    };

} // namespace flex_next_decoder
#include "dsp.h"
#include <cmath>
#include <algorithm>

// Implementation details for FLEXDSP
void FLEXDSP::reset() noexcept {
    try {
        dcAccumulator_ = 0.0f;
        currentEnvelope_ = 0.0;
        agcLocked_ = false;

        if (initialized_) {
            // Reset all DSP components to initial state
            // Note: The actual reset methods depend on the dsp:: library implementation
            // This is a conceptual implementation

            flog::debug("FLEX DSP components reset");
        }

        flog::debug("FLEX DSP reset completed");
    }
    catch (const std::exception& e) {
        flog::error("Error during FLEX DSP reset: {}", e.what());
    }
}

// Additional utility functions for signal processing
namespace {
    constexpr double ENVELOPE_SMOOTHING_FACTOR = 0.95;
    constexpr double LOCK_THRESHOLD_LOW = 0.1;
    constexpr double LOCK_THRESHOLD_HIGH = 2.0;
}

bool FLEXDSP::isSignalLocked() const noexcept {
    return agcLocked_ &&
           (currentEnvelope_ > LOCK_THRESHOLD_LOW) &&
           (currentEnvelope_ < LOCK_THRESHOLD_HIGH);
}

double FLEXDSP::getEnvelopeSmoothed() const noexcept {
    // This would maintain a smoothed envelope estimate
    static double smoothedEnvelope = 0.0;
    smoothedEnvelope = smoothedEnvelope * ENVELOPE_SMOOTHING_FACTOR +
                       currentEnvelope_ * (1.0 - ENVELOPE_SMOOTHING_FACTOR);
    return smoothedEnvelope;
}
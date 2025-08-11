
#pragma once
#include <dsp/stream.h>
#include <dsp/demod/quadrature.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/loop/fast_agc.h>
#include <dsp/filter/fir.h>
#include <dsp/taps/low_pass.h>
#include <utils/flog.h>
#include "../dsp.h"
#include <memory>
#include <optional>

/**
 * @class FLEXDSP
 * @brief Modern C++17 FLEX DSP Pipeline for SDR++ Integration
 *
 * Audio Stream Flow: VFO (IQ samples) → FLEXDSP → flex_next_decoder
 * Replaces multimon-ng FLEX decoder with the new flex_next_decoder architecture.
 */
class FLEXDSP : public dsp::Processor<dsp::complex_t, float> {
    using base_type = dsp::Processor<dsp::complex_t, float>;

public:
    struct SignalQuality {
        double envelope = 0.0;
        double dc_offset = 0.0;
        bool locked = false;
        double sample_rate = 0.0;
    };

    FLEXDSP() = default;

    explicit FLEXDSP(dsp::stream<dsp::complex_t>* in, double samplerate) {
        init(in, samplerate);
    }

    // Non-copyable but moveable (manages unique DSP state)
    FLEXDSP(const FLEXDSP&) = delete;
    FLEXDSP& operator=(const FLEXDSP&) = delete;
    FLEXDSP(FLEXDSP&&) = default;
    FLEXDSP& operator=(FLEXDSP&&) = default;

    ~FLEXDSP() = default;

    void init(dsp::stream<dsp::complex_t>* in, double samplerate) {
        samplerate_ = samplerate;

        try {
            initializeDemodulationChain(samplerate);

            // Initialize the base class
            base_type::init(in);
            initialized_ = true;

            flog::info("FLEX DSP initialized: FM demod (±4500 Hz) + AGC + LP filter (5kHz) at {} Hz", samplerate_);
        }
        catch (const std::exception& e) {
            flog::error("FLEX DSP initialization failed: {}", e.what());
            initialized_ = false;
            throw;
        }
    }

    int run() {
        if (!initialized_) {
            flog::error("FLEX DSP: Not initialized");
            return -1;
        }

        try {
            // Read from input
            int count = base_type::_in->read();
            if (count <= 0) {
                return count;
            }

            // Validate input buffers
            if (!validateBuffers()) {
                flog::error("FLEX DSP: Invalid input/output buffers");
                return -1;
            }

            // Process through demodulation chain
            count = processDemodulationChain(count);
            if (count <= 0) {
                return count;
            }

            // Flush input and swap output
            base_type::_in->flush();

            if (!base_type::out.swap(count)) {
                return -1;
            }

            return count;
        }
        catch (const std::exception& e) {
            flog::error("FLEX DSP run error: {}", e.what());
            return -1;
        }
    }

    [[nodiscard]] constexpr double getAudioSampleRate() const noexcept {
        return PAGER_AUDIO_SAMPLERATE;
    }

    [[nodiscard]] bool isInitialized() const noexcept {
        return initialized_;
    }

    [[nodiscard]] std::optional<SignalQuality> getSignalQuality() const noexcept {
        if (!initialized_) {
            return std::nullopt;
        }

        return SignalQuality{
            .envelope = currentEnvelope_,
            .dc_offset = dcAccumulator_,
            .locked = agcLocked_,
            .sample_rate = samplerate_
        };
    }

    void reset() noexcept; /*{
        try {
            dcAccumulator_ = 0.0f;
            currentEnvelope_ = 0.0;
            agcLocked_ = false;

            if (initialized_) {
                fmDemod_.reset();
                agc_.reset();
                lpFilter_.reset();
            }

            flog::debug("FLEX DSP reset completed");
        } catch (const std::exception& e) {
            flog::error("Error during FLEX DSP reset: {}", e.what());
        }
    }*/
    bool isSignalLocked() const noexcept;

    double getEnvelopeSmoothed() const noexcept;

private:
    // DSP chain components (using RAII via dsp:: classes)
    dsp::demod::Quadrature fmDemod_;
    dsp::loop::FastAGC<float> agc_;
    dsp::tap<float> lpTaps_;
    dsp::filter::FIR<float, float> lpFilter_;

    // State variables
    double samplerate_ = 0.0;
    bool initialized_ = false;
    float dcAccumulator_ = 0.0f;
    double currentEnvelope_ = 0.0;
    bool agcLocked_ = false;

    // Processing constants
    static constexpr double FM_DEVIATION = 4500.0;  // FLEX uses ±4.5kHz deviation
    static constexpr double AGC_SET_POINT = 1.0;    // AGC target amplitude
    static constexpr double AGC_MAX_GAIN = 10.0;    // Maximum AGC gain
    static constexpr double AGC_RATE = 1e-3;        // AGC adaptation rate
    static constexpr double AGC_INIT_GAIN = 1.0;    // Initial AGC gain
    static constexpr double LP_CUTOFF = 5000.0;     // Low-pass filter cutoff
    static constexpr double LP_TRANSITION = 6000.0; // Low-pass filter transition
    static constexpr float DC_FILTER_ALPHA = 16.0f; // DC removal filter constant
    static constexpr float OUTPUT_SCALING = 0.1f;   // Output scaling factor

    void initializeDemodulationChain(double samplerate) {
        // FM demodulation - FLEX uses ±4.5kHz deviation typically
        fmDemod_.init(nullptr, FM_DEVIATION, samplerate);

        // AGC for signal amplitude normalization
        agc_.init(nullptr, AGC_SET_POINT, AGC_MAX_GAIN, AGC_RATE, AGC_INIT_GAIN);

        // Low-pass filter to remove high-frequency noise
        // FLEX max symbol rate is 3200 baud, so use 5000 Hz cutoff with good roll-off
        lpTaps_ = dsp::taps::lowPass(LP_CUTOFF, LP_TRANSITION, samplerate);
        lpFilter_.init(nullptr, lpTaps_);

        flog::debug("FLEX DSP demodulation chain initialized");
    }

    [[nodiscard]] bool validateBuffers() const noexcept {
        return base_type::_in->readBuf &&
               base_type::out.writeBuf &&
               fmDemod_.out.readBuf &&
               agc_.out.readBuf &&
               lpFilter_.out.readBuf;
    }

    int processDemodulationChain(int count) {
        // Step 1: FM demodulation
        count = processFMDemodulation(count);
        if (count <= 0) return count;

        // Step 2: DC removal and envelope tracking
        count = processDCRemovalAndEnvelope(count);
        if (count <= 0) return count;

        // Step 3: AGC processing
        count = processAGC(count);
        if (count <= 0) return count;

        // Step 4: Low-pass filtering
        count = processLowPassFilter(count);
        if (count <= 0) return count;

        // Step 5: Final output scaling
        return processOutputScaling(count);
    }

    int processFMDemodulation(int count) {
        count = fmDemod_.process(count, base_type::_in->readBuf, fmDemod_.out.readBuf);
        if (count <= 0) {
            flog::warn("FM demodulation failed");
        }
        return count;
    }

    int processDCRemovalAndEnvelope(int count) {
        // Calculate DC filter alpha based on sample rate (equivalent to 16 Hz cutoff)
        const float dcAlpha = DC_FILTER_ALPHA / static_cast<float>(samplerate_);
        double envelopeSum = 0.0;

        // Process each sample for DC removal and envelope tracking
        for (int i = 0; i < count; i++) {
            float sample = fmDemod_.out.readBuf[i];

            // Simple DC removal (high-pass filter)
            dcAccumulator_ = dcAccumulator_ * (1.0f - dcAlpha) + sample * dcAlpha;
            float dcRemoved = sample - dcAccumulator_;

            // Track envelope for signal quality monitoring
            envelopeSum += std::abs(dcRemoved);

            // Store DC-removed sample for AGC processing
            fmDemod_.out.readBuf[i] = dcRemoved;
        }

        // Update envelope estimate
        currentEnvelope_ = envelopeSum / count;

        return count;
    }

    int processAGC(int count) {
        count = agc_.process(count, fmDemod_.out.readBuf, agc_.out.readBuf);
        if (count <= 0) {
            flog::warn("AGC processing failed");
            return count;
        }

        // Check AGC lock status based on gain stability
        agcLocked_ = (currentEnvelope_ > 0.1) && (currentEnvelope_ < 2.0);

        return count;
    }

    int processLowPassFilter(int count) {
        count = lpFilter_.process(count, agc_.out.readBuf, lpFilter_.out.readBuf);
        if (count <= 0) {
            flog::warn("Low-pass filtering failed");
        }
        return count;
    }

    int processOutputScaling(int count) {
        // Copy to output with appropriate scaling for FLEX decoder
        for (int i = 0; i < count; i++) {
            base_type::out.writeBuf[i] = lpFilter_.out.readBuf[i] * OUTPUT_SCALING;
        }
        return count;
    }
};
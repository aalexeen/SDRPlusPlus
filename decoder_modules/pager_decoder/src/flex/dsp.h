#pragma once
#include <dsp/stream.h>
#include <dsp/demod/quadrature.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/loop/fast_agc.h>
#include <dsp/filter/fir.h>
#include <dsp/taps/low_pass.h>
#include <utils/flog.h>
#include "../dsp.h"

class FLEXDSP : public dsp::Processor<dsp::complex_t, float> {
    using base_type = dsp::Processor<dsp::complex_t, float>;
public:
    FLEXDSP() : initialized(false) {}
    FLEXDSP(dsp::stream<dsp::complex_t>* in, double samplerate) {
        init(in, samplerate);
    }

    void init(dsp::stream<dsp::complex_t>* in, double samplerate) {
        _samplerate = samplerate;

        try {
            // FM demodulation - FLEX uses ±4.5kHz deviation typically
            fmDemod.init(nullptr, 4500.0, samplerate);

            // AGC for signal amplitude normalization
            // FastAGC(input, setPoint, maxGain, rate, initGain)
            agc.init(nullptr, 1.0, 10.0, 1e-3, 1.0);  // setPoint=1.0, maxGain=10.0, rate=1e-3, initGain=1.0

            // Low-pass filter to remove high-frequency noise
            // FLEX max symbol rate is 3200 baud, so use 5000 Hz cutoff with good roll-off
            lpTaps = dsp::taps::lowPass(5000.0, 6000.0, samplerate);
            lpFilter.init(nullptr, lpTaps);

            // Initialize the base class
            base_type::init(in);
            initialized = true;
            flog::info("FLEX DSP initialized: FM demod (±4500 Hz) + AGC + LP filter (5kHz) at {} Hz", samplerate);
        } catch (const std::exception& e) {
            flog::error("FLEX DSP initialization failed: {}", e.what());
            initialized = false;
            throw;
        }
    }

    int run() {
        if (!initialized) {
            return -1;
        }

        try {
            // Read from input
            int count = base_type::_in->read();
            if (count <= 0) {
                return count;
            }

            // Validate input buffers exist
            if (!base_type::_in->readBuf || !base_type::out.writeBuf) {
                flog::error("FLEX DSP: Invalid input/output buffers");
                return -1;
            }

            // FM demodulation
            if (!fmDemod.out.readBuf) {
                flog::error("FLEX DSP: FM demod output buffer is null");
                return -1;
            }

            count = fmDemod.process(count, base_type::_in->readBuf, fmDemod.out.readBuf);
            if (count <= 0) {
                return count;
            }

            // Simple DC removal using a high-pass filter approach
            // This mimics the original multimon-ng DC offset removal
            static float dcAccumulator = 0.0f;
            const float dcAlpha = 16.0f / _samplerate;  // Equivalent to 16 Hz cutoff

            // AGC processing with DC removal
            if (!agc.out.readBuf) {
                flog::error("FLEX DSP: AGC output buffer is null");
                return -1;
            }

            // First pass: DC removal
            for (int i = 0; i < count; i++) {
                float sample = fmDemod.out.readBuf[i];

                // Simple DC removal (high-pass filter)
                dcAccumulator = dcAccumulator * (1.0f - dcAlpha) + sample * dcAlpha;
                float dcRemoved = sample - dcAccumulator;

                // Store DC-removed sample for AGC processing
                fmDemod.out.readBuf[i] = dcRemoved;
            }

            // AGC processing
            count = agc.process(count, fmDemod.out.readBuf, agc.out.readBuf);
            if (count <= 0) {
                return count;
            }

            // Low-pass filtering to remove high-frequency noise
            if (!lpFilter.out.readBuf) {
                flog::error("FLEX DSP: LP filter output buffer is null");
                return -1;
            }

            count = lpFilter.process(count, agc.out.readBuf, lpFilter.out.readBuf);
            if (count <= 0) {
                return count;
            }

            // Copy to output with appropriate scaling for FLEX decoder
            for (int i = 0; i < count; i++) {
                base_type::out.writeBuf[i] = lpFilter.out.readBuf[i] * 0.1f;
            }

            // Flush input and swap output
            base_type::_in->flush();

            if (!base_type::out.swap(count)) {
                return -1;
            }

            return count;

        } catch (const std::exception& e) {
            flog::error("FLEX DSP run error: {}", e.what());
            return -1;
        }
    }

    double getAudioSampleRate() const {
        return PAGER_AUDIO_SAMPLERATE;
    }

    bool isInitialized() const {
        return initialized;
    }

private:
    dsp::demod::Quadrature fmDemod;
    dsp::loop::FastAGC<float> agc;
    dsp::tap<float> lpTaps;
    dsp::filter::FIR<float, float> lpFilter;
    double _samplerate;
    bool initialized;
};
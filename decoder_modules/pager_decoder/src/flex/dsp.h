#pragma once
#include <dsp/stream.h>
#include <dsp/demod/quadrature.h>
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
            // Try different FM demod parameters - maybe the issue is with gain or deviation
            // FLEX typical deviation is around ±4.5kHz, so try different values
            fmDemod.init(nullptr, 4500.0, samplerate);  // Try positive deviation instead of 0.0

            // DON'T free the buffer yet - maybe that's causing the issue
            // fmDemod.out.free();

            // Initialize the base class
            base_type::init(in);
            initialized = true;
            flog::info("Safer FM FLEX DSP initialized at {} Hz with ±4500 Hz deviation", samplerate);
        } catch (const std::exception& e) {
            flog::error("Safer FM FLEX DSP initialization failed: {}", e.what());
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

            // Validate buffers exist
            if (!base_type::_in->readBuf || !base_type::out.writeBuf) {
                flog::error("Safer FM FLEX DSP: Invalid buffers");
                return -1;
            }

            // Try FM demodulation with buffer validation
            if (!fmDemod.out.readBuf) {
                flog::error("FM demod readBuf is null");
                return -1;
            }

            count = fmDemod.process(count, base_type::_in->readBuf, fmDemod.out.readBuf);
            if (count <= 0) {
                return count;
            }

            // Simple copy from FM demod output to final output
            for (int i = 0; i < count; i++) {
                base_type::out.writeBuf[i] = fmDemod.out.readBuf[i] * 0.1f;
            }

            // Flush input and swap output
            base_type::_in->flush();

            if (!base_type::out.swap(count)) {
                return -1;
            }

            return count;

        } catch (const std::exception& e) {
            flog::error("Safer FM FLEX DSP run error: {}", e.what());
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
    double _samplerate;
    bool initialized;
};
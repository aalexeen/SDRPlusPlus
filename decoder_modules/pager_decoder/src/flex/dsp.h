#pragma once
#include <dsp/stream.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/demod/quadrature.h>
#include <dsp/filter/fir.h>
#include <dsp/taps/low_pass.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/loop/fast_agc.h>
#include "../dsp.h"  // Include the common definitions

class FLEXDSP : public dsp::Processor<dsp::complex_t, float> {
    using base_type = dsp::Processor<dsp::complex_t, float>;
public:
    FLEXDSP() {}
    FLEXDSP(dsp::stream<dsp::complex_t>* in, double samplerate) {
        init(in, samplerate);
    }

    void init(dsp::stream<dsp::complex_t>* in, double samplerate) {
        _samplerate = samplerate;

        // FM Demodulator - converts FSK to audio
        // FLEX uses FSK, so FM demod will convert freq shifts to amplitude changes
        fmDemod.init(NULL, 0.0, samplerate);

        // DC Blocker - removes DC component
        dcBlocker.init(NULL, 0.001);

        // AGC - automatic gain control for consistent audio levels
        agc.init(NULL, 0.5, 10.0, 0.001, 1.0); // setPoint, maxGain, rate, initGain

        // Low-pass filter - audio bandwidth limiting (~10kHz)
        auto lpTaps = dsp::taps::lowPass(10000.0, 2000.0, samplerate);
        lpFilter.init(NULL, lpTaps);

        // Resampler - convert to audio sample rate (22050 Hz)
        if (samplerate != PAGER_AUDIO_SAMPLERATE) {
            resampler.init(NULL, samplerate, PAGER_AUDIO_SAMPLERATE);
            needsResampling = true;
        } else {
            needsResampling = false;
        }

        // Free intermediate buffers to save memory
        fmDemod.out.free();
        dcBlocker.out.free();
        agc.out.free();
        lpFilter.out.free();
        if (needsResampling) { resampler.out.free(); }

        // Initialize base processor
        base_type::init(in);
    }

    int process(int count, dsp::complex_t* in, float* out) {
        // FM demodulation (FSK -> audio)
        count = fmDemod.process(count, in, fmDemod.out.readBuf);

        // Remove DC component
        count = dcBlocker.process(count, fmDemod.out.readBuf, dcBlocker.out.readBuf);

        // Automatic gain control
        count = agc.process(count, dcBlocker.out.readBuf, agc.out.readBuf);

        // Low-pass filtering
        count = lpFilter.process(count, agc.out.readBuf, lpFilter.out.readBuf);

        // Resample to audio rate if needed
        if (needsResampling) {
            count = resampler.process(count, lpFilter.out.readBuf, out);
        } else {
            memcpy(out, lpFilter.out.readBuf, count * sizeof(float));
        }

        return count;
    }

    int run() {
        int count = base_type::_in->read();
        if (count < 0) { return -1; }

        count = process(count, base_type::_in->readBuf, base_type::out.writeBuf);

        base_type::_in->flush();
        if (!base_type::out.swap(count)) { return -1; }
        return count;
    }

    double getAudioSampleRate() const { return PAGER_AUDIO_SAMPLERATE; }

private:
    dsp::demod::Quadrature fmDemod;           // FM demodulator
    dsp::correction::DCBlocker<float> dcBlocker;  // DC removal
    dsp::loop::FastAGC<float> agc;            // Automatic gain control
    dsp::filter::FIR<float, float> lpFilter;  // Low-pass filter
    dsp::multirate::RationalResampler<float> resampler; // Sample rate conversion

    double _samplerate;
    bool needsResampling;
};
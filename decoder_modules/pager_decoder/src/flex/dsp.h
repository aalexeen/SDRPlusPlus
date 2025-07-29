#pragma once
#include <dsp/stream.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/demod/quadrature.h>
#include <dsp/filter/fir.h>
#include <dsp/taps/low_pass.h>
#include <dsp/correction/dc_blocker.h>
#include <dsp/loop/fast_agc.h>

#define AUDIO_SAMPLERATE 22050.0  // Standard audio rate for multimon-ng

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
        agc.init(NULL, 0.001, 1.0);
        
        // Low-pass filter - audio bandwidth limiting (~10kHz)
        auto lpTaps = dsp::taps::lowPass<float>(10000.0, 2000.0, samplerate);
        lpFilter.init(NULL, lpTaps);
        
        // Resampler - convert to audio sample rate (22050 Hz)
        if (samplerate != AUDIO_SAMPLERATE) {
            resampler.init(NULL, samplerate, AUDIO_SAMPLERATE);
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

    double getAudioSampleRate() const { return AUDIO_SAMPLERATE; }

private:
    dsp::demod::Quadrature fmDemod;           // FM demodulator
    dsp::correction::DCBlocker<float> dcBlocker;  // DC removal
    dsp::loop::FastAGC<float> agc;            // Automatic gain control
    dsp::filter::FIR<float, float> lpFilter;  // Low-pass filter
    dsp::multirate::RationalResampler<float> resampler; // Sample rate conversion
    
    double _samplerate;
    bool needsResampling;
};

// FLEX Decoder that uses audio input (adapted from multimon-ng)
class FLEXDecoder : public Decoder {
public:
    FLEXDecoder(const std::string& name, VFOManager::VFO* vfo) {
        this->name = name;
        this->vfo = vfo;

        // Set VFO parameters for FLEX (typically 929-932 MHz, 25kHz bandwidth)
        vfo->setBandwidthLimits(25000, 25000, true);
        vfo->setSampleRate(AUDIO_SAMPLERATE, 25000);
        
        // Initialize DSP chain
        dsp.init(vfo->output, vfo->getSampleRate());
        
        // Audio handler - receives audio samples for FLEX decoding
        audioHandler.init(&dsp.out, _audioHandler, this);
        
        // Initialize multimon-ng FLEX decoder here
        initMultimonFLEX();
    }

    ~FLEXDecoder() {
        stop();
    }

    void showMenu() override {
        ImGui::Text("FLEX Decoder (Audio-based)");
        ImGui::Text("Sample Rate: %.0f Hz", dsp.getAudioSampleRate());
        
        // Add FLEX-specific controls here
        ImGui::Checkbox("Show Raw Data", &showRawData);
        
        // You can add frequency offset, filter settings, etc.
    }

    void setVFO(VFOManager::VFO* vfo) override {
        this->vfo = vfo;
        vfo->setBandwidthLimits(25000, 25000, true);
        vfo->setSampleRate(AUDIO_SAMPLERATE, 25000);
        dsp.setInput(vfo->output);
    }

    void start() override {
        dsp.start();
        audioHandler.start();
    }

    void stop() override {
        dsp.stop();
        audioHandler.stop();
    }

private:
    static void _audioHandler(float* data, int count, void* ctx) {
        FLEXDecoder* _this = (FLEXDecoder*)ctx;
        _this->processAudioSamples(data, count);
    }

    void processAudioSamples(float* samples, int count) {
        // Convert float samples to format expected by multimon-ng
        // multimon-ng typically expects 16-bit signed samples
        std::vector<int16_t> audioData(count);
        
        for (int i = 0; i < count; i++) {
            // Scale and clamp to 16-bit range
            float sample = samples[i] * 32767.0f;
            if (sample > 32767.0f) sample = 32767.0f;
            if (sample < -32768.0f) sample = -32768.0f;
            audioData[i] = (int16_t)sample;
        }
        
        // Feed to multimon-ng FLEX decoder
        // This is where you'd call the adapted multimon-ng functions
        multimonFLEXProcess(audioData.data(), count);
    }

    void initMultimonFLEX() {
        // Initialize multimon-ng FLEX decoder state
        // You'll need to adapt multimon-ng's FLEX initialization code here
        
        // Example structure (adapt from multimon-ng source):
        // flex_init(&flexState);
        // flexState.callback = flexMessageCallback;
    }

    void multimonFLEXProcess(int16_t* samples, int count) {
        // Process audio samples through multimon-ng FLEX decoder
        // Adapt multimon-ng's FLEX processing functions here
        
        // Example (adapt from multimon-ng):
        // flex_process(&flexState, samples, count);
    }

    static void flexMessageCallback(/* FLEX message parameters */) {
        // Handle decoded FLEX messages
        // Log or display the message
        flog::info("FLEX Message: [details here]");
    }

    std::string name;
    VFOManager::VFO* vfo;
    
    FLEXDSP dsp;
    dsp::sink::Handler<float> audioHandler;
    
    bool showRawData = false;
    
    // multimon-ng FLEX decoder state
    // You'll need to define this based on multimon-ng's FLEX structures
    // struct flex_state flexState;
};
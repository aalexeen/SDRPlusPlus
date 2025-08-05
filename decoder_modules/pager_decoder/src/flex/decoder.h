#pragma once
#include "../decoder.h"
#include <signal_path/vfo_manager.h>
#include <utils/optionlist.h>
#include <gui/widgets/symbol_diagram.h>
#include <gui/style.h>
#include <dsp/sink/handler_sink.h>
#include <utils/flog.h>
#include <memory>
#include "dsp.h"           // Local FLEX DSP header
#include "flex_next.h"     // FlexDecoderWrapper
#include "../BCHCode.h"    // BCH error correction (up one directory)

class FLEXDecoder : public Decoder {
public:
    FLEXDecoder(const std::string& name, VFOManager::VFO* vfo) : name(name), vfo(vfo) {
        // Set VFO parameters for FLEX (typically 929-932 MHz, 25kHz bandwidth)
        vfo->setBandwidthLimits(25000, 25000, true);
        vfo->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);

        // Initialize DSP chain
        dsp.init(vfo->output, 24000);

        // Audio handler - receives audio samples for FLEX decoding
        audioHandler.init(&dsp.out, _audioHandler, this);

        // Initialize FLEX decoder with BCH error correction
        initFLEXDecoder();
    }

    ~FLEXDecoder() {
        stop();
    }

    void showMenu() override {
        ImGui::Text("FLEX Decoder (Multimon-ng based)");
        ImGui::Text("Sample Rate: %.0f Hz", dsp.getAudioSampleRate());

        // Add FLEX-specific controls
        ImGui::Checkbox("Show Raw Data", &showRawData);
        ImGui::Checkbox("Show Errors", &showErrors);

        if (ImGui::Button("Reset Decoder")) {
            resetDecoder();
        }
    }

    void setVFO(VFOManager::VFO* vfo) override {
        this->vfo = vfo;
        vfo->setBandwidthLimits(25000, 25000, true);
        vfo->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);
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
        for (int i = 0; i < count; i++) {
            // Scale and clamp to appropriate range for FLEX decoder
            float sample = samples[i];

            // Feed to converted multimon-ng FLEX decoder
            // This calls your converted flex_next functions
            processFlexSample(sample);
        }
    }

    void initFLEXDecoder() {
        // Initialize BCH error correction
        static const int primitive_poly[] = {1, 0, 1, 0, 0, 1}; // Example for BCH(31,21,5)
        bchDecoder = std::make_unique<BCHCode>(primitive_poly, 5, 31, 21, 2);

        // Initialize FLEX decoder wrapper
        flexDecoder = std::make_unique<FlexDecoderWrapper>();
        flexDecoder->setMessageCallback([this](int64_t addr, int type, const std::string& data) {
            handleFlexMessage(addr, type, data);
        });
    }

    void processFlexSample(float sample) {
        // This integrates with your converted flex_next.cpp functions
        if (flexDecoder) {
            flexDecoder->processSample(sample);
        }
    }

    void handleFlexMessage(int64_t address, int type, const std::string& data) {
        // Console output for testing
        printf("FLEX: Addr=%ld Type=%d Data=%s\n",
               address, type, data.c_str());

        // Also use flog for SDR++ logging
        flog::info("FLEX Message - Addr: {}, Type: {}, Data: {}",
                   address, type, data);

        // You can add GUI display, logging, forwarding, etc. here
    }

    void resetDecoder() {
        if (flexDecoder) {
            flexDecoder->reset();
        }
    }

    std::string name;
    VFOManager::VFO* vfo;

    FLEXDSP dsp;
    dsp::sink::Handler<float> audioHandler;

    // Converted multimon-ng components
    std::unique_ptr<BCHCode> bchDecoder;
    std::unique_ptr<FlexDecoderWrapper> flexDecoder;

    bool showRawData = false;
    bool showErrors = false;
};
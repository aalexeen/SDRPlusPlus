#pragma once
#include "../decoder.h"
#include <signal_path/vfo_manager.h>
#include <utils/optionlist.h>
#include <gui/widgets/symbol_diagram.h>
#include <gui/style.h>
#include <dsp/sink/handler_sink.h>
#include <utils/flog.h>
#include <memory>
#include "dsp.h"        // Local FLEX DSP header
#include "flex_next.h"  // FlexDecoderWrapper
#include "../BCHCode.h" // BCH error correction (up one directory)

class FLEXDecoder : public Decoder {
public:
    FLEXDecoder(const std::string& name, VFOManager::VFO* vfo) : name(name), vfo(vfo), initialized(false) {
        try {
            // Set VFO parameters for FLEX (typically 929-932 MHz, 25kHz bandwidth)
            vfo->setBandwidthLimits(12500, 12500, true);
            vfo->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);

            // Initialize DSP chain with validation
            dsp.init(vfo->output, 24000); // Use fixed sample rate

            if (!dsp.isInitialized()) {
                throw std::runtime_error("Failed to initialize FLEX DSP");
            }

            // Initialize FLEX decoder with BCH error correction
            initFLEXDecoder();

            initialized = true;
            flog::info("FLEX decoder created successfully");

            // Audio handler - receives audio samples for FLEX decoding
            audioHandler.init(&dsp.out, _audioHandler, this);
        }
        catch (const std::exception& e) {
            flog::error("Failed to create FLEX decoder: {}", e.what());
            initialized = false;
        }
    }

    ~FLEXDecoder() {
        stop();
    }

    void showMenu() override {
        if (!initialized) {
            ImGui::Text("FLEX Decoder (FAILED TO INITIALIZE)");
            ImGui::Text("Check logs for initialization errors");
            return;
        }

        ImGui::Text("FLEX Decoder (Multimon-ng based)");
        ImGui::Text("Sample Rate: %.0f Hz", dsp.getAudioSampleRate());
        ImGui::Text("DSP Status: %s", dsp.isInitialized() ? "OK" : "ERROR");

        // Add FLEX-specific controls
        ImGui::Checkbox("Show Raw Data", &showRawData);
        ImGui::Checkbox("Show Errors", &showErrors);
        // New: Message window toggle
        ImGui::Checkbox("Show Message Window", &showMessageWindow);

        if (ImGui::Button("Reset Decoder")) {
            resetDecoder();
        }
        // Message window display
        if (showMessageWindow) {
            showFlexMessageWindow();
        }
    }

    void setVFO(VFOManager::VFO* vfo) override {
        if (!initialized) {
            flog::warn("FLEX decoder not initialized, cannot set VFO");
            return;
        }

        try {
            this->vfo = vfo;
            vfo->setBandwidthLimits(25000, 25000, true);
            vfo->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);
            dsp.setInput(vfo->output);
            flog::info("FLEX decoder VFO set successfully");
        }
        catch (const std::exception& e) {
            flog::error("Failed to set FLEX decoder VFO: {}", e.what());
        }
    }

    void start() override {
        if (!initialized) {
            flog::error("Cannot start FLEX decoder - not initialized");
            return;
        }

        if (!dsp.isInitialized()) {
            flog::error("Cannot start FLEX decoder - DSP not initialized");
            return;
        }

        try {
            dsp.start();
            audioHandler.start();
            flog::info("FLEX decoder started");
        }
        catch (const std::exception& e) {
            flog::error("Failed to start FLEX decoder: {}", e.what());
        }
    }

    void stop() override {
        if (!initialized) return;

        try {
            audioHandler.stop();
            dsp.stop();
            flog::info("FLEX decoder stopped");
        }
        catch (const std::exception& e) {
            flog::error("Error stopping FLEX decoder: {}", e.what());
        }
    }

private:
    void showFlexMessageWindow() {
        // Use window flags to prevent interaction with other windows
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoFocusOnAppearing;

        // Set initial position and size on first appearance
        static bool first_time = true;
        if (first_time) {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
            first_time = false;
        }

        if (!ImGui::Begin(("FLEX Messages##" + name).c_str(), &showMessageWindow, window_flags)) {
            ImGui::End();
            return;
        }
        // Get messages from the FLEX decoder using the existing global functions
        auto messages = getFlexMessages();

        // Controls
        if (ImGui::Button("Clear Messages")) {
            clearFlexMessages();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &autoScrollMessages);
        ImGui::Separator();

        // Message display area
        ImGui::BeginChild("MessageArea", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& message : messages) {
            ImGui::TextUnformatted(message.c_str());
        }

        // Auto-scroll to bottom if enabled and there are new messages
        if (autoScrollMessages && !messages.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
        ImGui::End();
    }

    static void _audioHandler(float* data, int count, void* ctx) {
        FLEXDecoder* _this = (FLEXDecoder*)ctx;
        if (_this && _this->initialized) {
            _this->processAudioSamples(data, count);
        }
    }

    void processAudioSamples(float* samples, int count) {
        if (!initialized || !samples || count <= 0) {
            return;
        }

        // Log sample reception periodically for debugging
        static int sample_counter = 0;
        sample_counter += count;

        if (sample_counter % (44100 * 5) == 0) { // Log every 5 seconds
            flog::info("FLEX decoder received {} samples (total: {})", count, sample_counter);
        }

        try {
            // Process samples in smaller chunks to avoid overflow
            const int CHUNK_SIZE = 1024;

            for (int i = 0; i < count; i += CHUNK_SIZE) {
                int chunk_size = std::min(CHUNK_SIZE, count - i);

                for (int j = 0; j < chunk_size; j++) {
                    float sample = samples[i + j];

                    // Validate sample
                    if (!std::isfinite(sample)) {
                        continue; // Skip invalid samples
                    }

                    // Clamp sample to reasonable range
                    sample = std::clamp(sample, -10.0f, 10.0f);

                    // Feed to FLEX decoder
                    processFlexSample(sample);
                }
            }
        }
        catch (const std::exception& e) {
            flog::error("Error processing FLEX samples: {}", e.what());
        }
    }

    void processFlexSample(float sample) {
        if (!initialized || !flexDecoder) {
            return;
        }

        try {
            flexDecoder->processSample(sample);
        }
        catch (const std::exception& e) {
            flog::error("Error in FLEX sample processing: {}", e.what());
            // Don't rethrow to avoid cascading crashes
        }
    }

    void initFLEXDecoder() {
        try {
            // Initialize BCH error correction
            static const int primitive_poly[] = { 1, 0, 1, 0, 0, 1 }; // Example for BCH(31,21,5)
            bchDecoder = std::make_unique<BCHCode>(primitive_poly, 5, 31, 21, 2);

            // Initialize FLEX decoder wrapper
            flexDecoder = std::make_unique<FlexDecoderWrapper>();
            flexDecoder->setMessageCallback([this](int64_t addr, int type, const std::string& data) {
                handleFlexMessage(addr, type, data);
            });

            flog::info("FLEX decoder components initialized");
        }
        catch (const std::exception& e) {
            flog::error("Failed to initialize FLEX decoder components: {}", e.what());
            throw;
        }
    }

    void handleFlexMessage(int64_t address, int type, const std::string& data) {
        try {
            // Safe message handling
            if (data.length() > 1000) {
                flog::warn("FLEX message too long, truncating");
                return;
            }

            // Console output for testing
            printf("FLEX: Addr=%ld Type=%d Data=%s\n", address, type, data.c_str());

            // Also use flog for SDR++ logging
            flog::info("FLEX Message - Addr: {}, Type: {}, Data: {}", address, type, data);
        }
        catch (const std::exception& e) {
            flog::error("Error handling FLEX message: {}", e.what());
        }
    }

    void resetDecoder() {
        if (!initialized) return;

        try {
            if (flexDecoder) {
                flexDecoder->reset();
                flog::info("FLEX decoder reset");
            }
        }
        catch (const std::exception& e) {
            flog::error("Error resetting FLEX decoder: {}", e.what());
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
    bool initialized;
    bool showMessageWindow = false;
    bool autoScrollMessages = true;
};
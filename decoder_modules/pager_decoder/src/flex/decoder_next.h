#pragma once
#include "../decoder.h"
#include <signal_path/vfo_manager.h>
#include <utils/optionlist.h>
#include <gui/widgets/symbol_diagram.h>
#include <gui/style.h>
#include <dsp/sink/handler_sink.h>
#include <utils/flog.h>
#include <memory>
#include "dsp.h" // Local FLEX DSP header
#include <thread>
#include <chrono>
#include "../BCHCode.h" // BCH error correction (up one directory)
#include "flex_next_decoder/FlexDecoder.h"
#include "flex_next_decoder/parsers/IMessageParser.h"

#include <iostream>

class FLEXDecoderNext : public Decoder {
public:
    FLEXDecoderNext(const std::string &name, VFOManager::VFO *vfo) : name(name), vfo(vfo), initialized(false) {
        try {
            // Validate VFO first
            if (!vfo) { throw std::runtime_error("VFO is null"); }

            // Set VFO parameters for FLEX (typically 929-932 MHz, 25kHz bandwidth)
            vfo->setBandwidthLimits(12500, 12500, true);
            vfo->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);

            // Wait a moment for VFO to settle
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Initialize DSP chain with validation - use VFO sample rate
            if (!vfo->output) { throw std::runtime_error("VFO output stream is null"); }

            // Initialize DSP chain with validation
            dsp.init(vfo->output, 24000); // Use fixed sample rate

            if (!dsp.isInitialized()) {
                throw std::runtime_error("Failed to initialize FLEX DSP - check sample rates and filters");
            }

            // Initialize FLEX decoder with BCH error correction
            initFLEXDecoder();

            // Audio handler - receives audio samples for FLEX decoding
            audioHandler.init(&dsp.out, _audioHandler, this);

            initialized = true;
            flog::info("FLEX decoder created successfully");
        } catch (const std::exception &e) {
            flog::error("Failed to create FLEX decoder: {}", e.what());
            initialized = false;

            // Clean up partially initialized components
            cleanup();
        }
    }

    ~FLEXDecoderNext() override { stop(); }

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

        // Verbosity level control
        ImGui::Text("Debug Settings:");
        if (ImGui::SliderInt("Verbosity Level", &verbosity_level_, 0, 6, "Level %d")) {
            // Update the FlexDecoder verbosity level when changed
            if (flexDecoderNext) {
                flexDecoderNext->setVerbosityLevel(verbosity_level_);
                flog::info("FLEX decoder verbosity level set to {}", verbosity_level_);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Debug output level:\n0 = Silent\n1 = Errors only\n2 = Info + Errors\n3 = Debug info\n4 "
                              "= Verbose debug\n5 = Very verbose");
        }

        if (ImGui::Button("Reset Decoder")) { resetDecoder(); }
        // Message window display
        if (showMessageWindow) { showFlexMessageWindow(); }
    }

    void setVFO(VFOManager::VFO *vfo) override {
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
        } catch (const std::exception &e) { flog::error("Failed to set FLEX decoder VFO: {}", e.what()); }
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
        } catch (const std::exception &e) { flog::error("Failed to start FLEX decoder: {}", e.what()); }
    }

    void stop() override {
        if (!initialized) return;

        try {
            audioHandler.stop();
            dsp.stop();
            flog::info("FLEX decoder stopped");
        } catch (const std::exception &e) { flog::error("Error stopping FLEX decoder: {}", e.what()); }
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
        // auto messages = getFlexMessages();

        // Controls
        /*if (ImGui::Button("Clear Messages")) {
            clearFlexMessages();
        }*/
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &autoScrollMessages);
        ImGui::Separator();

        // Message display area
        ImGui::BeginChild("MessageArea", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        /*for (const auto& message : messages) {
            ImGui::TextUnformatted(message.c_str());
        }

        // Auto-scroll to bottom if enabled and there are new messages
        if (autoScrollMessages && !messages.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }*/

        ImGui::EndChild();
        ImGui::End();
    }

    static void _audioHandler(float *data, int count, void *ctx) {
        static int total_samples = 0;
        total_samples += count;

        // Log every 22050 samples (1 second worth)
        if (total_samples % 22050 < count) {
            flog::info("Audio handler: {} samples this call, {} total", count, total_samples);
        }

        FLEXDecoderNext *_this = (FLEXDecoderNext *) ctx;
        if (_this && _this->initialized) { _this->processAudioSamples(data, count); }
    }

    void processAudioSamples(float *samples, int count) {
        if (!initialized || !samples || count <= 0) {
            flog::info("processAudioSamples: Invalid call - init={}, samples={}, count={}", initialized,
                       static_cast<const void *>(samples), count);
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
        } catch (const std::exception &e) { flog::error("Error processing FLEX samples: {}", e.what()); }
    }

    void processFlexSample(float sample) {
        if (!initialized || !flexDecoderNext) { return; }

        try {
            flexDecoderNext->processSample(sample);
            if (verbosity_level_ >= 5) {
                std::cout << typeid(*this).name() << ": processFlexSample called" << std::endl;
            }
        } catch (const std::exception &e) {
            flog::error("Error in FLEX sample processing: {}", e.what());
            // Don't rethrow to avoid cascading crashes
        }
    }

    void initFLEXDecoder() {
        try {
            // Initialize BCH error correction
            static const int primitive_poly[] = { 1, 0, 1, 0, 0, 1 }; // Example for BCH(31,21,5)
            bchDecoder = std::make_unique<BCHCode>(primitive_poly, 5, 31, 21, 2);

            // Initialize FLEX decoder next
            flexDecoderNext = std::make_unique<flex_next_decoder::FlexDecoder>(
                    static_cast<uint32_t>(PAGER_AUDIO_SAMPLERATE), verbosity_level_);
            /*flexDecoderNext->setMessageCallback([this](int64_t addr, int type, const std::string& data) {
                handleFlexMessage(addr, type, data);
            });*/
            // Configure decoder settings
            if (!flexDecoderNext) {
                std::cout << "Failed to create FlexDecoder instance" << std::endl;
                return;
            }


            flexDecoderNext->setVerbosityLevel(2); // Set debug level

            flog::info("FLEX decoder (new implementation) initialized");
        } catch (const std::exception &e) {
            flog::error("Failed to initialize FLEX decoder components: {}", e.what());
            throw;
        }
    }

    void handleFlexMessage(const flex_next_decoder::MessageParseResult &result,
                           const flex_next_decoder::MessageParseInput &input) {
        try {
            // Log the message for debugging
            if (verbosity_level_ >= 2) {
                flog::info("FLEX Message received - Type: {}, Content: {}", static_cast<int>(input.type),
                           result.content);
            }

            // Check if we have a valid message decoder
            if (!flexMessageDecoder) {
                flog::error("FlexMessageDecoder not initialized, cannot process message");
                return;
            }

            // Call the output formatter through the message decoder
            flexMessageDecoder->outputFormattedMessage(result, input);

            // Also call the existing handleFlexMessage for compatibility
            if (!result.content.empty()) {
                handleFlexMessage(input.capcode, static_cast<int>(input.type), result.content);
            }
        } catch (const std::exception &e) { flog::error("Error handling FLEX message: {}", e.what()); }
    }

    void handleFlexMessage(int64_t address, int type, const std::string &data) {
        try {
            // Safe message handling (existing function)
            if (data.length() > 1000) {
                flog::warn("FLEX message too long, truncating");
                return;
            }

            // Console output for testing
            printf("FLEX: Addr=%ld Type=%d Data=%s\n", address, type, data.c_str());

            // Also use flog for SDR++ logging
            flog::info("FLEX Message - Addr: {}, Type: {}, Data: {}", address, type, data);
        } catch (const std::exception &e) { flog::error("Error handling FLEX message: {}", e.what()); }
    }


    void resetDecoder() {
        if (!initialized) return;

        try {
            if (flexDecoderNext) {
                flexDecoderNext->reset();
                flog::info("FLEX decoder reset");
            }
        } catch (const std::exception &e) { flog::error("Error resetting FLEX decoder: {}", e.what()); }
    }

    void cleanup() {
        try {
            audioHandler.stop();
            if (dsp.isInitialized()) { dsp.stop(); }
        } catch (...) {
            // Ignore cleanup errors
        }
    }

    std::string name;
    VFOManager::VFO *vfo;

    FLEXDSP dsp;
    dsp::sink::Handler<float> audioHandler;

    // Converted multimon-ng components
    std::unique_ptr<BCHCode> bchDecoder;
    std::unique_ptr<flex_next_decoder::FlexDecoder> flexDecoderNext;

    bool showRawData = false;
    bool showErrors = false;
    bool initialized;
    bool showMessageWindow = false;
    bool autoScrollMessages = true;

private:
    int verbosity_level_ = 2; ///< Debug output level
};

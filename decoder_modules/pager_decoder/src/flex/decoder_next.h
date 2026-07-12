#pragma once
#include "../decoder.h"
#include <signal_path/vfo_manager.h>
#include <utils/optionlist.h>
#include <gui/style.h>
#include <dsp/sink/handler_sink.h>
#include <utils/flog.h>
#include <memory>
#include "dsp.h" // Local FLEX DSP header
#include <thread>
#include <mutex>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include "../BCHCode.h" // BCH error correction (up one directory)
#include "flex_next_decoder/FlexDecoder.h"
#include "flex_next_decoder/FlexFileLogger.h"

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

        ImGui::Text("FLEX Decoder");
        ImGui::Text("Sample Rate: %.0f Hz", dsp.getAudioSampleRate());
        ImGui::Text("DSP Status: %s", dsp.isInitialized() ? "OK" : "ERROR");

        ImGui::Checkbox("Show Message Window", &showMessageWindow);

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
            flexLog.flushRaw(); // ensure a raw capture is complete on stop
            flog::info("FLEX decoder stopped");
        } catch (const std::exception &e) { flog::error("Error stopping FLEX decoder: {}", e.what()); }
    }

private:
    std::vector<std::string> flexMessages;
    // Guards flexMessages: written from the DSP thread (handleFlexMessage) and
    // read/cleared from the GUI thread (showMessageWindow). Without this, a
    // push_back reallocation concurrent with GUI iteration is a use-after-free.
    std::mutex flexMessagesMutex;

    // --- File logger (opt-in via env var SDRPP_FLEX_LOG_DIR) ---------------
    // Single writer, only ever touched from the DSP thread (handleFlexMessage /
    // the diagnostic callback). Never handed to the GUI thread, so it does not
    // reintroduce the flexMessages cross-thread race. Opened once at init.
    // Logic lives in the std-only FlexFileLogger so it is unit-testable in the
    // standalone test harness (no SDR++ deps).
    flex_next_decoder::FlexFileLogger flexLog;

    // Open the log file if SDRPP_FLEX_LOG_DIR is set. Called once at init.
    void initFlexLog() {
        if (flexLog.open(name)) {
            flog::info("FLEX file logging enabled -> {}", flexLog.path());
        }
        if (flexLog.openRaw(name)) {
            flog::info("FLEX raw audio capture enabled -> {} (22050 Hz s16, capped)", flexLog.rawPath());
        }
    }

    // Write one line to the log file (DSP thread only).
    void flexLogLine(const std::string &line) { flexLog.line(line); }

    // Update showFlexMessageWindow
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
        // Controls
        if (ImGui::Button("Clear Messages")) {
            std::lock_guard<std::mutex> lck(flexMessagesMutex);
            flexMessages.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &autoScrollMessages);
        ImGui::Separator();

        // Snapshot under lock so the DSP thread can keep appending while we
        // render, without holding the mutex across ImGui calls.
        std::vector<std::string> messagesSnapshot;
        {
            std::lock_guard<std::mutex> lck(flexMessagesMutex);
            messagesSnapshot = flexMessages;
        }

        // Message display area
        ImGui::BeginChild("MessageArea", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto &message: messagesSnapshot) { ImGui::TextUnformatted(message.c_str()); }

        // Auto-scroll to bottom if enabled and there are new messages
        if (autoScrollMessages && !messagesSnapshot.empty()) { ImGui::SetScrollHereY(1.0f); }

        ImGui::EndChild();
        ImGui::End();
    }

    static void _audioHandler(float *data, int count, void *ctx) {
        FLEXDecoderNext *_this = (FLEXDecoderNext *) ctx;
        if (_this && _this->initialized) { _this->processAudioSamples(data, count); }
    }

    void processAudioSamples(float *samples, int count) {
        if (!initialized || !samples || count <= 0) { return; }

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

                    // Raw capture: dump the EXACT sample the decoder consumes
                    // (post clamp/skip), so a replay through the test harness /
                    // multimon reproduces this decode bit-for-bit. No-op unless
                    // SDRPP_FLEX_RAW_DIR is set.
                    flexLog.raw(&sample, 1);

                    // Feed to FLEX decoder
                    processFlexSample(sample);
                }
            }
        } catch (const std::exception &e) { flog::error("Error processing FLEX samples: {}", e.what()); }
    }

    void processFlexSample(float sample) {
        // if (!initialized || !flexDecoderNext) { return; }
        if (!flexDecoderNext) { return; }

        try {
            flexDecoderNext->processSample(sample);
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

            // Open the opt-in file log (no-op unless SDRPP_FLEX_LOG_DIR is set)
            initFlexLog();

            // Register callback to route messages to handleFlexMessage
            flexDecoderNext->setMessageCallback([this](int64_t addr, int type, const std::string& data) {
                handleFlexMessage(addr, type, data);
            });

            // Register diagnostic callback: surface abnormally dropped pages
            // (uncorrectable BCH words, invalid address/vector, swallowed
            // exceptions) that would otherwise vanish with zero output. Runs on
            // the DSP thread. Tee to the log file and to the GUI message list.
            flexDecoderNext->setDiagnosticCallback([this](const std::string& diag) {
                flexLogLine(diag);
                std::lock_guard<std::mutex> lck(flexMessagesMutex);
                flexMessages.push_back("[" + diag + "]");
            });

            // Configure decoder settings
            if (!flexDecoderNext) {
                flog::error("Failed to create FlexDecoder instance");
                return;
            }

            flog::info("FLEX decoder (new implementation) initialized");
        } catch (const std::exception &e) {
            flog::error("Failed to initialize FLEX decoder components: {}", e.what());
            throw;
        }
    }

    void handleFlexMessage(int64_t address, int type, const std::string &data) {
        try {
            // Safe message handling (existing function)
            if (data.length() > 1000) {
                flog::warn("FLEX message too long, truncating");
                return;
            }

            // Store for GUI display (use the full formatted data directly)
            {
                std::lock_guard<std::mutex> lck(flexMessagesMutex);
                flexMessages.push_back(data);
            }

            // Tee to the opt-in file log (DSP thread, same as the diag callback)
            flexLogLine(data);

            // Also use flog for SDR++ logging
            flog::info("FLEX Message - Addr: {}, Type: {}, Data: {}", address, type, data);
        } catch (const std::exception &e) { flog::error("Error handling FLEX message: {}", e.what()); }
    }

    void resetDecoder() {
        // if (!initialized) return;

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

    bool initialized;
    bool showMessageWindow = false;
    bool autoScrollMessages = true;

private:
    int verbosity_level_ = 2; ///< Debug output level
};

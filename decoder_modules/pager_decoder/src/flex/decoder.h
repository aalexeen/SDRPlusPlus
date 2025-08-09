
#pragma once
#include "../decoder.h"
#include <signal_path/vfo_manager.h>
#include <utils/optionlist.h>
#include <gui/widgets/symbol_diagram.h>
#include <gui/style.h>
#include <dsp/sink/handler_sink.h>
#include <utils/flog.h>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include "dsp.h"        // Local FLEX DSP header
#include "flex_next.h"  // FlexDecoderWrapper
#include "../BCHCode.h" // BCH error correction (up one directory)

class FLEXDecoder : public Decoder {
public:
    FLEXDecoder(const std::string& name, VFOManager::VFO* vfo)
        : name(name), vfo(vfo), initialized(false), healthy(true),
          lastHealthCheck(std::chrono::steady_clock::now()) {

        std::lock_guard<std::recursive_mutex> lock(decoderMutex);

        try {
            flog::info("Initializing FLEX decoder '{}'", name);

            // Validate input parameters
            if (!vfo) {
                throw std::runtime_error("VFO cannot be null");
            }

            if (name.empty()) {
                throw std::runtime_error("Decoder name cannot be empty");
            }

            // Set VFO parameters for FLEX (typically 929-932 MHz, 25kHz bandwidth)
            vfo->setBandwidthLimits(12500, 12500, true);
            vfo->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);

            // Initialize DSP chain with validation
            if (!initializeDSP()) {
                throw std::runtime_error("Failed to initialize FLEX DSP");
            }

            // Initialize FLEX decoder components
            if (!initFLEXDecoder()) {
                throw std::runtime_error("Failed to initialize FLEX decoder components");
            }

            // Audio handler - receives audio samples for FLEX decoding
            audioHandler.init(&dsp.out, _audioHandler, this);

            // Start simplified health monitoring (no background thread)
            lastHealthCheck = std::chrono::steady_clock::now();

            initialized.store(true);
            healthy.store(true);

            flog::info("FLEX decoder '{}' created successfully", name);
        }
        catch (const std::exception& e) {
            flog::error("Failed to create FLEX decoder '{}': {}", name, e.what());
            cleanup();
            initialized.store(false);
            healthy.store(false);
        }
    }

    ~FLEXDecoder() {
        try {
            // Signal shutdown first
            healthy.store(false);
            initialized.store(false);

            // Stop processing
            stop();

            // Clean up resources
            cleanup();

            flog::debug("FLEX decoder '{}' destroyed", name);
        } catch (const std::exception& e) {
            flog::error("Exception in FLEX decoder destructor: {}", e.what());
        }
    }

    void showMenu() override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex);

        if (!initialized.load()) {
            ImGui::Text("FLEX Decoder (FAILED TO INITIALIZE)");
            ImGui::Text("Check logs for initialization errors");
            ImGui::Text("Health: %s", healthy.load() ? "OK" : "UNHEALTHY");

            if (ImGui::Button("Attempt Recovery")) {
                attemptRecovery();
            }
            return;
        }

        // Health status indicator
        bool isHealthy = healthy.load();
        if (isHealthy) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "FLEX Decoder (HEALTHY)");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "FLEX Decoder (UNHEALTHY)");
            ImGui::SameLine();
            if (ImGui::Button("Recover")) {
                attemptRecovery();
            }
        }

        ImGui::Text("Sample Rate: %.0f Hz", dsp.getAudioSampleRate());
        ImGui::Text("DSP Status: %s", dsp.isInitialized() ? "OK" : "ERROR");

        // Performance metrics
        ImGui::Text("Samples Processed: %lu", samplesProcessed.load());
        ImGui::Text("Error Count: %lu", errorCount.load());
        ImGui::Text("Messages Decoded: %lu", messagesDecoded.load());

        // Error rate calculation
        size_t totalSamples = samplesProcessed.load();
        if (totalSamples > 0) {
            double errorRate = (double)errorCount.load() / (double)totalSamples * 100.0;
            ImGui::Text("Error Rate: %.2f%%", errorRate);
        }

        ImGui::Separator();

        // FLEX-specific controls
        ImGui::Checkbox("Show Raw Data", &showRawData);
        ImGui::Checkbox("Show Errors", &showErrors);
        ImGui::Checkbox("Show Message Window", &showMessageWindow);

        if (ImGui::Button("Reset Decoder")) {
            resetDecoder();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear Statistics")) {
            clearStatistics();
        }

        // Message window display
        if (showMessageWindow) {
            showFlexMessageWindow();
        }
    }

    void setVFO(VFOManager::VFO* vfo) override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex);

        if (!initialized.load()) {
            flog::warn("FLEX decoder '{}' not initialized, cannot set VFO", name);
            return;
        }

        if (!vfo) {
            flog::error("Cannot set null VFO for FLEX decoder '{}'", name);
            healthy.store(false);
            return;
        }

        try {
            flog::debug("Setting VFO for FLEX decoder '{}'", name);

            // Stop current operation
            bool wasRunning = isRunning.load();
            if (wasRunning) {
                stop();
            }

            this->vfo = vfo;
            vfo->setBandwidthLimits(25000, 25000, true);
            vfo->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);
            dsp.setInput(vfo->output);

            // Restart if it was running
            if (wasRunning) {
                start();
            }

            flog::info("FLEX decoder '{}' VFO set successfully", name);
        }
        catch (const std::exception& e) {
            flog::error("Failed to set FLEX decoder '{}' VFO: {}", name, e.what());
            healthy.store(false);
        }
    }

    void start() override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex);

        if (!initialized.load()) {
            flog::error("Cannot start FLEX decoder '{}' - not initialized", name);
            return;
        }

        if (!healthy.load()) {
            flog::warn("Starting unhealthy FLEX decoder '{}' - attempting recovery", name);
            if (!attemptRecovery()) {
                flog::error("Recovery failed, cannot start FLEX decoder '{}'", name);
                return;
            }
        }

        if (isRunning.load()) {
            flog::debug("FLEX decoder '{}' already running", name);
            return;
        }

        try {
            if (!dsp.isInitialized()) {
                flog::error("Cannot start FLEX decoder '{}' - DSP not initialized", name);
                healthy.store(false);
                return;
            }

            dsp.start();
            audioHandler.start();
            isRunning.store(true);

            flog::info("FLEX decoder '{}' started successfully", name);
        }
        catch (const std::exception& e) {
            flog::error("Failed to start FLEX decoder '{}': {}", name, e.what());
            healthy.store(false);
            isRunning.store(false);
        }
    }

    void stop() override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex);

        if (!isRunning.load()) {
            return;
        }

        try {
            audioHandler.stop();
            dsp.stop();
            isRunning.store(false);

            flog::info("FLEX decoder '{}' stopped successfully", name);
        }
        catch (const std::exception& e) {
            flog::error("Error stopping FLEX decoder '{}': {}", name, e.what());
            healthy.store(false);
        }
    }

    // Health monitoring interface
    bool isHealthy() const {
        return healthy.load() && initialized.load();
    }

    bool isInitialized() const {
        return initialized.load();
    }

    // Statistics interface
    size_t getSamplesProcessed() const { return samplesProcessed.load(); }
    size_t getErrorCount() const { return errorCount.load(); }
    size_t getMessagesDecoded() const { return messagesDecoded.load(); }

private:
    // Thread safety
    mutable std::recursive_mutex decoderMutex;
    std::atomic<bool> initialized{false};
    std::atomic<bool> healthy{true};
    std::atomic<bool> isRunning{false};

    // Performance monitoring
    std::atomic<size_t> samplesProcessed{0};
    std::atomic<size_t> errorCount{0};
    std::atomic<size_t> messagesDecoded{0};
    std::chrono::steady_clock::time_point lastHealthCheck;

    bool initializeDSP() {
        try {
            dsp.init(vfo->output, 24000); // Use fixed sample rate

            if (!dsp.isInitialized()) {
                flog::error("DSP initialization failed");
                return false;
            }

            flog::info("FLEX DSP initialized: FM demod (±4500 Hz) + AGC + LP filter (5kHz) at {} Hz",
                      dsp.getAudioSampleRate());
            return true;

        } catch (const std::exception& e) {
            flog::error("Exception during DSP initialization: {}", e.what());
            return false;
        }
    }

    bool initFLEXDecoder() {
        try {
            // Initialize BCH error correction
            static const int primitive_poly[] = { 1, 0, 1, 0, 0, 1 }; // Example for BCH(31,21,5)
            bchDecoder = std::make_unique<BCHCode>(primitive_poly, 5, 31, 21, 2);

            if (!bchDecoder) {
                flog::error("Failed to create BCH decoder");
                return false;
            }

            // Initialize FLEX decoder wrapper
            flexDecoder = std::make_unique<FlexDecoderWrapper>();
            if (!flexDecoder) {
                flog::error("Failed to create FLEX decoder wrapper");
                return false;
            }

            flexDecoder->setMessageCallback([this](int64_t addr, int type, const std::string& data) {
                handleFlexMessage(addr, type, data);
            });

            flog::info("FLEX decoder components initialized");
            return true;

        } catch (const std::exception& e) {
            flog::error("Failed to initialize FLEX decoder components: {}", e.what());
            return false;
        }
    }

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

        // Health indicator in message window
        if (healthy.load()) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Decoder Status: HEALTHY");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Decoder Status: UNHEALTHY");
        }

        // Get messages from the FLEX decoder using the existing global functions
        auto messages = getFlexMessages();

        // Controls
        if (ImGui::Button("Clear Messages")) {
            clearFlexMessages();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &autoScrollMessages);
        ImGui::SameLine();
        ImGui::Text("Messages: %lu", messagesDecoded.load());

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
        if (_this && _this->initialized.load() && _this->healthy.load()) {
            _this->processAudioSamples(data, count);
        }
    }

    void processAudioSamples(float* samples, int count) {
        if (!initialized.load() || !healthy.load() || !samples || count <= 0) {
            return;
        }

        // Update statistics
        samplesProcessed.fetch_add(count);

        // Periodic health check during processing (inline, no background thread)
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastCheck = std::chrono::duration_cast<std::chrono::seconds>(now - lastHealthCheck);

        if (timeSinceLastCheck.count() > 30) { // Check every 30 seconds
            performHealthCheck();
            lastHealthCheck = now;
        }

        try {
            // Process samples in smaller chunks to avoid overflow
            const int CHUNK_SIZE = 1024;

            for (int i = 0; i < count; i += CHUNK_SIZE) {
                int chunk_size = std::min(CHUNK_SIZE, count - i);

                for (int j = 0; j < chunk_size; j++) {
                    float sample = samples[i + j];

                    // Validate and sanitize sample
                    if (!std::isfinite(sample)) {
                        errorCount.fetch_add(1);
                        continue; // Skip invalid samples
                    }

                    // Clamp sample to reasonable range
                    if (std::abs(sample) > 10.0f) {
                        sample = std::clamp(sample, -10.0f, 10.0f);
                        errorCount.fetch_add(1);
                    }

                    // Feed to FLEX decoder
                    processFlexSample(sample);
                }
            }
        }
        catch (const std::exception& e) {
            flog::error("Error processing FLEX samples in '{}': {}", name, e.what());
            errorCount.fetch_add(count); // Count entire chunk as errors
            healthy.store(false);
        }
    }

    void processFlexSample(float sample) {
        if (!initialized.load() || !flexDecoder) {
            return;
        }

        try {
            flexDecoder->processSample(sample);
        }
        catch (const std::exception& e) {
            errorCount.fetch_add(1);

            // Only log errors occasionally to avoid spam
            static auto lastErrorLog = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastError = std::chrono::duration_cast<std::chrono::seconds>(now - lastErrorLog);

            if (timeSinceLastError.count() > 60) { // Log errors max once per minute
                flog::error("Error in FLEX sample processing for '{}': {}", name, e.what());
                lastErrorLog = now;
            }

            // Mark as unhealthy if too many errors
            size_t totalSamples = samplesProcessed.load();
            size_t totalErrors = errorCount.load();
            if (totalSamples > 1000 && (double)totalErrors / (double)totalSamples > 0.1) { // 10% error rate
                healthy.store(false);
            }
        }
    }

    void handleFlexMessage(int64_t address, int type, const std::string& data) {
        try {
            // Input validation
            if (data.length() > 1000) {
                flog::warn("FLEX message too long, truncating (decoder: '{}')", name);
                return;
            }

            if (address < 0) {
                flog::warn("Invalid FLEX address: {} (decoder: '{}')", address, name);
                return;
            }

            // Update statistics
            messagesDecoded.fetch_add(1);

            // Safe message handling
            std::string safeData = data;
            // Remove any non-printable characters
            safeData.erase(std::remove_if(safeData.begin(), safeData.end(),
                [](char c) { return !std::isprint(static_cast<unsigned char>(c)); }),
                safeData.end());

            // Console output for testing
            printf("FLEX[%s]: Addr=%ld Type=%d Data=%s\n", name.c_str(), address, type, safeData.c_str());

            // Also use flog for SDR++ logging
            flog::info("FLEX[{}] Message - Addr: {}, Type: {}, Data: {}", name, address, type, safeData);
        }
        catch (const std::exception& e) {
            flog::error("Error handling FLEX message in '{}': {}", name, e.what());
            errorCount.fetch_add(1);
        }
    }

    void resetDecoder() {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex);

        if (!initialized.load()) {
            return;
        }

        try {
            flog::info("Resetting FLEX decoder '{}'", name);

            bool wasRunning = isRunning.load();
            if (wasRunning) {
                stop();
            }

            if (flexDecoder) {
                flexDecoder->reset();
            }

            // Clear statistics
            clearStatistics();

            // Mark as healthy after reset
            healthy.store(true);

            if (wasRunning) {
                start();
            }

            flog::info("FLEX decoder '{}' reset successfully", name);
        }
        catch (const std::exception& e) {
            flog::error("Error resetting FLEX decoder '{}': {}", name, e.what());
            healthy.store(false);
        }
    }

    void clearStatistics() {
        samplesProcessed.store(0);
        errorCount.store(0);
        messagesDecoded.store(0);
        flog::debug("Statistics cleared for FLEX decoder '{}'", name);
    }

    bool attemptRecovery() {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex);

        flog::info("Attempting recovery for FLEX decoder '{}'", name);

        try {
            // Stop everything
            stop();

            // Reinitialize components
            if (!initializeDSP()) {
                flog::error("DSP reinitialization failed during recovery");
                return false;
            }

            if (!initFLEXDecoder()) {
                flog::error("FLEX decoder reinitialization failed during recovery");
                return false;
            }

            // Clear error statistics
            clearStatistics();

            // Mark as healthy
            healthy.store(true);

            flog::info("Recovery successful for FLEX decoder '{}'", name);
            return true;

        } catch (const std::exception& e) {
            flog::error("Recovery failed for FLEX decoder '{}': {}", name, e.what());
            healthy.store(false);
            return false;
        }
    }

    void performHealthCheck() {
        if (!initialized.load()) {
            return;
        }

        try {
            // Check DSP health
            if (!dsp.isInitialized()) {
                flog::warn("DSP not initialized in FLEX decoder '{}'", name);
                healthy.store(false);
                return;
            }

            // Check error rate
            size_t totalSamples = samplesProcessed.load();
            size_t totalErrors = errorCount.load();

            if (totalSamples > 10000) { // Only check after processing significant samples
                double errorRate = (double)totalErrors / (double)totalSamples;

                if (errorRate > 0.15) { // 15% error rate threshold
                    flog::warn("High error rate in FLEX decoder '{}': {:.2f}%", name, errorRate * 100.0);
                    healthy.store(false);
                }
            }

            // Check if decoder components are still valid
            if (!flexDecoder || !bchDecoder) {
                flog::error("FLEX decoder components invalid in '{}'", name);
                healthy.store(false);
            }

        } catch (const std::exception& e) {
            flog::error("Health check exception for FLEX decoder '{}': {}", name, e.what());
            healthy.store(false);
        }
    }

    void cleanup() {
        try {
            if (flexDecoder) {
                flexDecoder.reset();
            }
            if (bchDecoder) {
                bchDecoder.reset();
            }
        } catch (const std::exception& e) {
            flog::error("Exception during cleanup: {}", e.what());
        }
    }

    // Core components
    std::string name;
    VFOManager::VFO* vfo;

    FLEXDSP dsp;
    dsp::sink::Handler<float> audioHandler;

    // Decoder components
    std::unique_ptr<BCHCode> bchDecoder;
    std::unique_ptr<FlexDecoderWrapper> flexDecoder;

    // UI state
    bool showRawData = false;
    bool showErrors = false;
    bool showMessageWindow = false;
    bool autoScrollMessages = true;
};
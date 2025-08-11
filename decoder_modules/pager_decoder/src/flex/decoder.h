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
#include <optional>
#include <sstream>                         // For string formatting
#include "dsp.h"                           // Local FLEX DSP header
#include "flex_next_decoder/FlexDecoder.h" // New decoder
#include "../BCHCode.h"                    // BCH error correction (up one directory)

class FLEXDecoder : public Decoder {
public:
    FLEXDecoder(const std::string& name, VFOManager::VFO* vfo)
        : name_(name), vfo_(vfo), initialized_(false), healthy_(true),
          lastHealthCheck_(std::chrono::steady_clock::now()), isRunning_(false) {

        std::lock_guard<std::recursive_mutex> lock(decoderMutex_);

        try {
            flog::info("Initializing FLEX decoder '{}'", name_);

            // Validate input parameters
            if (!vfo) {
                throw std::runtime_error("VFO cannot be null");
            }

            if (name.empty()) {
                throw std::runtime_error("Decoder name cannot be empty");
            }

            // Set VFO parameters for FLEX (typically 929-932 MHz, 25kHz bandwidth)
            vfo_->setBandwidthLimits(12500, 12500, true);
            vfo_->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);

            // Initialize DSP chain with validation
            if (!initializeDSP()) {
                throw std::runtime_error("Failed to initialize FLEX DSP");
            }

            // Initialize new FLEX decoder
            if (!initFLEXDecoder()) {
                throw std::runtime_error("Failed to initialize FLEX decoder components");
            }

            // Audio handler - receives audio samples for FLEX decoding
            audioHandler_.init(&dsp_.out, audioHandlerCallback, this);

            // Start simplified health monitoring
            lastHealthCheck_ = std::chrono::steady_clock::now();

            initialized_.store(true);
            healthy_.store(true);

            flog::info("FLEX decoder '{}' created successfully", name_);
        }
        catch (const std::exception& e) {
            flog::error("Failed to create FLEX decoder '{}': {}", name_, e.what());
            cleanup();
            initialized_.store(false);
            healthy_.store(false);
        }
    }

    ~FLEXDecoder() {
        try {
            // Signal shutdown first
            healthy_.store(false);
            initialized_.store(false);

            // Stop processing
            stop();

            // Clean up resources
            cleanup();

            flog::debug("FLEX decoder '{}' destroyed", name_);
        }
        catch (const std::exception& e) {
            flog::error("Exception in FLEX decoder destructor: {}", e.what());
        }
    }

    void showMenu() override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex_);

        if (!initialized_.load()) {
            ImGui::Text("FLEX Decoder (FAILED TO INITIALIZE)");
            ImGui::Text("Check logs for initialization errors");
            ImGui::Text("Health: %s", healthy_.load() ? "OK" : "UNHEALTHY");

            if (ImGui::Button("Attempt Recovery")) {
                attemptRecovery();
            }
            return;
        }

        // Health status indicator
        bool isHealthy = healthy_.load();
        if (isHealthy) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "FLEX Decoder (HEALTHY)");
        }
        else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "FLEX Decoder (UNHEALTHY)");
            ImGui::SameLine();
            if (ImGui::Button("Recover")) {
                attemptRecovery();
            }
        }

        ImGui::Text("Sample Rate: %.0f Hz", dsp_.getAudioSampleRate());
        ImGui::Text("DSP Status: %s", dsp_.isInitialized() ? "OK" : "ERROR");

        // Display signal quality metrics
        if (flexDecoder_) {
            auto signalQuality = flexDecoder_->getSignalQuality();
            ImGui::Text("Lock Status: %s", signalQuality.locked ? "LOCKED" : "UNLOCKED");
            ImGui::Text("State: %s", stateToString(signalQuality.state).c_str());
            ImGui::Text("Envelope: %.3f", signalQuality.envelope);
            ImGui::Text("Symbol Rate: %.1f", signalQuality.symbol_rate);
        }

        // Performance metrics
        ImGui::Text("Samples Processed: %lu", samplesProcessed_.load());
        ImGui::Text("Error Count: %lu", errorCount_.load());
        ImGui::Text("Messages Decoded: %lu", messagesDecoded_.load());

        // Error rate calculation
        size_t totalSamples = samplesProcessed_.load();
        if (totalSamples > 0) {
            double errorRate = (double)errorCount_.load() / (double)totalSamples * 100.0;
            ImGui::Text("Error Rate: %.2f%%", errorRate);
        }

        ImGui::Separator();

        // FLEX-specific controls
        ImGui::Checkbox("Show Raw Data", &showRawData_);
        ImGui::Checkbox("Show Errors", &showErrors_);
        ImGui::Checkbox("Show Message Window", &showMessageWindow_);

        if (ImGui::Button("Reset Decoder")) {
            resetDecoder();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear Statistics")) {
            clearStatistics();
        }

        // Verbosity level control
        if (ImGui::SliderInt("Verbosity", &verbosityLevel_, 0, 3)) {
            if (flexDecoder_) {
                flexDecoder_->setVerbosityLevel(verbosityLevel_);
            }
        }

        // Message window display
        if (showMessageWindow_) {
            showFlexMessageWindow();
        }
    }

    void setVFO(VFOManager::VFO* vfo) override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex_);

        if (!initialized_.load()) {
            flog::warn("FLEX decoder '{}' not initialized, cannot set VFO", name_);
            return;
        }

        if (!vfo) {
            flog::error("Cannot set null VFO for FLEX decoder '{}'", name_);
            healthy_.store(false);
            return;
        }

        try {
            flog::debug("Setting VFO for FLEX decoder '{}'", name_);

            // Stop current operation
            bool wasRunning = isRunning_.load();
            if (wasRunning) {
                stop();
            }

            vfo_ = vfo;
            vfo_->setBandwidthLimits(25000, 25000, true);
            vfo_->setSampleRate(PAGER_AUDIO_SAMPLERATE, 25000);
            dsp_.setInput(vfo_->output);

            // Restart if it was running
            if (wasRunning) {
                start();
            }

            flog::info("FLEX decoder '{}' VFO set successfully", name_);
        }
        catch (const std::exception& e) {
            flog::error("Failed to set FLEX decoder '{}' VFO: {}", name_, e.what());
            healthy_.store(false);
        }
    }

    void start() override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex_);

        if (!initialized_.load()) {
            flog::error("Cannot start FLEX decoder '{}' - not initialized", name_);
            return;
        }

        if (!healthy_.load()) {
            flog::warn("Starting unhealthy FLEX decoder '{}' - attempting recovery", name_);
            if (!attemptRecovery()) {
                flog::error("Recovery failed, cannot start FLEX decoder '{}'", name_);
                return;
            }
        }

        if (isRunning_.load()) {
            flog::debug("FLEX decoder '{}' already running", name_);
            return;
        }

        try {
            if (!dsp_.isInitialized()) {
                flog::error("Cannot start FLEX decoder '{}' - DSP not initialized", name_);
                healthy_.store(false);
                return;
            }

            dsp_.start();
            audioHandler_.start();
            isRunning_.store(true);

            flog::info("FLEX decoder '{}' started successfully", name_);
        }
        catch (const std::exception& e) {
            flog::error("Failed to start FLEX decoder '{}': {}", name_, e.what());
            healthy_.store(false);
            isRunning_.store(false);
        }
    }

    void stop() override {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex_);

        if (!isRunning_.load()) {
            return;
        }

        try {
            audioHandler_.stop();
            dsp_.stop();
            isRunning_.store(false);

            flog::info("FLEX decoder '{}' stopped successfully", name_);
        }
        catch (const std::exception& e) {
            flog::error("Error stopping FLEX decoder '{}': {}", name_, e.what());
            healthy_.store(false);
        }
    }

    // Health monitoring interface
    bool isHealthy() const {
        return healthy_.load() && initialized_.load();
    }

    bool isInitialized() const {
        return initialized_.load();
    }

    // Statistics interface
    size_t getSamplesProcessed() const { return samplesProcessed_.load(); }
    size_t getErrorCount() const { return errorCount_.load(); }
    size_t getMessagesDecoded() const { return messagesDecoded_.load(); }

private:
    // Thread safety
    mutable std::recursive_mutex decoderMutex_;
    std::atomic<bool> initialized_{ false };
    std::atomic<bool> healthy_{ true };
    std::atomic<bool> isRunning_{ false };

    // Performance monitoring
    std::atomic<size_t> samplesProcessed_{ 0 };
    std::atomic<size_t> errorCount_{ 0 };
    std::atomic<size_t> messagesDecoded_{ 0 };
    std::chrono::steady_clock::time_point lastHealthCheck_;

    bool initializeDSP() {
        try {
            dsp_.init(vfo_->output, 24000); // Use fixed sample rate

            if (!dsp_.isInitialized()) {
                flog::error("DSP initialization failed");
                return false;
            }

            flog::info("FLEX DSP initialized: FM demod (±4500 Hz) + AGC + LP filter (5kHz) at {} Hz",
                       dsp_.getAudioSampleRate());
            return true;
        }
        catch (const std::exception& e) {
            flog::error("Exception during DSP initialization: {}", e.what());
            return false;
        }
    }

    bool initFLEXDecoder() {
        try {
            // Initialize new FLEX decoder with sample rate
            flexDecoder_ = std::make_unique<flex_next_decoder::FlexDecoder>(PAGER_AUDIO_SAMPLERATE);

            if (!flexDecoder_) {
                flog::error("Failed to create FLEX decoder");
                return false;
            }

            // Set initial verbosity level
            flexDecoder_->setVerbosityLevel(verbosityLevel_);

            flog::info("FLEX decoder components initialized");
            return true;
        }
        catch (const std::exception& e) {
            flog::error("Failed to initialize FLEX decoder components: {}", e.what());
            return false;
        }
    }

    void showFlexMessageWindow() {
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoFocusOnAppearing;

        static bool first_time = true;
        if (first_time) {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
            first_time = false;
        }

        if (!ImGui::Begin(("FLEX Messages##" + name_).c_str(), &showMessageWindow_, window_flags)) {
            ImGui::End();
            return;
        }

        // Health indicator in message window
        if (healthy_.load()) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Decoder Status: HEALTHY");
        }
        else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Decoder Status: UNHEALTHY");
        }

        // Controls
        if (ImGui::Button("Clear Messages")) {
            clearMessages();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &autoScrollMessages_);
        ImGui::SameLine();
        ImGui::Text("Messages: %lu", messagesDecoded_.load());

        ImGui::Separator();

        // Message display area
        ImGui::BeginChild("MessageArea", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        std::lock_guard<std::mutex> msgLock(messagesMutex_);
        for (const auto& message : messages_) {
            ImGui::TextUnformatted(message.c_str());
        }

        // Auto-scroll to bottom if enabled and there are new messages
        if (autoScrollMessages_ && !messages_.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
        ImGui::End();
    }

    static void audioHandlerCallback(float* data, int count, void* ctx) {
        FLEXDecoder* decoder = static_cast<FLEXDecoder*>(ctx);
        if (decoder && decoder->initialized_.load() && decoder->healthy_.load()) {
            decoder->processAudioSamples(data, count);
        }
    }

    void processAudioSamples(float* samples, int count) {
        if (!initialized_.load() || !healthy_.load() || !samples || count <= 0) {
            return;
        }

        // Update statistics
        samplesProcessed_.fetch_add(count);

        // Periodic health check during processing (inline, no background thread)
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastCheck = std::chrono::duration_cast<std::chrono::seconds>(now - lastHealthCheck_);

        if (timeSinceLastCheck.count() > 30) { // Check every 30 seconds
            performHealthCheck();
            lastHealthCheck_ = now;
        }

        try {
            // Process samples through the new FLEX decoder
            processFlexSamples(samples, count);
        }
        catch (const std::exception& e) {
            flog::error("Error processing FLEX samples in '{}': {}", name_, e.what());
            errorCount_.fetch_add(count); // Count entire chunk as errors
            healthy_.store(false);
        }
    }

    void processFlexSamples(float* samples, int count) {
        if (!initialized_.load() || !flexDecoder_) {
            return;
        }

        try {
            // Use the new decoder's processSamples method
            flexDecoder_->processSamples(samples, count);
        }
        catch (const std::exception& e) {
            errorCount_.fetch_add(count);

            // Only log errors occasionally to avoid spam
            static auto lastErrorLog = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastError = std::chrono::duration_cast<std::chrono::seconds>(now - lastErrorLog);

            if (timeSinceLastError.count() > 60) { // Log errors max once per minute
                flog::error("Error in FLEX sample processing for '{}': {}", name_, e.what());
                lastErrorLog = now;
            }

            // Mark as unhealthy if too many errors
            size_t totalSamples = samplesProcessed_.load();
            size_t totalErrors = errorCount_.load();
            if (totalSamples > 1000 && (double)totalErrors / (double)totalSamples > 0.1) { // 10% error rate
                healthy_.store(false);
            }
        }
    }

    void handleFlexMessage(int64_t address, int type, const std::string& data) {
        try {
            // Input validation
            if (data.length() > 1000) {
                flog::warn("FLEX message too long, truncating (decoder: '{}')", name_);
                return;
            }

            if (address < 0) {
                flog::warn("Invalid FLEX address: {} (decoder: '{}')", address, name_);
                return;
            }

            // Update statistics
            messagesDecoded_.fetch_add(1);

            // Safe message handling
            std::string safeData = data;
            // Remove any non-printable characters
            safeData.erase(std::remove_if(safeData.begin(), safeData.end(),
                                          [](char c) { return !std::isprint(static_cast<unsigned char>(c)); }),
                           safeData.end());

            // Store message for display using stringstream instead of fmt::format
            {
                std::lock_guard<std::mutex> lock(messagesMutex_);
                std::stringstream ss;
                ss << "FLEX[" << name_ << "]: Addr=" << address << ", Type=" << type << ", Data=" << safeData;
                messages_.emplace_back(ss.str());

                // Limit message history
                if (messages_.size() > MAX_MESSAGES) {
                    messages_.erase(messages_.begin());
                }
            }

            // Console output for testing
            printf("FLEX[%s]: Addr=%ld Type=%d Data=%s\n", name_.c_str(), address, type, safeData.c_str());

            // Also use flog for SDR++ logging
            flog::info("FLEX[{}] Message - Addr: {}, Type: {}, Data: {}", name_, address, type, safeData);
        }
        catch (const std::exception& e) {
            flog::error("Error handling FLEX message in '{}': {}", name_, e.what());
            errorCount_.fetch_add(1);
        }
    }

    void resetDecoder() {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex_);

        if (!initialized_.load()) {
            return;
        }

        try {
            flog::info("Resetting FLEX decoder '{}'", name_);

            bool wasRunning = isRunning_.load();
            if (wasRunning) {
                stop();
            }

            if (flexDecoder_) {
                flexDecoder_->reset();
            }

            // Clear statistics
            clearStatistics();

            // Mark as healthy after reset
            healthy_.store(true);

            if (wasRunning) {
                start();
            }

            flog::info("FLEX decoder '{}' reset successfully", name_);
        }
        catch (const std::exception& e) {
            flog::error("Error resetting FLEX decoder '{}': {}", name_, e.what());
            healthy_.store(false);
        }
    }

    void clearStatistics() {
        samplesProcessed_.store(0);
        errorCount_.store(0);
        messagesDecoded_.store(0);
        flog::debug("Statistics cleared for FLEX decoder '{}'", name_);
    }

    void clearMessages() {
        std::lock_guard<std::mutex> lock(messagesMutex_);
        messages_.clear();
    }

    bool attemptRecovery() {
        std::lock_guard<std::recursive_mutex> lock(decoderMutex_);

        flog::info("Attempting recovery for FLEX decoder '{}'", name_);

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
            healthy_.store(true);

            flog::info("Recovery successful for FLEX decoder '{}'", name_);
            return true;
        }
        catch (const std::exception& e) {
            flog::error("Recovery failed for FLEX decoder '{}': {}", name_, e.what());
            healthy_.store(false);
            return false;
        }
    }

    void performHealthCheck() {
        if (!initialized_.load()) {
            return;
        }

        try {
            // Check DSP health
            if (!dsp_.isInitialized()) {
                flog::warn("DSP not initialized in FLEX decoder '{}'", name_);
                healthy_.store(false);
                return;
            }

            // Check error rate
            size_t totalSamples = samplesProcessed_.load();
            size_t totalErrors = errorCount_.load();

            if (totalSamples > 10000) { // Only check after processing significant samples
                double errorRate = (double)totalErrors / (double)totalSamples;

                if (errorRate > 0.15) { // 15% error rate threshold
                    flog::warn("High error rate in FLEX decoder '{}': {:.2f}%", name_, errorRate * 100.0);
                    healthy_.store(false);
                }
            }

            // Check if decoder components are still valid
            if (!flexDecoder_) {
                flog::error("FLEX decoder components invalid in '{}'", name_);
                healthy_.store(false);
            }
        }
        catch (const std::exception& e) {
            flog::error("Health check exception for FLEX decoder '{}': {}", name_, e.what());
            healthy_.store(false);
        }
    }

    void cleanup() {
        try {
            if (flexDecoder_) {
                flexDecoder_.reset();
            }
        }
        catch (const std::exception& e) {
            flog::error("Exception during cleanup: {}", e.what());
        }
    }

    std::string stateToString(flex_next_decoder::FlexState state) {
        using namespace flex_next_decoder;
        switch (state) {
        case FlexState::Sync1:
            return "SYNC1";
        case FlexState::FIW:
            return "FIW";
        case FlexState::Sync2:
            return "SYNC2";
        case FlexState::Data:
            return "DATA";
        default:
            return "UNKNOWN";
        }
    }

    // Core components
    std::string name_;
    VFOManager::VFO* vfo_;

    FLEXDSP dsp_;
    dsp::sink::Handler<float> audioHandler_;

    // New decoder components
    std::unique_ptr<flex_next_decoder::FlexDecoder> flexDecoder_;

    // UI state
    bool showRawData_ = false;
    bool showErrors_ = false;
    bool showMessageWindow_ = false;
    bool autoScrollMessages_ = true;
    int verbosityLevel_ = 1;

    // Message storage
    std::mutex messagesMutex_;
    std::vector<std::string> messages_;
    static constexpr size_t MAX_MESSAGES = 100;
};
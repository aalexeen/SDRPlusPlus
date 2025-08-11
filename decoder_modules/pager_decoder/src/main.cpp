
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <signal_path/vfo_manager.h>
#include <core.h>
#include <config.h>
#include <utils/optionlist.h>
#include <utils/flog.h>
#include <memory>
#include <mutex>

// Include decoder implementations
#include "decoder.h"
#include "pocsag/decoder.h"
#include "flex/decoder.h"

SDRPP_MOD_INFO{
    /* Name:            */ "pager_decoder",
    /* Description:     */ "Pager (POCSAG/FLEX) decoder module with modern C++ architecture",
    /* Author:          */ "SDR++",
    /* Version:         */ 1, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

class PagerDecoderModule : public ModuleManager::Instance {
public:
    PagerDecoderModule(std::string name) : name_(std::move(name)) {
        // Initialize decoder types with modern enum class approach
        decoderTypes_.define("POCSAG", DecoderType::POCSAG);
        decoderTypes_.define("FLEX", DecoderType::FLEX);

        // Load config
        config.acquire();
        if (!config.conf.contains(name_)) {
            config.conf[name_]["selectedDecoder"] = static_cast<int>(DecoderType::FLEX); // Default to FLEX
            config.conf[name_]["enabled"] = false;
        }
        selectedDecoderId_ = static_cast<DecoderType>(config.conf[name_]["selectedDecoder"].get<int>());
        enabled_ = config.conf[name_]["enabled"];
        config.release(true);

        // Register the menu for this instance
        gui::menu.registerEntry(name_, menuHandler, this, this);

        flog::info("Pager decoder module '{}' initialized", name_);
    }

    ~PagerDecoderModule() {
        try {
            disable();
            gui::menu.removeEntry(name_);
            flog::info("Pager decoder module '{}' destroyed", name_);
        }
        catch (const std::exception& e) {
            flog::error("Exception in PagerDecoderModule destructor: {}", e.what());
        }
    }

    void postInit() override {
        // Module fully initialized - can access other modules now
        flog::info("Pager decoder module '{}' post-initialization complete", name_);
    }

    void enable() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (enabled_) return;

        try {
            // Create VFO with proper initialization (similar to radio module)
            vfo_ = sigpath::vfoManager.createVFO(name_, ImGui::WaterfallVFO::REF_CENTER, 0, 12500, 24000, 12500, 12500, true);

            if (!vfo_) {
                flog::error("Failed to create VFO for pager decoder '{}'", name_);
                return;
            }

            // Set the VFO slider step (snap) in Hz for pager frequencies
            vfo_->setSnapInterval(1000.0); // 1 kHz steps

            // Configure VFO properly for pager frequencies
            vfo_->setBandwidthLimits(12500, 12500, true);
            vfo_->setSampleRate(PAGER_AUDIO_SAMPLERATE, 12500);

            // Create appropriate decoder
            createDecoder();

            enabled_ = true;

            // Save state
            config.acquire();
            config.conf[name_]["enabled"] = enabled_;
            config.release(true);

            flog::info("Pager decoder '{}' enabled with VFO waterfall", name_);
        }
        catch (const std::exception& e) {
            flog::error("Failed to enable pager decoder '{}': {}", name_, e.what());
            cleanup();
        }
    }

    void disable() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!enabled_) return;

        try {
            cleanup();
            enabled_ = false;

            // Save state
            config.acquire();
            config.conf[name_]["enabled"] = enabled_;
            config.release(true);

            flog::info("Pager decoder '{}' disabled", name_);
        }
        catch (const std::exception& e) {
            flog::error("Error disabling pager decoder '{}': {}", name_, e.what());
        }
    }

    bool isEnabled() override {
        return enabled_;
    }

    // Static menu handler
    static void menuHandler(void* ctx) {
        PagerDecoderModule* _this = static_cast<PagerDecoderModule*>(ctx);
        _this->menuHandlerImpl();
    }

private:
    enum class DecoderType : int {
        POCSAG = 0,
        FLEX = 1
    };

    void menuHandlerImpl() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!enabled_) {
            if (ImGui::Button(("Enable##" + name_).c_str())) {
                enable();
            }
            return;
        }

        // Main decoder controls
        ImGui::Text("Pager Decoder");

        // Decoder selection
        ImGui::Text("Protocol:");
        int currentType = static_cast<int>(selectedDecoderId_);
        if (ImGui::Combo(("##decoder_type_" + name_).c_str(), &currentType, decoderTypes_.txt)) {
            selectedDecoderId_ = static_cast<DecoderType>(currentType);
            createDecoder();

            // Save selection
            config.acquire();
            config.conf[name_]["selectedDecoder"] = currentType;
            config.release(true);

            const std::string& typeKey = decoderTypes_.key(currentType);
            flog::info("Switched to {} decoder", typeKey);
        }

        ImGui::SameLine();
        if (ImGui::Button(("Disable##" + name_).c_str())) {
            disable();
            return;
        }

        // Show decoder-specific menu if available
        if (decoder_) {
            ImGui::Separator();

            // Display decoder status
            const std::string& decoderName = decoderTypes_.key(static_cast<int>(selectedDecoderId_));
            ImGui::Text("Active Decoder: %s", decoderName.c_str());

            // Show decoder's custom UI
            try {
                decoder_->showMenu();
            }
            catch (const std::exception& e) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Decoder Error: %s", e.what());
                flog::error("Decoder menu error in '{}': {}", name_, e.what());
            }
        }
        else {
            ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Decoder: Not initialized");
        }
    }

    void createDecoder() {
        // Clean up existing decoder
        if (decoder_) {
            decoder_->stop();
            decoder_.reset();
        }

        if (!vfo_) {
            flog::error("Cannot create decoder without VFO for '{}'", name_);
            return;
        }

        try {
            // Create decoder based on selected type
            switch (selectedDecoderId_) {
            case DecoderType::POCSAG:
                decoder_ = std::make_unique<POCSAGDecoder>(name_ + "_POCSAG", vfo_);
                flog::info("Created POCSAG decoder for '{}'", name_);
                break;
            case DecoderType::FLEX:
                decoder_ = std::make_unique<FLEXDecoder>(name_ + "_FLEX", vfo_);
                flog::info("Created FLEX decoder for '{}'", name_);
                break;
            default:
                flog::error("Unknown decoder type: {} for '{}'", static_cast<int>(selectedDecoderId_), name_);
                return;
            }

            // Start the decoder if module is enabled
            if (decoder_ && enabled_) {
                decoder_->start();
                const std::string& decoderName = decoderTypes_.key(static_cast<int>(selectedDecoderId_));
                flog::info("Started {} decoder for '{}'", decoderName, name_);
            }
        }
        catch (const std::exception& e) {
            flog::error("Failed to create decoder for '{}': {}", name_, e.what());
            decoder_.reset();
        }
    }

    void cleanup() {
        // Stop and cleanup decoder
        if (decoder_) {
            try {
                decoder_->stop();
            }
            catch (const std::exception& e) {
                flog::error("Error stopping decoder for '{}': {}", name_, e.what());
            }
            decoder_.reset();
        }

        // Cleanup VFO
        if (vfo_) {
            try {
                sigpath::vfoManager.deleteVFO(vfo_);
            }
            catch (const std::exception& e) {
                flog::error("Error deleting VFO for '{}': {}", name_, e.what());
            }
            vfo_ = nullptr;
        }
    }

    // Member variables
    std::string name_;
    bool enabled_ = false;
    std::mutex mutex_; // Thread safety

    VFOManager::VFO* vfo_ = nullptr;
    std::unique_ptr<Decoder> decoder_;

    DecoderType selectedDecoderId_ = DecoderType::FLEX;
    OptionList<std::string, DecoderType> decoderTypes_;
};

MOD_EXPORT void _INIT_() {
    // Initialize config system
    json def = json({});
    config.setPath(core::args["root"].s() + "/pager_decoder_config.json");
    config.load(def);
    config.enableAutoSave();

    flog::info("Pager decoder module initialized");
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new PagerDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete static_cast<PagerDecoderModule*>(instance);
}

MOD_EXPORT void _END_() {
    try {
        config.disableAutoSave();
        config.save();
        flog::info("Pager decoder module cleanup complete");
    }
    catch (const std::exception& e) {
        flog::error("Error during pager decoder module cleanup: {}", e.what());
    }
}
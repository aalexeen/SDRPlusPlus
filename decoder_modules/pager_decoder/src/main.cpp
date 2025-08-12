#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/vfo_manager.h>
#include <core.h>
#include <utils/optionlist.h>
#include <utils/flog.h>
#include "decoder.h"
#include "flex/decoder_next.h"
#include "pocsag/decoder.h"


SDRPP_MOD_INFO{
    /* Name:            */ "pager_decoder",
    /* Description:     */ "Pager (POCSAG/FLEX) decoder module",
    /* Author:          */ "SDR++",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ -1
};

ConfigManager config;

class PagerDecoderModule : public ModuleManager::Instance {
public:
    PagerDecoderModule(std::string name) : name(name) {
        // Initialize decoder types
        decoderTypes.define("POCSAG", DECODER_POCSAG);
        decoderTypes.define("FLEX", DECODER_FLEX);

        // Load config
        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name]["selectedDecoder"] = DECODER_POCSAG;
            config.conf[name]["enabled"] = false;
        }
        selectedDecoderId = config.conf[name]["selectedDecoder"];
        enabled = config.conf[name]["enabled"];
        config.release(true);

        // Register the menu for this instance
        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~PagerDecoderModule() {
        disable();
        gui::menu.removeEntry(name);
    }

    void postInit() {}

    void enable() {
        if (enabled) return;

        // Create VFO with proper initialization (similar to radio module)
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 12500, 24000, 12500, 12500, true);

        if (!vfo) {
            flog::error("Failed to create VFO for pager decoder");
            return;
        }

        // Set the VFO slider step (snap) in Hz, e.g., 1000 Hz
        vfo->setSnapInterval(/* step in Hz, e.g. */ 1000.0);

        // Configure VFO properly for pager frequencies
        vfo->setBandwidthLimits(12500, 12500, true);
        vfo->setSampleRate(24000, 12500);

        // Create appropriate decoder
        createDecoder();

        enabled = true;

        // Save state
        config.acquire();
        config.conf[name]["enabled"] = enabled;
        config.release(true);

        flog::info("Pager decoder enabled with VFO waterfall");
    }

    void disable() {
        if (!enabled) return;

        if (decoder) {
            decoder->stop();
            delete decoder;
            decoder = nullptr;
        }

        if (vfo) {
            sigpath::vfoManager.deleteVFO(vfo);
            vfo = nullptr;
        }

        enabled = false;

        // Save state
        config.acquire();
        config.conf[name]["enabled"] = enabled;
        config.release(true);

        flog::info("Pager decoder disabled");
    }

    bool isEnabled() {
        return enabled;
    }

    // Static menu handler - this is what was missing!
    static void menuHandler(void* ctx) {
        PagerDecoderModule* _this = (PagerDecoderModule*)ctx;

        if (!_this->enabled) {
            if (ImGui::Button("Enable")) {
                _this->enable();
            }
            return;
        }

        ImGui::Text("Pager Decoder");

        // Decoder selection
        ImGui::Text("Protocol:");
        if (ImGui::Combo("##decoder_type", &_this->selectedDecoderId, _this->decoderTypes.txt)) {
            _this->createDecoder();
            config.acquire();
            config.conf[_this->name]["selectedDecoder"] = _this->selectedDecoderId;
            config.release(true);
        }

        if (ImGui::Button("Disable")) {
            _this->disable();
        }

        // Show decoder-specific menu if available
        if (_this->decoder) {
            ImGui::Separator();
            _this->decoder->showMenu();
        }
    }

private:
    enum DecoderType {
        DECODER_POCSAG,
        DECODER_FLEX
    };

    void createDecoder() {
        if (decoder) {
            decoder->stop();
            delete decoder;
        }

        if (!vfo) {
            flog::error("Cannot create decoder without VFO");
            return;
        }

        switch (selectedDecoderId) {
        case DECODER_POCSAG:
            decoder = new POCSAGDecoder(name, vfo);
            break;
        case DECODER_FLEX:
            decoder = new FLEXDecoderNext(name, vfo);
            break;
        default:
            flog::error("Unknown decoder type: {}", selectedDecoderId);
            return;
        }

        if (decoder && enabled) {
            decoder->start();
            flog::info("Created and started {} decoder", decoderTypes.key(selectedDecoderId));
        }
    }

    std::string name;
    bool enabled = false;

    VFOManager::VFO* vfo = nullptr;
    Decoder* decoder = nullptr;

    int selectedDecoderId = DECODER_POCSAG;
    OptionList<std::string, DecoderType> decoderTypes;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/pager_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new PagerDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (PagerDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
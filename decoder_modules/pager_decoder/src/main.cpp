#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <gui/widgets/folder_select.h>
#include <utils/optionlist.h>
#include "decoder.h"
#include "pocsag/decoder.h"  // Your existing POCSAG decoder
#include "flex/decoder.h"    // FLEX decoder

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "pager_decoder",
    /* Description:     */ "POCSAG and Flex Pager Decoder"
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

enum Protocol {
    PROTOCOL_INVALID = -1,
    PROTOCOL_POCSAG,
    PROTOCOL_FLEX
};

class PagerDecoderModule : public ModuleManager::Instance {
public:
    PagerDecoderModule(std::string name) {
        this->name = name;

        // Define protocols
        protocols.define("POCSAG", PROTOCOL_POCSAG);
        protocols.define("FLEX", PROTOCOL_FLEX);

        // Initialize VFO with default values
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 12500, 24000, 12500, 12500, true);
        vfo->setSnapInterval(1);

        // Create initial decoder but don't start it yet
        decoder = std::make_unique<POCSAGDecoder>(name, vfo);
        proto = PROTOCOL_POCSAG;

        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~PagerDecoderModule() {
        gui::menu.removeEntry(name);
        // Stop DSP
        if (enabled && decoder) {
            decoder->stop();
            decoder.reset();
        }
        if (vfo) {
            sigpath::vfoManager.deleteVFO(vfo);
        }

        sigpath::sinkManager.unregisterStream(name);
    }

    void postInit() {}

    void enable() {
        if (enabled) return;

        double bw = gui::waterfall.getBandwidth();
        if (!vfo) {
            vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, std::clamp<double>(0, -bw / 2.0, bw / 2.0), 12500, 24000, 12500, 12500, true);
            vfo->setSnapInterval(1);
        }

        if (decoder) {
            decoder->setVFO(vfo);
            decoder->start();
        }

        enabled = true;
    }

    void disable() {
        if (!enabled) return;

        if (decoder) {
            decoder->stop();
        }
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void selectProtocol(Protocol newProto) {
        // If the protocol hasn't changed, no need to do anything
        if (newProto == proto) { return; }

        // Stop current decoder if running
        bool wasRunning = enabled;
        if (wasRunning && decoder) {
            decoder->stop();
        }

        // Delete current decoder
        decoder.reset();

        // Create a new decoder
        switch (newProto) {
        case PROTOCOL_POCSAG:
            decoder = std::make_unique<POCSAGDecoder>(name, vfo);
            break;
        case PROTOCOL_FLEX:
            decoder = std::make_unique<FLEXDecoder>(name, vfo);
            break;
        default:
            flog::error("Tried to select unknown pager protocol");
            return;
        }

        // Start the new decoder if we were running before
        if (wasRunning && decoder) {
            decoder->setVFO(vfo);
            decoder->start();
        }

        // Save selected protocol
        proto = newProto;
    }

private:
    static void menuHandler(void* ctx) {
        PagerDecoderModule* _this = (PagerDecoderModule*)ctx;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (!_this->enabled) { style::beginDisabled(); }

        ImGui::LeftLabel("Protocol");
        ImGui::FillWidth();
        if (ImGui::Combo(("##pager_decoder_proto_" + _this->name).c_str(), &_this->protoId, _this->protocols.txt)) {
            _this->selectProtocol(_this->protocols.value(_this->protoId));
        }

        if (_this->decoder) { _this->decoder->showMenu(); }

        ImGui::Button(("Record##pager_decoder_show_" + _this->name).c_str(), ImVec2(menuWidth, 0));
        ImGui::Button(("Show Messages##pager_decoder_show_" + _this->name).c_str(), ImVec2(menuWidth, 0));

        if (!_this->enabled) { style::endDisabled(); }
    }

    std::string name;
    bool enabled = true;

    Protocol proto = PROTOCOL_INVALID;
    int protoId = 0;

    OptionList<std::string, Protocol> protocols;

    // DSP Chain
    VFOManager::VFO* vfo;
    std::unique_ptr<Decoder> decoder;

    bool showLines = false;
};

MOD_EXPORT void _INIT_() {
    // Create default recording directory
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
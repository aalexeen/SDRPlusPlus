#pragma once
#include "../decoder.h"  // Base decoder class
#include <signal_path/vfo_manager.h>
#include <utils/optionlist.h>
#include <gui/widgets/symbol_diagram.h>
#include <gui/style.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/buffer/reshaper.h>
#include <utils/flog.h>
#include <imgui.h>
#include "dsp.h"      // POCSAG DSP (in same directory)
#include "pocsag.h"   // POCSAG protocol (in same directory)

#define BAUDRATE    2400
#define SAMPLERATE  (BAUDRATE*10)

class POCSAGDecoder : public Decoder {
public:
    POCSAGDecoder(const std::string& name, VFOManager::VFO* vfo) : diag(0.6, BAUDRATE) {
        this->name = name;
        this->vfo = vfo;

        // Define baudrate options
        baudrates.define(512, "512 Baud", 512);
        baudrates.define(1200, "1200 Baud", 1200);
        baudrates.define(2400, "2400 Baud", 2400);

        // Init DSP
        vfo->setBandwidthLimits(12500, 12500, true);
        vfo->setSampleRate(SAMPLERATE, 12500);
        dsp.init(vfo->output, SAMPLERATE, BAUDRATE);
        reshape.init(&dsp.soft, BAUDRATE, (BAUDRATE / 30.0) - BAUDRATE);
        dataHandler.init(&dsp.out, _dataHandler, this);
        diagHandler.init(&reshape.out, _diagHandler, this);

        // Init decoder
        decoder.onMessage.bind(&POCSAGDecoder::messageHandler, this);
    }

    ~POCSAGDecoder() {
        stop();
    }

    void showMenu() override {
        ImGui::LeftLabel("Baudrate");
        ImGui::FillWidth();
        if (ImGui::Combo(("##pager_decoder_pocsag_br_" + name).c_str(), &brId, baudrates.txt)) {
            // TODO: Update baudrate
        }

        ImGui::FillWidth();
        diag.draw();
    }

    void setVFO(VFOManager::VFO* vfo) override {
        this->vfo = vfo;
        vfo->setBandwidthLimits(12500, 12500, true);
        vfo->setSampleRate(SAMPLERATE, 12500);
        dsp.setInput(vfo->output);
    }

    void start() override {
        dsp.start();
        reshape.start();
        dataHandler.start();
        diagHandler.start();
    }

    void stop() override {
        dsp.stop();
        reshape.stop();
        dataHandler.stop();
        diagHandler.stop();
    }

private:
    static void _dataHandler(uint8_t* data, int count, void* ctx) {
        POCSAGDecoder* _this = (POCSAGDecoder*)ctx;
        _this->decoder.process(data, count);
    }

    static void _diagHandler(float* data, int count, void* ctx) {
        POCSAGDecoder* _this = (POCSAGDecoder*)ctx;
        float* buf = _this->diag.acquireBuffer();
        memcpy(buf, data, count * sizeof(float));
        _this->diag.releaseBuffer();
    }

    void messageHandler(pocsag::Address addr, pocsag::MessageType type, const std::string& msg) {
        flog::debug("[{}]: '{}'", (uint32_t)addr, msg);
    }

    std::string name;
    VFOManager::VFO* vfo;

    POCSAGDSP dsp;
    dsp::buffer::Reshaper<float> reshape;
    dsp::sink::Handler<uint8_t> dataHandler;
    dsp::sink::Handler<float> diagHandler;

    pocsag::Decoder decoder;

    ImGui::SymbolDiagram diag;

    int brId = 2;

    OptionList<int, int> baudrates;

    int verbosity_level_;       ///< Debug output level
};
#pragma once
#include "../decoder.h"
#include <signal_path/vfo_manager.h>
#include <utils/optionlist.h>
#include <gui/widgets/symbol_diagram.h>
#include <gui/style.h>
#include <dsp/sink/handler_sink.h>
#include "dsp.h"
#include "flex.h"

#define BAUDRATE    1600
#define SAMPLERATE  (BAUDRATE*10)

class FLEXDecoder : public Decoder {
public:
    FLEXDecoder(const std::string& name, VFOManager::VFO* vfo) : diag(0.6, BAUDRATE) {
        this->name = name;
        this->vfo = vfo;

        // Define baudrate options
        baudrates.define(1600, "1600 Baud", 1600);
        baudrates.define(3200, "3200 Baud", 3200);
        baudrates.define(6400, "6400 Baud", 6400);

        // Init DSP
        vfo->setBandwidthLimits(12500, 12500, true);
        vfo->setSampleRate(SAMPLERATE, 12500);
        dsp.init(vfo->output, SAMPLERATE, BAUDRATE);
        reshape.init(&dsp.soft, BAUDRATE, (BAUDRATE / 30.0) - BAUDRATE);
        dataHandler.init(&dsp.out, _dataHandler, this);
        diagHandler.init(&reshape.out, _diagHandler, this);

        // Init decoder
        decoder.onMessage.bind(&FLEXDecoder::messageHandler, this);
    }

    ~FLEXDecoder() {
        stop();
    }

    void showMenu() {
        ImGui::LeftLabel("Baudrate");
        ImGui::FillWidth();
        if (ImGui::Combo(("##pager_decoder_flex_br_" + name).c_str(), &brId, baudrates.txt)) {
            // TODO: Implement baudrate change
        }

        ImGui::FillWidth();
        diag.draw();
    }

    void setVFO(VFOManager::VFO* vfo) {
        this->vfo = vfo;
        vfo->setBandwidthLimits(12500, 12500, true);
        vfo->setSampleRate(SAMPLERATE, 12500);
        dsp.setInput(vfo->output);
    }

    void start() {
        dsp.start();
        reshape.start();
        dataHandler.start();
        diagHandler.start();
    }

    void stop() {
        dsp.stop();
        reshape.stop();
        dataHandler.stop();
        diagHandler.stop();
    }

private:
    static void _dataHandler(uint8_t* data, int count, void* ctx) {
        FLEXDecoder* _this = (FLEXDecoder*)ctx;
        _this->decoder.process(data, count);
    }

    static void _diagHandler(float* data, int count, void* ctx) {
        FLEXDecoder* _this = (FLEXDecoder*)ctx;
        float* buf = _this->diag.acquireBuffer();
        memcpy(buf, data, count * sizeof(float));
        _this->diag.releaseBuffer();
    }

    void messageHandler(flex::Address addr, flex::MessageType type, const std::string& msg) {
        const char* typeStr = "UNK";
        switch (type) {
            case flex::MESSAGE_TYPE_ALPHANUMERIC: typeStr = "ALN"; break;
            case flex::MESSAGE_TYPE_STANDARD_NUMERIC: typeStr = "NUM"; break;
            case flex::MESSAGE_TYPE_TONE: typeStr = "TON"; break;
            case flex::MESSAGE_TYPE_BINARY: typeStr = "BIN"; break;
            default: break;
        }
        flog::debug("FLEX [{}] {}: '{}'", (uint64_t)addr, typeStr, msg);
    }

    std::string name;
    VFOManager::VFO* vfo;

    FLEXDSP dsp;
    dsp::buffer::Reshaper<float> reshape;
    dsp::sink::Handler<uint8_t> dataHandler;
    dsp::sink::Handler<float> diagHandler;

    flex::Decoder decoder;

    ImGui::SymbolDiagram diag;

    int brId = 0;

    OptionList<int, int> baudrates;
};
#pragma once

#include <memory>
#include <array>
#include "FlexTypes.h"

namespace flex_next_decoder {

    class FlexDemodulator;
    class FlexStateMachine;
    class FlexSynchronizer;
    class FlexFrameProcessor;
    class FlexDataCollector;
    class FlexErrorCorrector;
    class FlexMessageDecoder;
    class FlexGroupHandler;
    class FlexOutputFormatter;

    class FlexDecoder {
    public:
        explicit FlexDecoder(uint32_t sample_frequency = 22050);
        ~FlexDecoder();

        FlexDecoder(const FlexDecoder&) = delete;
        FlexDecoder& operator=(const FlexDecoder&) = delete;
        FlexDecoder(FlexDecoder&&) = default;
        FlexDecoder& operator=(FlexDecoder&&) = default;

        void processSamples(const float* samples, size_t count);
        void reset();

        // Configuration
        void setVerbosityLevel(int level);

    private:
        std::unique_ptr<FlexDemodulator> demodulator_;
        std::unique_ptr<FlexStateMachine> state_machine_;
        std::unique_ptr<FlexSynchronizer> synchronizer_;
        std::unique_ptr<FlexFrameProcessor> frame_processor_;
        std::unique_ptr<FlexDataCollector> data_collector_;
        std::unique_ptr<FlexErrorCorrector> error_corrector_;
        std::unique_ptr<FlexMessageDecoder> message_decoder_;
        std::unique_ptr<FlexGroupHandler> group_handler_;
        std::unique_ptr<FlexOutputFormatter> output_formatter_;

        uint32_t sample_frequency_;
    };

} // namespace flex_next_decoder
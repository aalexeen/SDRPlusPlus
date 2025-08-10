#pragma once

#include "FlexTypes.h"

namespace flex_next_decoder {

    class FlexErrorCorrector;
    class FlexGroupHandler;

    class FlexFrameProcessor {
    public:
        explicit FlexFrameProcessor(FlexErrorCorrector& error_corrector,
                                   FlexGroupHandler& group_handler);
        ~FlexFrameProcessor() = default;

        bool processRawFIW(uint32_t raw_fiw, FrameInfo& frame_info);
        void reset();

        void read2FSK(uint8_t symbol, uint32_t& data);

    private:
        bool validateChecksum(const FrameInfo& frame_info) const;
        void processGroupFrames(const FrameInfo& frame_info);

        FlexErrorCorrector& error_corrector_;
        FlexGroupHandler& group_handler_;
    };

} // namespace flex_next_decoder
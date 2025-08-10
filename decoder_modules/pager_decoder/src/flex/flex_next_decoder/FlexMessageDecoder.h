#pragma once

#include "FlexTypes.h"
#include "parsers/IMessageParser.h"

#include <memory>

namespace flex_next_decoder {

    class FlexDataCollector;
    class FlexErrorCorrector;
    class FlexGroupHandler;
    class FlexOutputFormatter;
    class IMessageParser;

    class FlexMessageDecoder {
    public:
        FlexMessageDecoder(FlexErrorCorrector& error_corrector,
                          FlexGroupHandler& group_handler,
                          FlexOutputFormatter& output_formatter);
        ~FlexMessageDecoder();

        void decodeData(FlexDataCollector& data_collector,
                       const SyncInfo& sync_info,
                       const FrameInfo& frame_info);

    private:
        void decodePhase(char phase_id, uint32_t* phase_data,
                        const SyncInfo& sync_info,
                        const FrameInfo& frame_info);

        FlexErrorCorrector& error_corrector_;
        FlexGroupHandler& group_handler_;
        FlexOutputFormatter& output_formatter_;

        std::unique_ptr<IMessageParser> alpha_parser_;
        std::unique_ptr<IMessageParser> numeric_parser_;
        std::unique_ptr<IMessageParser> tone_parser_;
        std::unique_ptr<IMessageParser> binary_parser_;
    };

} // namespace flex_next_decoder
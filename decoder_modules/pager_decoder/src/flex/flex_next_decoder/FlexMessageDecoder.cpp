#include "FlexMessageDecoder.h"
#include <algorithm>
#include <iostream>

namespace flex_next_decoder {

    //=============================================================================
    // FlexMessageDecoder Implementation
    //=============================================================================

    FlexMessageDecoder::FlexMessageDecoder(std::shared_ptr<FlexGroupHandler> group_handler,
                                           std::unique_ptr<FlexOutputFormatter> output_formatter) :
        group_handler_(std::move(group_handler)), output_formatter_(std::move(output_formatter)) {
        initializeParsers();

        // Set default options
        options_.enable_group_processing = true;
        options_.enable_fragment_assembly = true;
        options_.validate_capcodes = true;
        options_.enable_statistics = true;
        options_.max_message_length = MAX_ALN_LENGTH;
    }

    FlexMessageDecoder::FlexMessageDecoder(std::shared_ptr<FlexGroupHandler> group_handler,
                                           std::unique_ptr<FlexOutputFormatter> output_formatter, int verbosity_level) :
        FlexNextDecoder(verbosity_level), output_formatter_(std::move(output_formatter)),
        group_handler_(std::move(group_handler)) {
        initializeParsers();

        // Set default options
        options_.enable_group_processing = true;
        options_.enable_fragment_assembly = true;
        options_.validate_capcodes = true;
        options_.enable_statistics = true;
        options_.max_message_length = MAX_ALN_LENGTH;
    }

    MessageParseResult FlexMessageDecoder::parseMessage(const MessageParseInput &input) {
        return parseMessage(input, options_);
    }

    MessageParseResult FlexMessageDecoder::parseMessage(const MessageParseInput &input,
                                                        const DecodingOptions &options) {
        MessageParseResult result;

        try {
            // Find appropriate parser for message type
            auto parser = getParserForType(input.type);
            if (parser != nullptr) {
                result = parser->parseMessage(input);
            } else {
                // Fallback to binary parser for unknown types
                result = binary_parser_->parseAsDefault(input);
            }

            // Validate message if enabled
            if (options.validate_capcodes && result.success) {
                if (!validateMessage(result)) {
                    result.success = false;
                    if (result.error_message.empty()) { result.error_message = "Message validation failed"; }
                }
            }

            // Process group message if applicable
            if (options.enable_group_processing && result.success && !result.group_data.isEmpty()) {
                processGroupMessage(result);
            }

            // Handle fragment assembly if enabled
            if (options.enable_fragment_assembly && result.success) { processFragment(result); }

            // Post-process message content
            if (result.success) { postProcessMessage(result); }

            // Update statistics if enabled
            if (options.enable_statistics && statistics_enabled_) { updateStatistics(input, result); }

            // Invoke callbacks
            invokeCallbacks(result);
        } catch (const std::exception &e) {
            result.success = false;
            result.error_message = "Exception during message parsing: " + std::string(e.what());

            if (options.error_callback) { options.error_callback(result.error_message); }
        }

        outputFormattedMessage(result, input);

        return result;
    }

    void FlexMessageDecoder::setGroupHandler(std::shared_ptr<FlexGroupHandler> group_handler) {
        group_handler_ = std::move(group_handler);
    }

    void FlexMessageDecoder::setDecodingOptions(const DecodingOptions &options) { options_ = options; }

    const DecodingOptions &FlexMessageDecoder::getDecodingOptions() const { return options_; }

    bool FlexMessageDecoder::processFragment(MessageParseResult &result) {
        if (result.fragment_flag == FragmentFlag::Complete || result.fragment_flag == FragmentFlag::Unknown) {
            return false; // No fragment processing needed
        }

        int64_t capcode = 0; // We'd need to get this from the input context

        // For now, we'll use a simple approach - in a real implementation,
        // we'd need the capcode from the MessageParseInput
        FragmentBuffer &buffer = fragment_buffers_[capcode];

        if (result.fragment_flag == FragmentFlag::Fragment) {
            // Start or continue a fragment sequence
            if (buffer.fragment_count == 0) { buffer.capcode = capcode; }
            buffer.addFragment(result.content, result.fragment_flag);

            // Don't return the partial content yet
            result.content = "[Fragment " + std::to_string(buffer.fragment_count) + " buffered]";
            return true;
        } else if (result.fragment_flag == FragmentFlag::Continuation) {
            // Complete the fragment sequence
            if (buffer.fragment_count > 0 && buffer.capcode == capcode) {
                buffer.addFragment(result.content, result.fragment_flag);

                // Replace result content with assembled message
                result.content = buffer.assembled_content;
                result.fragment_flag = FragmentFlag::Complete;

                // Clear the buffer
                buffer.reset();
                fragment_buffers_.erase(capcode);

                return true;
            } else {
                // Orphaned continuation - treat as standalone
                result.error_message += " (Orphaned continuation fragment)";
            }
        }

        return false;
    }

    void FlexMessageDecoder::clearFragmentBuffers() { fragment_buffers_.clear(); }

    size_t FlexMessageDecoder::getPendingFragmentCount() const { return fragment_buffers_.size(); }

    const MessageStatistics &FlexMessageDecoder::getStatistics() const { return statistics_; }

    void FlexMessageDecoder::resetStatistics() { statistics_.reset(); }

    void FlexMessageDecoder::setStatisticsEnabled(bool enabled) { statistics_enabled_ = enabled; }

    //=============================================================================
    // Original Parser Management Interface
    //=============================================================================

    const IMessageParser *FlexMessageDecoder::getParserForType(MessageType type) const {
        auto it = parser_map_.find(type);
        if (it != parser_map_.end()) { return it->second; }
        return nullptr;
    }

    bool FlexMessageDecoder::isTypeSupported(MessageType type) const {
        return parser_map_.find(type) != parser_map_.end();
    }

    std::vector<MessageType> FlexMessageDecoder::getSupportedTypes() const {
        std::vector<MessageType> types;
        types.reserve(parser_map_.size());

        for (const auto &pair: parser_map_) { types.push_back(pair.first); }

        return types;
    }

    std::vector<std::pair<std::string, std::vector<MessageType>>> FlexMessageDecoder::getParserInfo() const {
        std::vector<std::pair<std::string, std::vector<MessageType>>> info;

        // Collect unique parsers and their supported types
        std::vector<const IMessageParser *> unique_parsers = { alphanumeric_parser_.get(), numeric_parser_.get(),
                                                               tone_parser_.get(), binary_parser_.get() };

        for (const auto *parser: unique_parsers) {
            if (parser != nullptr) { info.emplace_back(parser->getParserName(), parser->getSupportedTypes()); }
        }

        return info;
    }

    const BinaryParser &FlexMessageDecoder::getDefaultParser() const { return *binary_parser_; }

    //=============================================================================
    // Private Methods Implementation
    //=============================================================================

    void FlexMessageDecoder::processGroupMessage(MessageParseResult &result) {
        if (!group_handler_ || result.group_data.isEmpty()) { return; }

        try {
            GroupMessageInfo group_info = group_handler_->processGroupMessage(result.group_data.group_bit);
            if (group_info.isValid()) {
                result.group_data.capcodes = std::move(group_info.capcodes);

                // Invoke group callback if configured
                if (options_.group_callback) { options_.group_callback(group_info); }
            }
        } catch (const std::exception &e) {
            result.error_message += " (Group processing error: " + std::string(e.what()) + ")";
        }
    }

    void FlexMessageDecoder::updateStatistics(const MessageParseInput &input, const MessageParseResult &result) {
        statistics_.total_messages++;

        if (result.success) {
            statistics_.successful_messages++;
        } else {
            statistics_.failed_messages++;
        }

        // Update type-specific counters
        switch (input.type) {
            case MessageType::Alphanumeric:
            case MessageType::Secure:
                statistics_.alphanumeric_messages++;
                break;
            case MessageType::StandardNumeric:
            case MessageType::SpecialNumeric:
            case MessageType::NumberedNumeric:
                statistics_.numeric_messages++;
                break;
            case MessageType::Tone:
                statistics_.tone_messages++;
                break;
            case MessageType::Binary:
                statistics_.binary_messages++;
                break;
            default:
                break;
        }

        // Update feature-specific counters
        if (input.is_group_message) { statistics_.group_messages++; }

        if (result.fragment_flag != FragmentFlag::Unknown && result.fragment_flag != FragmentFlag::Complete) {
            statistics_.fragmented_messages++;
        }

        if (input.long_address) { statistics_.long_address_messages++; }
    }

    void FlexMessageDecoder::invokeCallbacks(const MessageParseResult &result) {
        if (options_.message_callback) {
            try {
                options_.message_callback(result);
            } catch (const std::exception &e) {
                if (options_.error_callback) {
                    options_.error_callback("Message callback error: " + std::string(e.what()));
                }
            }
        }

        if (!result.success && options_.error_callback && !result.error_message.empty()) {
            options_.error_callback(result.error_message);
        }
    }

    bool FlexMessageDecoder::validateMessage(MessageParseResult &result) {
        // Validate message content length
        if (result.content.length() > options_.max_message_length) {
            result.error_message += " (Content exceeds maximum length)";
            return false;
        }

        // Additional validation could be added here
        // - Character set validation
        // - Content format validation
        // - Checksum validation (if available)

        return true;
    }

    void FlexMessageDecoder::outputFormattedMessage(const MessageParseResult &result, const MessageParseInput &input) {

        /// Convert MessageParseResult to ParsedMessage
        ParsedMessage message;
        message.content = result.content;
        message.fragment_flag = result.fragment_flag;
        message.is_group_message = !result.group_data.isEmpty();
        message.group_bit = result.group_data.group_bit;

        // Build required info structures from input
        MessageInfo msg_info{};
        msg_info.capcode = input.capcode;
        msg_info.type = input.type;
        msg_info.long_address = input.long_address;
        msg_info.is_group_message = input.is_group_message;
        msg_info.fragment_number = input.fragment_number;
        msg_info.continuation_flag = input.continuation_flag;


        SyncInfo sync_info{};
        sync_info.baud_rate = input.baud_rate; // These would need to be added to MessageParseInput
        sync_info.levels = input.levels; // or passed separately
        sync_info.polarity = input.polarity;

        FrameInfo frame_info{};
        frame_info.cycle_number = input.cycle_number; // These would need to be added to MessageParseInput
        frame_info.frame_number = input.frame_number; // or passed separately

        // Get group capcodes if this is a group message
        std::vector<int64_t> group_capcodes;
        if (!result.group_data.isEmpty() && !result.group_data.capcodes.empty()) {
            group_capcodes = result.group_data.capcodes;
        }

        // Output the formatted message
        output_formatter_->outputMessage(message, msg_info, sync_info, frame_info, input.phase_id, group_capcodes);
    }


    void FlexMessageDecoder::postProcessMessage(MessageParseResult &result) {
        if (!result.success || result.content.empty()) { return; }

        // Trim whitespace from content
        auto start = result.content.find_first_not_of(" \t\r\n");
        auto end = result.content.find_last_not_of(" \t\r\n");

        if (start != std::string::npos && end != std::string::npos) {
            result.content = result.content.substr(start, end - start + 1);
        } else if (start == std::string::npos) {
            result.content.clear(); // All whitespace
        }

        // Additional post-processing could be added here
        // - Character encoding conversion
        // - Content normalization
        // - Format standardization
    }

    void FlexMessageDecoder::initializeParsers() {
        // Create parser instances using RAII
        alphanumeric_parser_ = std::make_unique<AlphanumericParser>();
        numeric_parser_ = std::make_unique<NumericParser>();
        tone_parser_ = std::make_unique<ToneParser>();
        binary_parser_ = std::make_unique<BinaryParser>();

        // Build parser lookup table for efficient type-based selection
        buildParserMap();
    }

    void FlexMessageDecoder::buildParserMap() {
        parser_map_.clear();

        // Register alphanumeric parser
        for (MessageType type: alphanumeric_parser_->getSupportedTypes()) {
            parser_map_[type] = alphanumeric_parser_.get();
        }

        // Register numeric parser
        for (MessageType type: numeric_parser_->getSupportedTypes()) { parser_map_[type] = numeric_parser_.get(); }

        // Register tone parser
        for (MessageType type: tone_parser_->getSupportedTypes()) { parser_map_[type] = tone_parser_.get(); }

        // Register binary parser
        for (MessageType type: binary_parser_->getSupportedTypes()) { parser_map_[type] = binary_parser_.get(); }
    }

} // namespace flex_next_decoder

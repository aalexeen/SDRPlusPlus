#include "FlexMessageDecoder.h"

namespace flex_next_decoder {

FlexMessageDecoder::FlexMessageDecoder() {
    initializeParsers();
}

MessageParseResult FlexMessageDecoder::parseMessage(const MessageParseInput& input) const {
    // Find appropriate parser for message type
    auto parser = getParserForType(input.type);
    if (parser != nullptr) {
        return parser->parseMessage(input);
    }

    // Fallback to binary parser for unknown types
    return binary_parser_->parseAsDefault(input);
}

const IMessageParser* FlexMessageDecoder::getParserForType(MessageType type) const {
    auto it = parser_map_.find(type);
    if (it != parser_map_.end()) {
        return it->second;
    }
    return nullptr;
}

bool FlexMessageDecoder::isTypeSupported(MessageType type) const {
    return parser_map_.find(type) != parser_map_.end();
}

std::vector<MessageType> FlexMessageDecoder::getSupportedTypes() const {
    std::vector<MessageType> types;
    types.reserve(parser_map_.size());
    
    for (const auto& pair : parser_map_) {
        types.push_back(pair.first);
    }
    
    return types;
}

std::vector<std::pair<std::string, std::vector<MessageType>>> FlexMessageDecoder::getParserInfo() const {
    std::vector<std::pair<std::string, std::vector<MessageType>>> info;
    
    // Collect unique parsers and their supported types
    std::vector<const IMessageParser*> unique_parsers = {
        alphanumeric_parser_.get(),
        numeric_parser_.get(),
        tone_parser_.get(),
        binary_parser_.get()
    };

    for (const auto* parser : unique_parsers) {
        if (parser != nullptr) {
            info.emplace_back(parser->getParserName(), parser->getSupportedTypes());
        }
    }

    return info;
}

const BinaryParser& FlexMessageDecoder::getDefaultParser() const {
    return *binary_parser_;
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
    for (MessageType type : alphanumeric_parser_->getSupportedTypes()) {
        parser_map_[type] = alphanumeric_parser_.get();
    }

    // Register numeric parser
    for (MessageType type : numeric_parser_->getSupportedTypes()) {
        parser_map_[type] = numeric_parser_.get();
    }

    // Register tone parser
    for (MessageType type : tone_parser_->getSupportedTypes()) {
        parser_map_[type] = tone_parser_.get();
    }

    // Register binary parser
    for (MessageType type : binary_parser_->getSupportedTypes()) {
        parser_map_[type] = binary_parser_.get();
    }
}

} // namespace flex_next_decoder
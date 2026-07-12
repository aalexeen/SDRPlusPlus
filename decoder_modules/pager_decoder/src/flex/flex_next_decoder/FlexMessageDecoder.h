#pragma once

#include "FlexTypes.h"
#include "parsers/IMessageParser.h"
#include "parsers/AlphanumericParser.h"
#include "parsers/NumericParser.h"
#include "parsers/ToneParser.h"
#include "parsers/BinaryParser.h"
#include "FlexGroupHandler.h"
#include <memory>
#include <unordered_map>
#include <map>
#include <tuple>
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include "FlexNextDecoder.h"
#include "FlexOutputFormatter.h"

namespace flex_next_decoder {

    /**
     * @struct MessageStatistics
     * @brief Statistics tracking for message decoding
     */
    struct MessageStatistics {
        uint64_t total_messages = 0;
        uint64_t successful_messages = 0;
        uint64_t failed_messages = 0;
        uint64_t alphanumeric_messages = 0;
        uint64_t numeric_messages = 0;
        uint64_t tone_messages = 0;
        uint64_t binary_messages = 0;
        uint64_t group_messages = 0;
        uint64_t fragmented_messages = 0;
        uint64_t long_address_messages = 0;

        /**
         * @brief Get success rate as percentage
         * @return Success rate (0.0 to 100.0)
         */
        double getSuccessRate() const {
            return (total_messages > 0) ? (100.0 * successful_messages / total_messages) : 0.0;
        }

        /**
         * @brief Reset all statistics
         */
        void reset() { *this = MessageStatistics{}; }
    };

    /**
     * @struct DecodingOptions
     * @brief Configuration options for message decoding
     */
    struct DecodingOptions {
        bool enable_group_processing = true; // Process group messages
        bool enable_fragment_assembly = true; // Attempt to assemble fragmented messages
        bool validate_capcodes = true; // Validate capcode ranges
        bool enable_statistics = true; // Track decoding statistics
        uint32_t max_message_length = MAX_ALN_LENGTH; // Maximum message content length
        bool enable_console_output = true; // NEW: Enable console output

        // Callback functions for real-time processing
        std::function<void(const MessageParseResult &)> message_callback;
        std::function<void(const GroupMessageInfo &)> group_callback;
        std::function<void(const std::string &)> error_callback;
    };

    /**
     * @struct FragmentSlot
     * @brief One in-progress fragment-reassembly stream.
     *
     * A stream is identified by (capcode, type, msg_n) — the reference
     * demod_flex_next.c FragStore key (frag_find). Keying on all three is what
     * separates two senders (or two message streams) whose fragments interleave
     * across the same frames; keying on capcode alone (or the old capcode=0
     * bug) let them collide and bleed into each other.
     */
    struct FragmentSlot {
        std::string data; // accumulated text of the fragments seen so far
        uint32_t frame_received = 0; // absolute frame of the FIRST fragment (for eviction)
    };

    /**
     * @class FlexMessageDecoder
     * @brief Enhanced Strategy Pattern coordinator for FLEX message parsing
     *
     * Manages multiple message parsers and provides comprehensive message decoding
     * functionality including group message processing, fragment assembly, statistics
     * tracking, and integration with other FLEX system components.
     *
     * Features:
     * - Strategy Pattern parser selection and management
     * - Group message processing with FlexGroupHandler integration
     * - Message fragment assembly and reconstruction
     * - Comprehensive statistics and monitoring
     * - Configurable callbacks for real-time processing
     * - Error handling and recovery
     * - Message validation and post-processing
     */
    class FlexMessageDecoder : public FlexNextDecoder {
    public:
        /**
         * @brief Constructor - initializes all message parsers
         * @param group_handler Optional group handler for group message processing
         * @param output_formatter
         */
        FlexMessageDecoder(std::shared_ptr<FlexGroupHandler> group_handler,
                           std::unique_ptr<FlexOutputFormatter> output_formatter);

        FlexMessageDecoder(std::shared_ptr<FlexGroupHandler> group_handler,
                           std::unique_ptr<FlexOutputFormatter> output_formatter, int verbosity_level);

        /**
         * @brief Destructor - automatic cleanup via RAII
         */
        ~FlexMessageDecoder() override = default;

        // Delete copy operations (unique_ptr members)
        FlexMessageDecoder(const FlexMessageDecoder &) = delete;
        FlexMessageDecoder &operator=(const FlexMessageDecoder &) = delete;

        // Allow move operations
        FlexMessageDecoder(FlexMessageDecoder &&) = default;
        FlexMessageDecoder &operator=(FlexMessageDecoder &&) = default;

        //=========================================================================
        // Message Parsing Interface
        //=========================================================================

        /**
         * @brief Parse message using appropriate strategy with enhanced processing
         * @param input Message parsing parameters
         * @return Enhanced message parsing result
         *
         * Performs complete message processing including:
         * - Parser selection and message parsing
         * - Group message processing
         * - Fragment assembly
         * - Statistics tracking
         * - Callback invocation
         */
        MessageParseResult parseMessage(const MessageParseInput &input);

        /**
         * @brief Parse message with custom options
         * @param input Message parsing parameters
         * @param options Custom decoding options
         * @return Enhanced message parsing result
         */
        MessageParseResult parseMessage(const MessageParseInput &input, const DecodingOptions &options);

        //=========================================================================
        // Configuration and Management
        //=========================================================================

        /**
         * @brief Set group handler for group message processing
         * @param group_handler Group handler instance
         */
        void setGroupHandler(std::shared_ptr<FlexGroupHandler> group_handler);

        /**
         * @brief Set decoding options
         * @param options Decoding configuration
         */
        void setDecodingOptions(const DecodingOptions &options);

        /**
         * @brief Get current decoding options
         * @return Current decoding options
         */
        const DecodingOptions &getDecodingOptions() const;

        void setMessageCallback(std::function<void(int64_t, int, const std::string &)> callback);

        //=========================================================================
        // Fragment Assembly
        //=========================================================================

        /**
         * @brief Process a message fragment: buffer F fragments and reassemble
         *        on the terminating C fragment.
         * @param input  Original parse input (supplies capcode/type/msg_n/frame)
         * @param result Message parse result (content replaced with the
         *               reassembled text on a completing C fragment)
         * @return true if the result was modified (reassembled)
         */
        bool processFragment(const MessageParseInput &input, MessageParseResult &result);

        //=========================================================================
        // Statistics and Monitoring
        //=========================================================================

        /**
         * @brief Get decoding statistics
         * @return Current statistics
         */
        const MessageStatistics &getStatistics() const;

        /**
         * @brief Reset statistics
         */
        void resetStatistics();

        /**
         * @brief Enable or disable statistics tracking
         * @param enabled true to enable statistics
         */
        void setStatisticsEnabled(bool enabled);

        //=========================================================================
        // Parser Management (from original interface)
        //=========================================================================

        /**
         * @brief Get parser for specific message type
         * @param type Message type to get parser for
         * @return Pointer to appropriate parser, or nullptr if not found
         */
        const IMessageParser *getParserForType(MessageType type) const;

        /**
         * @brief Check if a message type is supported
         * @param type Message type to check
         * @return true if a parser exists for this type
         */
        bool isTypeSupported(MessageType type) const;

        /**
         * @brief Get list of all supported message types
         * @return Vector of all supported MessageType values
         */
        std::vector<MessageType> getSupportedTypes() const;

        /**
         * @brief Get information about all available parsers
         * @return Vector of parser names and their supported types
         */
        std::vector<std::pair<std::string, std::vector<MessageType>>> getParserInfo() const;

        /**
         * @brief Get the default fallback parser
         * @return Reference to binary parser used for unknown types
         */
        const BinaryParser &getDefaultParser() const;

    private:
        //=========================================================================
        // Enhanced Processing Methods
        //=========================================================================

        /**
         * @brief Process group message if applicable
         * @param result Message parse result (potentially modified)
         */
        void processGroupMessage(MessageParseResult &result);

        /**
         * @brief Update statistics based on parse result
         * @param input Original input parameters
         * @param result Parse result
         */
        void updateStatistics(const MessageParseInput &input, const MessageParseResult &result);

        /**
         * @brief Invoke configured callbacks
         * @param result Parse result
         */
        void invokeCallbacks(const MessageParseResult &result);

        /**
         * @brief Validate message content and metadata
         * @param result Parse result (potentially modified)
         * @return true if message passed validation
         */
        bool validateMessage(MessageParseResult &result);

        /**
         * @brief Post-process message content
         * @param result Parse result (potentially modified)
         */
        void postProcessMessage(MessageParseResult &result);

        /**
         * @brief Output formatted message to console
         * @param result Parse result
         * @param input Original input parameters
         */
        void outputFormattedMessage(const MessageParseResult &result, const MessageParseInput &input);

        //=========================================================================
        // Original Parser Management (from base implementation)
        //=========================================================================

        /**
         * @brief Initialize all message parsers and build lookup table
         */
        void initializeParsers();

        /**
         * @brief Build the parser lookup table
         */
        void buildParserMap();

        /**
         * @brief Evict the oldest fragment slot if the store is at capacity.
         */
        void evictIfFull();

        //=========================================================================
        // Member Variables
        //=========================================================================

        // Parser management (from original implementation)
        std::unique_ptr<AlphanumericParser> alphanumeric_parser_;
        std::unique_ptr<NumericParser> numeric_parser_;
        std::unique_ptr<ToneParser> tone_parser_;
        std::unique_ptr<BinaryParser> binary_parser_;
        std::unordered_map<MessageType, IMessageParser *> parser_map_;

        // Enhanced functionality
        std::shared_ptr<FlexGroupHandler> group_handler_;
        std::unique_ptr<FlexOutputFormatter> output_formatter_;
        DecodingOptions options_;
        MessageStatistics statistics_;

        // Fragment reassembly store — keyed by (capcode, type, msg_n), the
        // reference FragStore key. Lives entirely on the decode thread (only
        // touched from parseMessage → processFragment); never shared with the
        // GUI, so it cannot recreate the CRASH-01 data race. Bounded to
        // FRAG_MAX_SLOTS with oldest-frame eviction, mirroring the C 64-slot
        // ring so it cannot grow unbounded on a live stream.
        using FragmentKey = std::tuple<int64_t, int, uint32_t>; // capcode, type, msg_n
        std::map<FragmentKey, FragmentSlot> fragment_slots_;
        static constexpr size_t FRAG_MAX_SLOTS = 64;

        // State management
        bool statistics_enabled_ = true;
    };

} // namespace flex_next_decoder

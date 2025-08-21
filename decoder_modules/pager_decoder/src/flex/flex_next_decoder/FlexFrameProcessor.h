#pragma once

#include "FlexTypes.h"
#include "FlexErrorCorrector.h"
#include "FlexGroupHandler.h"
#include "FlexMessageDecoder.h"
#include "FlexDataCollector.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace flex_next_decoder {

    /**
     * @struct BlockInfoWord
     * @brief Decoded Block Information Word from frame header
     */
    struct BlockInfoWord {
        uint32_t raw_data = 0;
        uint32_t address_offset = 0; // Start of address words (1-4)
        uint32_t vector_offset = 0; // Start of vector words (1-63)
        uint32_t max_pages = 0; // Maximum possible pages in frame
        bool is_valid = false;

        /**
         * @brief Check if BIW contains valid data
         * @return true if BIW is valid
         */
        bool isValid() const { return is_valid && vector_offset > address_offset && max_pages > 0; }
    };

    /**
     * @struct AddressInfoWord
     * @brief Decoded Address Information Word
     */
    struct AddressInfoWord {
        uint32_t raw_data = 0;
        int64_t capcode = 0;
        bool long_address = false;
        bool is_group_message = false;
        int group_bit = -1;
        bool is_valid = false;

        /**
         * @brief Check if AIW is valid
         * @return true if capcode is within valid range
         */
        bool isValid() const { return is_valid && capcode >= 0 && capcode <= MAX_CAPCODE; }
    };

    /**
     * @struct VectorInfoWord
     * @brief Decoded Vector Information Word
     */
    struct VectorInfoWord {
        uint32_t raw_data = 0;
        MessageType message_type = MessageType::Tone;
        uint32_t message_word_start = 0; // Start of message data
        uint32_t message_length = 0; // Length in words
        uint32_t header_word_index = 0; // Header word location
        uint32_t fragment_number = 0; // Fragment info (0-3)
        bool continuation_flag = false; // Continuation flag
        bool is_valid = false;

        // Short Instruction specific fields
        uint32_t assigned_frame = 0; // Target frame for group message
        int group_bit_target = -1; // Target group bit for registration

        /**
         * @brief Check if VIW is valid
         * @return true if VIW has valid structure
         */
        bool isValid() const { return is_valid; }

        /**
         * @brief Check if this is a Short Instruction message
         * @return true if message type is Short Instruction
         */
        bool isShortInstruction() const { return message_type == MessageType::ShortInstruction; }
    };

    /**
     * @struct ProcessedMessage
     * @brief Result of processing a complete message
     */
    struct ProcessedMessage {
        AddressInfoWord address_info;
        VectorInfoWord vector_info;
        MessageParseResult parse_result;
        uint32_t phase_index = 0; // Which phase this came from
        char phase_name = 'A'; // Phase name ('A', 'B', 'C', 'D')

        /**
         * @brief Check if message processing was successful
         * @return true if all components are valid
         */
        bool isValid() const { return address_info.isValid() && vector_info.isValid() && parse_result.success; }
    };

    /**
     * @struct FrameProcessingResult
     * @brief Result of processing an entire frame across all phases
     */
    struct FrameProcessingResult {
        std::vector<ProcessedMessage> messages;
        std::vector<std::string> errors;
        uint32_t total_messages = 0;
        uint32_t successful_messages = 0;
        uint32_t short_instructions = 0;
        bool has_errors = false;

        /**
         * @brief Add error message to result
         * @param error Error description
         */
        void addError(const std::string &error) {
            errors.push_back(error);
            has_errors = true;
        }

        /**
         * @brief Add processed message to result
         * @param message Processed message
         */
        void addMessage(ProcessedMessage &&message) {
            total_messages++;
            if (message.isValid()) { successful_messages++; }
            if (message.vector_info.isShortInstruction()) { short_instructions++; }
            messages.emplace_back(std::move(message));
        }
    };

    /**
     * @class FlexFrameProcessor
     * @brief Processes FLEX frame data and extracts messages
     *
     * Handles the complete FLEX frame processing pipeline:
     * 1. BCH error correction on phase data
     * 2. Block Information Word (BIW) extraction
     * 3. Address Information Word (AIW) processing
     * 4. Vector Information Word (VIW) processing
     * 5. Short Instruction handling for group messages
     * 6. Message parsing and dispatch
     * 7. Multi-phase coordination based on transmission mode
     */
    class FlexFrameProcessor : public FlexNextDecoder {
    public:
        /**
         * @brief Constructor
         * @param error_corrector BCH error corrector instance
         * @param message_decoder Message parser coordinator
         * @param group_handler Group message handler
         */
        FlexFrameProcessor(std::shared_ptr<FlexErrorCorrector> error_corrector,
                           std::shared_ptr<FlexMessageDecoder> message_decoder,
                           std::shared_ptr<FlexGroupHandler> group_handler);

        FlexFrameProcessor(std::shared_ptr<FlexErrorCorrector> error_corrector,
                           std::shared_ptr<FlexMessageDecoder> message_decoder,
                           std::shared_ptr<FlexGroupHandler> group_handler, int verbosity_level);

        /**
         * @brief Destructor
         */
        ~FlexFrameProcessor() = default;

        // Delete copy operations (manages shared state)
        FlexFrameProcessor(const FlexFrameProcessor &) = delete;
        FlexFrameProcessor &operator=(const FlexFrameProcessor &) = delete;

        // Allow move operations
        FlexFrameProcessor(FlexFrameProcessor &&) = default;
        FlexFrameProcessor &operator=(FlexFrameProcessor &&) = default;

        //=========================================================================
        // Frame Processing Interface
        //=========================================================================

        /**
         * @brief Process complete frame data across all active phases
         * @param phase_data_collector Source of phase buffer data
         * @param baud_rate Current baud rate (1600 or 3200)
         * @param fsk_levels Current FSK levels (2 or 4)
         * @param cycle_number Current cycle number (0-15)
         * @param frame_number Current frame number (0-127)
         * @return Frame processing results with all extracted messages
         */
        FrameProcessingResult processFrame(const FlexDataCollector &phase_data_collector, uint32_t baud_rate,
                                           uint32_t fsk_levels, uint32_t cycle_number, uint32_t frame_number);

        /**
         * @brief Process single phase data
         * @param phase_buffer Phase buffer to process
         * @param phase_name Phase identifier ('A', 'B', 'C', 'D')
         * @param cycle_number Current cycle number
         * @param frame_number Current frame number
         * @return Vector of processed messages from this phase
         */
        std::vector<ProcessedMessage> processPhase(const PhaseBuffer &phase_buffer, char phase_name,
                                                   uint32_t cycle_number, uint32_t frame_number);

        //=========================================================================
        // Configuration
        //=========================================================================

        /**
         * @brief Set message output callback for real-time processing
         * @param callback Function called for each processed message
         */
        void setMessageCallback(std::function<void(const ProcessedMessage &)> callback);

        /**
         * @brief Enable or disable BCH error correction
         * @param enabled true to enable error correction
         */
        void setErrorCorrectionEnabled(bool enabled);

        /**
         * @brief Update sync information for output formatting
         * @param sync_info Current sync information
         * @param fiw_raw Raw Frame Information Word
         */
        void updateSyncInfo(const SyncInfo &sync_info, uint32_t fiw_raw);


    private:
        //=========================================================================
        // Phase Processing Methods
        //=========================================================================

        /**
         * @brief Apply BCH error correction to phase data
         * @param phase_data Mutable phase data buffer
         * @param phase_name Phase identifier for logging
         * @return true if error correction succeeded
         */
        bool applyErrorCorrection(std::vector<uint32_t> &phase_data, char phase_name);

        /**
         * @brief Extract and validate Block Information Word
         * @param phase_data Phase buffer data
         * @param phase_name
         * @return Decoded BIW structure
         */
        BlockInfoWord extractBlockInfoWord(const std::vector<uint32_t> &phase_data, char phase_name);

        /**
         * @brief Process Address Information Word
         * @param raw_aiw Raw AIW data
         * @param next_word Next word for long address processing
         * @return Decoded AIW structure
         */
        AddressInfoWord processAddressInfoWord(uint32_t raw_aiw, uint32_t next_word);

        /**
         * @brief Process Vector Information Word
         * @param raw_viw Raw VIW data
         * @param address_info Associated address information
         * @param header_word Header word for fragment info
         * @return Decoded VIW structure
         */
        VectorInfoWord processVectorInfoWord(uint32_t raw_viw, const AddressInfoWord &address_info,
                                             uint32_t header_word);

        /**
         * @brief Handle Short Instruction message for group registration
         * @param address_info Address information
         * @param vector_info Vector information
         * @param cycle_number Current cycle
         * @param frame_number Current frame
         * @return true if registration succeeded
         */
        bool handleShortInstruction(const AddressInfoWord &address_info, const VectorInfoWord &vector_info,
                                    uint32_t cycle_number, uint32_t frame_number);

        /**
         * @brief Parse message content using appropriate parser
         * @param address_info Address information
         * @param vector_info Vector information
         * @param phase_data Phase buffer data
         * @param cycle_number Current cycle
         * @param frame_number Current frame
         * @return Message parsing result
         */
        MessageParseResult parseMessageContent(const AddressInfoWord &address_info, const VectorInfoWord &vector_info,
                                               const std::vector<uint32_t> &phase_data, uint32_t cycle_number,
                                               uint32_t frame_number);

        //=========================================================================
        // Utility Methods
        //=========================================================================

        /**
         * @brief Determine which phases to process based on transmission mode
         * @param baud_rate Baud rate (1600 or 3200)
         * @param fsk_levels FSK levels (2 or 4)
         * @return Vector of phase names to process
         */
        std::vector<char> getActivePhasesForMode(uint32_t baud_rate, uint32_t fsk_levels);

        /**
         * @brief Validate capcode range
         * @param capcode Capcode to validate
         * @return true if capcode is within valid range
         */
        static bool isValidCapcode(int64_t capcode);

        //=========================================================================
        // Member Variables
        //=========================================================================

        std::shared_ptr<FlexErrorCorrector> error_corrector_;
        std::shared_ptr<FlexMessageDecoder> message_decoder_;
        std::shared_ptr<FlexGroupHandler> group_handler_;

        std::function<void(const ProcessedMessage &)> message_callback_;
        bool error_correction_enabled_ = true;

        // Add sync information storage
        SyncInfo sync_info_;
        uint32_t current_fiw_raw_ = 0; // Store raw FIW for frame info extraction

        //=========================================================================
        // Constants
        //=========================================================================

        static constexpr uint32_t MESSAGE_BITS_MASK = 0x1FFFFF; // 21-bit message mask
        static constexpr uint32_t BIW_ADDRESS_OFFSET_MASK = 0x3; // Bits 9-8
        static constexpr uint32_t BIW_VECTOR_OFFSET_MASK = 0x3F; // Bits 15-10
        static constexpr uint32_t AIW_SHORT_ADDRESS_OFFSET = 0x8000; // Short address offset
        static constexpr int64_t LONG_ADDRESS_THRESHOLD_1 = 0x8001;
        static constexpr int64_t LONG_ADDRESS_THRESHOLD_2_LOW = 0x1E0000;
        static constexpr int64_t LONG_ADDRESS_THRESHOLD_2_HIGH = 0x1F0001;
        static constexpr int64_t LONG_ADDRESS_THRESHOLD_3 = 0x1F7FFE;
        static constexpr int64_t LONG_ADDRESS_CONSTANT = 2068480L;
    };

} // namespace flex_next_decoder

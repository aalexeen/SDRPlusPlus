#include "FlexFrameProcessor.h"
#include <iostream>
#include <algorithm>

namespace flex_next_decoder {

    //=============================================================================
    // FlexFrameProcessor Implementation
    //=============================================================================

    FlexFrameProcessor::FlexFrameProcessor(std::shared_ptr<FlexErrorCorrector> error_corrector,
                                           std::shared_ptr<FlexMessageDecoder> message_decoder,
                                           std::shared_ptr<FlexGroupHandler> group_handler) :
        error_corrector_(std::move(error_corrector)), message_decoder_(std::move(message_decoder)),
        group_handler_(std::move(group_handler)) {

        // Configure message decoder with group handler if both are available
        if (message_decoder_ && group_handler_) { message_decoder_->setGroupHandler(group_handler_); }
    }

    FlexFrameProcessor::FlexFrameProcessor(std::shared_ptr<FlexErrorCorrector> error_corrector,
                                           std::shared_ptr<FlexMessageDecoder> message_decoder,
                                           std::shared_ptr<FlexGroupHandler> group_handler, int verbosity_level) :
        FlexNextDecoder(verbosity_level), error_corrector_(std::move(error_corrector)),
        message_decoder_(std::move(message_decoder)), group_handler_(std::move(group_handler)) {

        // Configure message decoder with group handler if both are available
        if (message_decoder_ && group_handler_) { message_decoder_->setGroupHandler(group_handler_); }
    }

    FrameProcessingResult FlexFrameProcessor::processFrame(const FlexDataCollector &phase_data_collector,
                                                           uint32_t baud_rate, uint32_t fsk_levels,
                                                           uint32_t cycle_number, uint32_t frame_number) { // checked
        FrameProcessingResult result;

        // Determine which phases to process based on transmission mode
        std::vector<char> active_phases = getActivePhasesForMode(baud_rate, fsk_levels);

        // Process each active phase
        for (char phase_name: active_phases) {
            const PhaseBuffer *phase_buffer = nullptr;

            // Get appropriate phase buffer
            switch (phase_name) {
                case 'A':
                    phase_buffer = &phase_data_collector.getPhaseA();
                    break;
                case 'B':
                    phase_buffer = &phase_data_collector.getPhaseB();
                    break;
                case 'C':
                    phase_buffer = &phase_data_collector.getPhaseC();
                    break;
                case 'D':
                    phase_buffer = &phase_data_collector.getPhaseD();
                    break;
                default:
                    result.addError("Invalid phase name: " + std::string(1, phase_name));
                    continue;
            }

            if (phase_buffer == nullptr) {
                result.addError("Phase buffer is null for phase " + std::string(1, phase_name));
                continue;
            }

            // Process this phase
            try {
                std::vector<ProcessedMessage> phase_messages =
                        processPhase(*phase_buffer, phase_name, cycle_number, frame_number);

                // Add messages to result
                for (auto &message: phase_messages) { result.addMessage(std::move(message)); }
            } catch (const std::exception &e) {
                result.addError("Exception processing phase " + std::string(1, phase_name) + ": " + e.what());
            }
        }

        return result;
    }

    std::vector<ProcessedMessage> FlexFrameProcessor::processPhase(const PhaseBuffer &phase_buffer, char phase_name,
                                                                   uint32_t cycle_number, uint32_t frame_number) {
        std::vector<ProcessedMessage> messages;

        // Copy phase data for error correction
        std::vector<uint32_t> phase_data(phase_buffer.data(), phase_buffer.data() + phase_buffer.size());

        // Per-word BCH status, parallel to phase_data (reference bch_err[]):
        // 1 = uncorrectable, 0 = ok. Threaded into parsers so corrupt words
        // become '?' instead of decoded garbage (DROP-01).
        std::vector<uint8_t> word_error(phase_data.size(), 0);

        // Apply BCH error correction. Unlike the old code, this never abandons
        // the phase on too many failures (the reference C keeps going and marks
        // uncorrectable words); it records per-word status into word_error.
        if (error_correction_enabled_) {
            applyErrorCorrection(phase_data, word_error, phase_name);
        }

        // Extract Block Information Word
        BlockInfoWord biw = extractBlockInfoWord(phase_data, phase_name);
        if (!biw.isValid()) {
            return messages; // No valid data in this phase
        }

        // Process all address/vector word pairs
        for (uint32_t i = biw.address_offset; i < biw.vector_offset; i++) {
            try {
                // Skip address words that failed BCH correction (reference
                // demod_flex_next.c:3311). Gating on word_error BEFORE the idle
                // check is what makes it safe to no longer overwrite corrupt
                // words with the 0x1FFFFF idle sentinel: a genuinely uncorrectable
                // address word is dropped here, not misread as idle.
                if (i < word_error.size() && word_error[i]) {
                    continue; // Address word uncorrectable, skip page
                }

                // Check for idle codewords
                if (phase_data[i] == 0 || (phase_data[i] & MESSAGE_BITS_MASK) == MESSAGE_BITS_MASK) {
                    continue; // Skip idle words
                }

                // Process Address Information Word
                uint32_t next_word = (i + 1 < phase_data.size()) ? phase_data[i + 1] : 0;
                AddressInfoWord aiw = processAddressInfoWord(phase_data[i], next_word);

                if (!aiw.isValid()) {
                    continue; // Invalid address
                }

                // Long address consumes a 2nd word. Reference demod_flex_next.c
                // :3371-3380 skips the page if that word is out of range or failed
                // BCH correction (its value drives the capcode, so a bad one gives
                // a wrong recipient). Skip the consumed word either way.
                if (aiw.long_address) {
                    if (i + 1 >= biw.vector_offset ||
                        (i + 1 < word_error.size() && word_error[i + 1])) {
                        i++; // consume the (bad/missing) 2nd word
                        continue;
                    }
                }

                // Calculate vector word index
                uint32_t vector_index = biw.vector_offset + i - biw.address_offset;
                if (vector_index >= phase_data.size()) {
                    continue; // Vector index out of bounds
                }

                // Skip the page if the vector word itself failed BCH correction
                // (reference demod_flex_next.c:3504). A corrupt vector word means
                // the message boundaries/type are unknown, so parsers can't run;
                // done centrally here rather than duplicated in each parser.
                if (vector_index < word_error.size() && word_error[vector_index]) {
                    continue; // Vector word uncorrectable, skip page
                }

                // Get header word index for fragment information
                uint32_t header_word = 0;
                uint32_t header_index = 0;

                if (aiw.long_address && vector_index + 1 < phase_data.size()) {
                    header_index = vector_index + 1;
                    header_word = phase_data[header_index];
                } else if (!aiw.long_address) {
                    // Header is within the message - we'll extract it during VIW processing
                    header_word = 0; // Will be set in processVectorInfoWord
                }

                // Process Vector Information Word
                VectorInfoWord viw = processVectorInfoWord(phase_data[vector_index], aiw, header_word);

                if (!viw.isValid()) {
                    continue; // Invalid vector info
                }

                // Handle Short Instruction messages for group registration
                if (viw.isShortInstruction()) {
                    bool success = handleShortInstruction(aiw, viw, cycle_number, frame_number);
                    if (success) {
                        // Create a message entry for the Short Instruction
                        ProcessedMessage message;
                        message.address_info = aiw;
                        message.vector_info = viw;
                        message.phase_index = static_cast<uint32_t>(phase_name - 'A');
                        message.phase_name = phase_name;

                        // Create a successful parse result for Short Instruction
                        message.parse_result.success = true;
                        message.parse_result.content = "Short Instruction registered";
                        message.parse_result.fragment_flag = FragmentFlag::Complete;

                        messages.push_back(std::move(message));
                    }
                    continue; // Short Instructions don't need message parsing
                }

                // Parse message content. Pass the real vector word index (C `j`)
                // — Numeric/Tone parsers re-read the raw vector through it.
                MessageParseResult parse_result = parseMessageContent(aiw, viw, phase_data, word_error, vector_index, cycle_number, frame_number);

                // Create processed message
                ProcessedMessage message;
                message.address_info = aiw;
                message.vector_info = viw;
                message.parse_result = std::move(parse_result);
                message.phase_index = static_cast<uint32_t>(phase_name - 'A');
                message.phase_name = phase_name;

                // Call message callback if set
                if (message_callback_) { message_callback_(message); }

                messages.push_back(std::move(message));

                // Skip next address word if this was a long address
                if (aiw.long_address) { i++; }
            } catch (const std::exception &e) {
                // Continue processing other messages even if one fails
                continue;
            }
        }

        return messages;
    }

    void FlexFrameProcessor::setMessageCallback(std::function<void(const ProcessedMessage &)> callback) {
        message_callback_ = std::move(callback);
    }

    void FlexFrameProcessor::setErrorCorrectionEnabled(bool enabled) { error_correction_enabled_ = enabled; }

    //=============================================================================
    // Private Methods Implementation
    //=============================================================================

    bool FlexFrameProcessor::applyErrorCorrection(std::vector<uint32_t> &phase_data,
                                                  std::vector<uint8_t> &word_error, char phase_name) {
        word_error.assign(phase_data.size(), 0);

        if (!error_corrector_) {
            return true; // No error corrector available, assume data is clean
        }

        int failed_words = 0;
        int corrected_words = 0;
        int clean_words = 0;

        for (size_t i = 0; i < phase_data.size(); i++) {
            uint32_t original_word = phase_data[i];
            bool corrected = error_corrector_->fixErrors(phase_data[i], phase_name);

            if (!corrected) {
                // Reference (demod_flex_next.c) never overwrites an uncorrectable
                // word with an idle sentinel; it records bch_err[i]=1 and leaves
                // the (masked) received value in place. Parsers turn the flag into
                // '?'. Writing 0x1FFFFF here was the DROP-01 bug — it aliased the
                // idle pattern and made corrupt words silently vanish.
                word_error[i] = 1;
                failed_words++;
            } else if (phase_data[i] != (original_word & 0x1FFFFF)) {
                corrected_words++;
            } else {
                clean_words++;
            }

            // Always apply the 21-bit mask
            phase_data[i] &= MESSAGE_BITS_MASK;
        }

        if (verbosity_level_ >= 3 && (failed_words > 0 || corrected_words > 0)) {
            std::cout << "FLEX_NEXT: Phase " << phase_name << " - Clean:" << clean_words
                      << " Corrected:" << corrected_words << " Failed:" << failed_words
                      << " Total:" << phase_data.size() << std::endl;
        }

        // Never abandon the phase: the reference always parses what it can and
        // marks the rest with '?'. The old 50%-usable threshold was invented and
        // dropped whole phases the reference would have decoded (DROP-01b).
        return true;
    }

    BlockInfoWord FlexFrameProcessor::extractBlockInfoWord(const std::vector<uint32_t> &phase_data, char phase_name) {
        BlockInfoWord biw;

        if (phase_data.empty()) {
            return biw; // Invalid
        }

        biw.raw_data = phase_data[0];

        // Check for empty frame
        if (biw.raw_data == 0 || (biw.raw_data & MESSAGE_BITS_MASK) == MESSAGE_BITS_MASK) {
            return biw; // Invalid - empty frame
        }

        // Extract address and vector offsets using original algorithm
        // Address offset is bits 9-8, plus one for offset (to account for biw)
        biw.address_offset = ((biw.raw_data >> 8) & BIW_ADDRESS_OFFSET_MASK) + 1;

        // Vector offset is bits 15-10
        biw.vector_offset = (biw.raw_data >> 10) & BIW_VECTOR_OFFSET_MASK;

        // Validate structure
        if (biw.vector_offset < biw.address_offset) {
            if (verbosity_level_ >= 3) { std::cout << "Invalid structure: " << biw.raw_data << std::endl; }
            return biw; // Invalid structure
        }

        if (verbosity_level_ >= 3) {
            std::cout << "FLEX_NEXT: BlockInfoWord: (Phase " << phase_name << ") BIW:" << std::hex << biw.raw_data
                      << " AW " << std::dec << biw.address_offset << " VW " << biw.vector_offset << " (up to "
                      << biw.max_pages << " pages)" << std::endl;
        }

        biw.max_pages = biw.vector_offset - biw.address_offset;
        biw.is_valid = true;

        return biw;
    }

    AddressInfoWord FlexFrameProcessor::processAddressInfoWord(uint32_t raw_aiw, uint32_t next_word) {
        AddressInfoWord aiw;
        aiw.raw_data = raw_aiw;

        // Capcode decode per ARIB STD-43A Table 3.8.1-1 / 3.8.2.2-1, ported
        // verbatim from reference demod_flex_next.c:3359-3410. The old code used
        // the pre-_next demod_flex.c PDW formula (single formula + 3 thresholds),
        // which produced wrong capcodes for long addresses (PHASE-01).
        //
        // Address word classification (raw 21-bit values):
        //   LA1: 0x000001-0x008000 (first word of long-addr sets 1-2/1-3/1-4)
        //   SA : 0x008001-0x1E0000 (short address, capcode = aw - 0x8000)
        //   LA2: 0x1F7FFF-0x1FFFFE (first word of long-addr sets 2-3/2-4)
        // Only LA1 and LA2 begin a long address.
        aiw.long_address = (raw_aiw >= 0x000001u && raw_aiw <= 0x008000u) ||   // LA1
                           (raw_aiw >= 0x1F7FFFu && raw_aiw <= 0x1FFFFEu);     // LA2

        // Short-address default (also correct for all special-address ranges).
        aiw.capcode = static_cast<int64_t>(raw_aiw) - 0x8000;

        if (aiw.long_address) {
            // Long address uses two words: w1 = this word, w2 = next word.
            // The set is chosen by which ranges w1 and w2 fall into.
            uint32_t w1 = raw_aiw;
            uint32_t w2 = next_word;
            aiw.capcode = 0;

            if (w1 >= 1 && w1 <= 32768 &&
                w2 >= 2064383u && w2 <= 2097150u) {
                // Set 1-2: w1 in LA1, w2 in LA2
                aiw.capcode = static_cast<int64_t>(w1)
                              + static_cast<int64_t>(2097151u - w2) * 32768LL
                              + 2068480LL;
            } else if (w1 >= 1 && w1 <= 32768 &&
                       w2 >= 1966081u && w2 <= 2031616u) {
                // Set 1-3 / 1-4: w1 in LA1, w2 in LA3/LA4
                aiw.capcode = static_cast<int64_t>(w1)
                              + static_cast<int64_t>(w2 - 1933312u) * 32768LL
                              + 2068480LL;
            } else if (w1 >= 2064383u && w1 <= 2097150u &&
                       w2 >= 1966081u && w2 <= 2031616u) {
                // Set 2-3 / 2-4: w1 in LA2, w2 in LA3/LA4
                aiw.capcode = static_cast<int64_t>(w1 - 2064383u)
                              + static_cast<int64_t>(w2 - 1867776u) * 32768LL
                              + 2068479LL;
            } else {
                // Unknown long-address set — invalid, leave capcode invalid.
                return aiw;
            }
        }

        // Validate capcode range
        if (!isValidCapcode(aiw.capcode)) {
            return aiw; // Invalid capcode
        }

        // Check for group message
        aiw.is_group_message = FlexGroupHandler::isGroupCapcode(aiw.capcode);
        if (aiw.is_group_message) {
            aiw.group_bit = FlexGroupHandler::getGroupBit(aiw.capcode);

            // Group messages cannot use long addresses (by spec)
            if (aiw.long_address) {
                return aiw; // Invalid combination
            }
        }

        aiw.is_valid = true;
        return aiw;
    }

    VectorInfoWord FlexFrameProcessor::processVectorInfoWord(uint32_t raw_viw, const AddressInfoWord &address_info,
                                                             uint32_t header_word) {
        VectorInfoWord viw;
        viw.raw_data = raw_viw;

        // Extract message type, start, and length using original bit positions
        viw.message_type = static_cast<MessageType>((raw_viw >> 4) & 0x7);
        viw.message_word_start = (raw_viw >> 7) & 0x7F;
        viw.message_length = (raw_viw >> 14) & 0x7F;

        // Handle Short Instruction specific fields
        if (viw.message_type == MessageType::ShortInstruction) {
            viw.assigned_frame = (raw_viw >> 10) & 0x7F;
            viw.group_bit_target = static_cast<int>((raw_viw >> 17) & 0x7F);
            viw.is_valid = (viw.group_bit_target >= 0 && viw.group_bit_target < GROUP_BITS);
            return viw;
        }

        // Adjust message parameters based on address type
        if (address_info.long_address) {
            // Header is within the next VW
            viw.header_word_index = 0; // Will be calculated by caller
            if (viw.message_length >= 1) {
                viw.message_length--; // Per PDW
            }
        } else {
            // Header is within the message
            viw.header_word_index = viw.message_word_start;
            viw.message_word_start++;
            if (!address_info.is_group_message && viw.message_length >= 1) {
                viw.message_length--; // Fix for observed length issues
            }
        }

        // Extract fragment information from header word if available
        if (header_word != 0) {
            viw.fragment_number = (header_word >> 11) & 0x3;
            viw.continuation_flag = (header_word >> 10) & 0x1;
        }

        // Validate message bounds. The message_length field is only meaningful
        // for Alphanumeric/Secure/Binary — those parsers consume it. Numeric
        // types recompute their own word range from the vector word and ignore
        // message_length (reference demod_flex_next.c changelog: "length is only
        // checked for Alpha messages, the other types calculate their length
        // while decoding"). Gating numeric on message_length>0 wrongly rejected
        // short numeric pages whose n field decremented to 0 (SHORT-NUM-01).
        switch (viw.message_type) {
            case MessageType::StandardNumeric:
            case MessageType::SpecialNumeric:
            case MessageType::NumberedNumeric:
                // Length computed by the parser; only bound the start word.
                viw.is_valid = (viw.message_word_start < PHASE_WORDS);
                break;
            default:
                viw.is_valid = (viw.message_length > 0 &&
                                viw.message_word_start + viw.message_length <= PHASE_WORDS);
                break;
        }

        // Special case for tone messages
        if (viw.message_type == MessageType::Tone) {
            viw.message_word_start = 0;
            viw.message_length = 0;
            viw.is_valid = true;
        }

        return viw;
    }

    bool FlexFrameProcessor::handleShortInstruction(const AddressInfoWord &address_info,
                                                    const VectorInfoWord &vector_info, uint32_t cycle_number,
                                                    uint32_t frame_number) {
        if (!group_handler_ || !vector_info.isShortInstruction()) { return false; }

        // Register capcode to group using original vector word format
        uint32_t vector_word = (vector_info.group_bit_target << 17) | (vector_info.assigned_frame << 10);

        return group_handler_->registerCapcodeToGroup(address_info.capcode, vector_word, cycle_number, frame_number);
    }

    MessageParseResult FlexFrameProcessor::parseMessageContent(const AddressInfoWord &address_info,
                                                               const VectorInfoWord &vector_info,
                                                               const std::vector<uint32_t> &phase_data,
                                                               const std::vector<uint8_t> &word_error,
                                                               uint32_t vector_index,
                                                               uint32_t cycle_number, uint32_t frame_number) {
        if (!message_decoder_) {
            MessageParseResult result;
            result.success = false;
            result.error_message = "No message decoder available";
            return result;
        }

        // Prepare message parsing input
        MessageParseInput input;
        input.type = vector_info.message_type;
        input.long_address = address_info.long_address;
        input.capcode = address_info.capcode;
        input.phase_data = phase_data.data();
        input.phase_data_size = static_cast<uint32_t>(phase_data.size());
        input.word_error = word_error.empty() ? nullptr : word_error.data();
        input.message_word_start = vector_info.message_word_start;
        input.message_length = vector_info.message_length;
        input.vector_word_index = vector_index; // C `j`: Numeric/Tone re-read the raw vector word here
        input.fragment_number = vector_info.fragment_number;
        input.continuation_flag = vector_info.continuation_flag;
        input.is_group_message = address_info.is_group_message;
        input.group_bit = address_info.group_bit;
        input.cycle_number = cycle_number;
        input.frame_number = frame_number;
        // Add sync and frame information needed for output formatting
        input.baud_rate = sync_info_.baud_rate;
        input.levels = sync_info_.levels; // Store sync info in frame processor
        input.polarity = sync_info_.polarity;
        input.sync_code = sync_info_.sync_code;


        // Parse the message - FlexMessageDecoder now handles group processing internally
        MessageParseResult result = message_decoder_->parseMessage(input);

        return result;
    }

    std::vector<char> FlexFrameProcessor::getActivePhasesForMode(uint32_t baud_rate, uint32_t fsk_levels) { // checked
        // Original algorithm from decode_data()
        std::vector<char> phases;

        if (baud_rate == 1600) {
            if (fsk_levels == 2) {
                phases = { 'A' };
            } else {
                phases = { 'A', 'B' };
            }
        } else {
            if (fsk_levels == 2) {
                phases = { 'A', 'C' };
            } else {
                phases = { 'A', 'B', 'C', 'D' };
            }
        }

        return phases;
    }

    bool FlexFrameProcessor::isValidCapcode(int64_t capcode) { return capcode >= 0 && capcode <= MAX_CAPCODE; }

    void FlexFrameProcessor::updateSyncInfo(const SyncInfo &sync_info, uint32_t fiw_raw) {
        sync_info_ = sync_info;
        current_fiw_raw_ = fiw_raw;
    }


} // namespace flex_next_decoder

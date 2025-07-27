#pragma once
#include "utils/new_event.h"


#include <stdint.h>
#include <string>

namespace flex {
    enum MessageType {
        MESSAGE_TYPE_SECURE = 0,
        MESSAGE_TYPE_SHORT_INSTRUCTION = 1,
        MESSAGE_TYPE_TONE = 2,
        MESSAGE_TYPE_STANDARD_NUMERIC = 3,
        MESSAGE_TYPE_SPECIAL_NUMERIC = 4,
        MESSAGE_TYPE_ALPHANUMERIC = 5,
        MESSAGE_TYPE_BINARY = 6,
        MESSAGE_TYPE_NUMBERED_NUMERIC = 7
    };

    typedef uint64_t Address;

    class Decoder {
    public:
        Decoder();
        void process(uint8_t* symbols, int count);

        NewEvent<Address, MessageType, const std::string&> onMessage;

    private:
        // Sync detection
        uint64_t syncBuf;
        bool synced;
        int syncPos;

        // Frame state
        enum State {
            STATE_SYNC1,
            STATE_FIW,
            STATE_SYNC2,
            STATE_DATA
        } state;

        // Frame Information Word
        struct {
            uint32_t raw;
            uint8_t cycleno;
            uint8_t frameno;
        } fiw;

        // Data collection
        uint32_t dataBuf[88]; // Phase buffer
        int dataPos;
        int dataCount;

        // Baud rate and levels
        int baud;
        int levels;
        bool polarity;

        // BCH error correction
        bool correctBCH(uint32_t& data);

        // Sync detection
        bool checkSync(uint64_t buf);
        void decodeMode(uint16_t syncCode);

        // Frame processing
        bool decodeFIW();
        void processData();
        void decodeMessages();

        // Message decoding
        void decodeAlphanumeric(uint32_t* data, int start, int len, Address addr);
        void decodeNumeric(uint32_t* data, int start, int len, Address addr);
        void decodeTone(Address addr);

        // Utility functions
        int hammingDistance(uint32_t a, uint32_t b);
        char sanitizeChar(uint8_t ch);
    };
}
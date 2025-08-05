#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <string>

// All the #defines from original multimon-ng
#define FREQ_SAMP            22050
#define FILTLEN              1
#define REPORT_GROUP_CODES   1       // Report each cleared faulty group capcode : 0 = Each on a new line; 1 = All on the same line;

#define FLEX_SYNC_MARKER     0xA6C6AAAAul  // Synchronisation code marker for FLEX
#define SLICE_THRESHOLD      0.667         // For 4 level code, levels 0 and 3 have 3 times the amplitude of levels 1 and 2, so quantise at 2/3
#define DC_OFFSET_FILTER     0.010         // DC Offset removal IIR filter response (seconds)
#define PHASE_LOCKED_RATE    0.045         // Correction factor for locked state
#define PHASE_UNLOCKED_RATE  0.050         // Correction factor for unlocked state
#define LOCK_LEN             24            // Number of symbols to check for phase locking (max 32)
#define IDLE_THRESHOLD       0             // Number of idle codewords allowed in data section
#define CAPCODES_INDEX       0
#define DEMOD_TIMEOUT        100           // Maximum number of periods with no zero crossings before we decide that the system is not longer within a Timing lock.
#define GROUP_BITS           17            // Centralized maximum of group msg cache
#define PHASE_WORDS          88            // per spec, there are 88 4B words per frame
#define MAX_ALN              512           // max possible ALN characters

// Forward declaration for BCH error correction
struct BCHCode;

// FLEX Page Type Enumeration
enum Flex_PageTypeEnum {
    FLEX_PAGETYPE_SECURE,
    FLEX_PAGETYPE_SHORT_INSTRUCTION,
    FLEX_PAGETYPE_TONE,
    FLEX_PAGETYPE_STANDARD_NUMERIC,
    FLEX_PAGETYPE_SPECIAL_NUMERIC,
    FLEX_PAGETYPE_ALPHANUMERIC,
    FLEX_PAGETYPE_BINARY,
    FLEX_PAGETYPE_NUMBERED_NUMERIC
};

// FLEX State Machine Enumeration
enum Flex_StateEnum {
    FLEX_STATE_SYNC1,
    FLEX_STATE_FIW,
    FLEX_STATE_SYNC2,
    FLEX_STATE_DATA
};

// FLEX Demodulator Structure
struct Flex_Demodulator {
    unsigned int sample_freq;
    double sample_last;
    int locked;
    int phase;
    unsigned int sample_count;
    unsigned int symbol_count;
    double envelope_sum;
    int envelope_count;
    uint64_t lock_buf;
    int symcount[4];
    int timeout;
    int nonconsec;
    unsigned int baud;          // Current baud rate
};

// FLEX Group Message Handler Structure
struct Flex_GroupHandler {
    int64_t GroupCodes[GROUP_BITS][1000];
    int GroupCycle[GROUP_BITS];
    int GroupFrame[GROUP_BITS];
};

// FLEX Modulation Parameters Structure
struct Flex_Modulation {
    double symbol_rate;
    double envelope;
    double zero;
};

// FLEX State Machine Structure
struct Flex_State {
    unsigned int sync2_count;
    unsigned int data_count;
    unsigned int fiwcount;
    enum Flex_StateEnum Current;
    enum Flex_StateEnum Previous;
};

// FLEX Synchronization Structure
struct Flex_Sync {
    unsigned int sync;          // Outer synchronization code
    unsigned int baud;          // Baudrate of SYNC2 and DATA
    unsigned int levels;        // FSK encoding of SYNC2 and DATA
    unsigned int polarity;      // 0=Positive (Normal) 1=Negative (Inverted)
    uint64_t syncbuf;
};

// FLEX Frame Information Word Structure
struct Flex_FIW {
    unsigned int rawdata;
    unsigned int checksum;
    unsigned int cycleno;
    unsigned int frameno;
    unsigned int fix3;
};

// FLEX Phase Data Structure
struct Flex_Phase {
    unsigned int buf[PHASE_WORDS];
    int idle_count;
};

// FLEX Data Structure (contains all phases)
struct Flex_Data {
    int phase_toggle;
    unsigned int data_bit_counter;
    struct Flex_Phase PhaseA;
    struct Flex_Phase PhaseB;
    struct Flex_Phase PhaseC;
    struct Flex_Phase PhaseD;
};

// FLEX Decode Structure
struct Flex_Decode {
    enum Flex_PageTypeEnum type;
    int long_address;
    int64_t capcode;
    struct BCHCode* BCHCode;
};

// Main FLEX Structure (contains all components)
struct Flex_Next {
    struct Flex_Demodulator Demodulator;
    struct Flex_Modulation Modulation;
    struct Flex_State State;
    struct Flex_Sync Sync;
    struct Flex_FIW FIW;
    struct Flex_Data Data;
    struct Flex_Decode Decode;
    struct Flex_GroupHandler GroupHandler;
};

// FLEX Next Decoder C++ Class
class FLEXNextDecoder {
private:
    Flex_Next* flexState;
    bool initialized;

public:
    // Constructor/Destructor
    FLEXNextDecoder();
    ~FLEXNextDecoder();

    // Main interface methods
    bool initialize();
    void deinitialize();
    void processAudioSamples(const float* buffer, int length);
    bool isInitialized() const;

    // Optional: Access to internal state for debugging
    const Flex_Next* getFlexState() const { return flexState; }
};

// Function declarations for core FLEX processing
Flex_Next* Flex_New(unsigned int sampleFrequency);
void Flex_Delete(Flex_Next* flex);
void Flex_Demodulate(Flex_Next* flex, double sample);


// Helper function declarations
static unsigned int count_bits(struct Flex_Next* flex, unsigned int data);
static int bch3121_fix_errors(struct Flex_Next* flex, uint32_t* data_to_fix, char PhaseNo);
static unsigned int flex_sync_check(struct Flex_Next* flex, uint64_t buf);
static unsigned int flex_sync(struct Flex_Next* flex, unsigned char sym);
static void decode_mode(struct Flex_Next* flex, unsigned int sync_code);
static void read_2fsk(struct Flex_Next* flex, unsigned int sym, unsigned int* dat);
static int decode_fiw(struct Flex_Next* flex);
static void parse_alphanumeric(struct Flex_Next* flex, unsigned int* phaseptr, unsigned int mw1, unsigned int len, int frag, int cont, int flex_groupmessage, int flex_groupbit);
static void parse_numeric(struct Flex_Next* flex, unsigned int* phaseptr, int j);
static void parse_tone_only(struct Flex_Next* flex, unsigned int* phaseptr, int j);
static void parse_binary(struct Flex_Next* flex, unsigned int* phaseptr, unsigned int mw1, unsigned int len);
static void decode_phase(struct Flex_Next* flex, char PhaseNo);
static void clear_phase_data(struct Flex_Next* flex);
static void decode_data(struct Flex_Next* flex);
static int read_data(struct Flex_Next* flex, unsigned char sym);
static void report_state(struct Flex_Next* flex);
static void flex_sym(struct Flex_Next* flex, unsigned char sym);
static int buildSymbol(struct Flex_Next* flex, double sample);

// Utility functions
static unsigned int add_ch(unsigned char ch, unsigned char* buf, unsigned int idx);

// Page type check helper functions
static int is_alphanumeric_page(struct Flex_Next* flex);
static int is_numeric_page(struct Flex_Next* flex);
static int is_tone_page(struct Flex_Next* flex);
static int is_binary_page(struct Flex_Next* flex);

// Simple C++ wrapper around your converted C code
class FlexDecoderWrapper {
public:
    FlexDecoderWrapper();
    ~FlexDecoderWrapper();

    void processSample(float sample);
    void setMessageCallback(std::function<void(int64_t, int, const std::string&)> callback);
    void reset();

private:
    // Your actual C struct/data from converted flex_next code
    void* flex_state;  // Points to your converted Flex_Next struct
    std::function<void(int64_t, int, const std::string&)> messageCallback;

    // Static callback bridge for C code
    static void staticMessageCallback(int64_t addr, int type, const char* data, void* userdata);
};

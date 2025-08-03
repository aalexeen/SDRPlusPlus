#include "flex_next.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstdarg>  // For va_list, va_start, va_end
#define __STDC_FORMAT_MACROS
#include <inttypes.h>  // For PRId64 format specifier

// Logging function (replace multimon-ng's verbprintf)
static void verbprintf(int level, const char* format, ...) {
    // Placeholder - in SDR++ you might want to use flog::debug, flog::info, etc.
    if (level <= 2) {  // Only show important messages for now
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

//=============================================================================
// FLEX_NEW - Constructor Function
//=============================================================================
Flex_Next* Flex_New(unsigned int sampleFrequency) {
    // Allocate memory for main FLEX structure
    Flex_Next* flex = (Flex_Next*)malloc(sizeof(Flex_Next));
    if (flex == nullptr) {
        return nullptr;
    }

    // Initialize all memory to zero
    memset(flex, 0, sizeof(Flex_Next));

    // Set up demodulator parameters
    flex->Demodulator.sample_freq = sampleFrequency;
    // The baud rate of first syncword and FIW is always 1600, so set that rate to start
    flex->Demodulator.baud = 1600;

    // Initialize BCH error correction code
    // Generator polynomial for BCH3121 Code
    int p[6];
    p[0] = p[2] = p[5] = 1;
    p[1] = p[3] = p[4] = 0;

    flex->Decode.BCHCode = BCHCode_New(p, 5, 31, 21, 2);
    if (flex->Decode.BCHCode == nullptr) {
        Flex_Delete(flex);
        return nullptr;
    }

    // Initialize group handler - set all frames and cycles to -1 (invalid)
    for (int g = 0; g < GROUP_BITS; g++) {
        flex->GroupHandler.GroupFrame[g] = -1;
        flex->GroupHandler.GroupCycle[g] = -1;
    }

    verbprintf(2, "FLEX_NEXT: Initialized for %u Hz sample rate\n", sampleFrequency);
    return flex;
}

//=============================================================================
// FLEX_DELETE - Destructor Function
//=============================================================================
void Flex_Delete(Flex_Next* flex) {
    if (flex == nullptr) return;

    // Clean up BCH error correction code
    if (flex->Decode.BCHCode != nullptr) {
        BCHCode_Delete(flex->Decode.BCHCode);
        flex->Decode.BCHCode = nullptr;
    }

    verbprintf(2, "FLEX_NEXT: Cleaned up and deleted\n");

    // Free the main structure
    free(flex);
}

//=============================================================================
// FLEX_DEMODULATE - Main Processing Function
//=============================================================================
void Flex_Demodulate(Flex_Next* flex, double sample) {
    if (flex == nullptr) return;

    // Process the sample through symbol building
    // buildSymbol returns 1 when a complete symbol period has elapsed
    if (buildSymbol(flex, sample) == 1) {
        // Reset non-consecutive zero crossing counter
        flex->Demodulator.nonconsec = 0;
        flex->Demodulator.symbol_count++;

        // Calculate current symbol rate
        flex->Modulation.symbol_rate = 1.0 * flex->Demodulator.symbol_count *
                                      flex->Demodulator.sample_freq /
                                      flex->Demodulator.sample_count;

        // Determine the modal (most frequent) symbol from the 4 possible levels
        int decmax = 0;
        int modal_symbol = 0;
        for (int j = 0; j < 4; j++) {
            if (flex->Demodulator.symcount[j] > decmax) {
                modal_symbol = j;
                decmax = flex->Demodulator.symcount[j];
            }
        }

        // Clear symbol counters for next symbol period
        flex->Demodulator.symcount[0] = 0;
        flex->Demodulator.symcount[1] = 0;
        flex->Demodulator.symcount[2] = 0;
        flex->Demodulator.symcount[3] = 0;

        if (flex->Demodulator.locked) {
            // We have symbol lock - process the symbol through FLEX decoder
            flex_sym(flex, modal_symbol);
        }
        else {
            // No lock yet - check for lock pattern
            // Shift symbols into buffer, symbols are converted so that
            // the max and min symbols map to 1 and 2 (each contain a single 1)
            flex->Demodulator.lock_buf = (flex->Demodulator.lock_buf << 2) | (modal_symbol ^ 0x1);

            uint64_t lock_pattern = flex->Demodulator.lock_buf ^ 0x6666666666666666ull;
            uint64_t lock_mask = (1ull << (2 * LOCK_LEN)) - 1;

            if ((lock_pattern & lock_mask) == 0 || ((~lock_pattern) & lock_mask) == 0) {
                verbprintf(1, "FLEX_NEXT: Locked\n");
                flex->Demodulator.locked = 1;

                // Clear the synchronization buffer and reset counters
                flex->Demodulator.lock_buf = 0;
                flex->Demodulator.symbol_count = 0;
                flex->Demodulator.sample_count = 0;
            }
        }

        // Timeout handling - lose lock after too many periods with no zero crossings
        flex->Demodulator.timeout++;
        if (flex->Demodulator.timeout > DEMOD_TIMEOUT) {
            verbprintf(1, "FLEX_NEXT: Timeout\n");
            flex->Demodulator.locked = 0;
        }
    }

    // Report state changes for debugging
    report_state(flex);
}

//=============================================================================
// C++ CLASS IMPLEMENTATION
//=============================================================================

FLEXNextDecoder::FLEXNextDecoder() : flexState(nullptr), initialized(false) {
    // Constructor body
}

FLEXNextDecoder::~FLEXNextDecoder() {
    deinitialize();
}

bool FLEXNextDecoder::initialize() {
    if (initialized) return true;

    flexState = Flex_New(FREQ_SAMP);
    if (flexState == nullptr) {
        return false;
    }

    initialized = true;
    return true;
}

void FLEXNextDecoder::deinitialize() {
    if (!initialized) return;

    if (flexState != nullptr) {
        Flex_Delete(flexState);
        flexState = nullptr;
    }

    initialized = false;
}

void FLEXNextDecoder::processAudioSamples(const float* buffer, int length) {
    if (!initialized || flexState == nullptr) return;

    for (int i = 0; i < length; i++) {
        Flex_Demodulate(flexState, static_cast<double>(buffer[i]));
    }
}

bool FLEXNextDecoder::isInitialized() const {
    return initialized;
}

//=============================================================================
// PLACEHOLDER FUNCTIONS (to be implemented next)
//=============================================================================

// BCH Error Correction Placeholders
struct BCHCode* BCHCode_New(int* p, int t, int n, int k, int d) {
    // Placeholder - will implement BCH error correction later
    verbprintf(3, "FLEX_NEXT: BCHCode_New placeholder called\n");
    return (struct BCHCode*)malloc(1); // Dummy allocation
}

void BCHCode_Delete(struct BCHCode* bch) {
    // Placeholder - will implement BCH cleanup later
    if (bch) free(bch);
}

int BCHCode_Decode(struct BCHCode* bch, int* recd) {
    // Placeholder - will implement BCH decoding later
    return 0; // Return no errors for now
}

// Core processing function - Symbol Building and Timing Recovery
static int buildSymbol(struct Flex_Next* flex, double sample) {
    if (flex == nullptr) return 0;

    // Calculate phase parameters for symbol timing
    const int64_t phase_max = 100 * flex->Demodulator.sample_freq;  // Maximum value for phase
    const int64_t phase_rate = phase_max * flex->Demodulator.baud / flex->Demodulator.sample_freq;  // Increment per sample
    const double phasepercent = 100.0 * flex->Demodulator.phase / phase_max;  // Current phase as percentage

    // Update the sample counter
    flex->Demodulator.sample_count++;

    // Remove DC offset (FIR filter) - only during sync acquisition
    if (flex->State.Current == FLEX_STATE_SYNC1) {
        flex->Modulation.zero = (flex->Modulation.zero * (FREQ_SAMP * DC_OFFSET_FILTER) + sample) /
                               ((FREQ_SAMP * DC_OFFSET_FILTER) + 1);
    }
    sample -= flex->Modulation.zero;

    if (flex->Demodulator.locked) {
        // During the synchronization period, establish the envelope of the signal
        if (flex->State.Current == FLEX_STATE_SYNC1) {
            flex->Demodulator.envelope_sum += fabs(sample);
            flex->Demodulator.envelope_count++;
            flex->Modulation.envelope = flex->Demodulator.envelope_sum / flex->Demodulator.envelope_count;
        }
    }
    else {
        // Reset and hold in initial state when not locked
        flex->Modulation.envelope = 0;
        flex->Demodulator.envelope_sum = 0;
        flex->Demodulator.envelope_count = 0;
        flex->Demodulator.baud = 1600;
        flex->Demodulator.timeout = 0;
        flex->Demodulator.nonconsec = 0;
        flex->State.Current = FLEX_STATE_SYNC1;
    }

    // MID 80% OF SYMBOL PERIOD - Sample symbol levels for decision
    if (phasepercent > 10 && phasepercent < 90) {
        // Count the number of occurrences of each symbol value for analysis at end of symbol period
        // FLEX uses 4-level FSK: levels 0,1,2,3 representing different frequency shifts
        if (sample > 0) {
            if (sample > flex->Modulation.envelope * SLICE_THRESHOLD)
                flex->Demodulator.symcount[3]++;  // Highest positive level
            else
                flex->Demodulator.symcount[2]++;  // Lower positive level
        }
        else {
            if (sample < -flex->Modulation.envelope * SLICE_THRESHOLD)
                flex->Demodulator.symcount[0]++;  // Lowest negative level
            else
                flex->Demodulator.symcount[1]++;  // Higher negative level
        }
    }

    // ZERO CROSSING DETECTION - For symbol timing recovery
    if ((flex->Demodulator.sample_last < 0 && sample >= 0) ||
        (flex->Demodulator.sample_last >= 0 && sample < 0)) {

        // The phase error has a direction towards the closest symbol boundary
        double phase_error = 0.0;
        if (phasepercent < 50) {
            phase_error = flex->Demodulator.phase;
        }
        else {
            phase_error = flex->Demodulator.phase - phase_max;
        }

        // Phase lock with the signal - different rates for locked vs unlocked
        if (flex->Demodulator.locked) {
            flex->Demodulator.phase -= phase_error * PHASE_LOCKED_RATE;
        }
        else {
            flex->Demodulator.phase -= phase_error * PHASE_UNLOCKED_RATE;
        }

        // If too many zero crossings occur within the mid 80% then indicate lock has been lost
        if (phasepercent > 10 && phasepercent < 90) {
            flex->Demodulator.nonconsec++;
            if (flex->Demodulator.nonconsec > 20 && flex->Demodulator.locked) {
                verbprintf(1, "FLEX_NEXT: Synchronisation Lost\n");
                flex->Demodulator.locked = 0;
            }
        }
        else {
            flex->Demodulator.nonconsec = 0;
        }

        // Reset timeout on zero crossing (signal activity detected)
        flex->Demodulator.timeout = 0;
    }

    // Store current sample for next zero crossing detection
    flex->Demodulator.sample_last = sample;

    // END OF SYMBOL PERIOD - Advance phase accumulator
    flex->Demodulator.phase += phase_rate;

    // Check if we've completed a full symbol period
    if (flex->Demodulator.phase > phase_max) {
        flex->Demodulator.phase -= phase_max;
        return 1;  // Symbol period complete - process the accumulated symbol data
    } else {
        return 0;  // Still accumulating samples for current symbol
    }
}

// FLEX Symbol Processing - Main State Machine
static void flex_sym(struct Flex_Next* flex, unsigned char sym) {
    if (flex == nullptr) return;

    // If the signal has a negative polarity, the symbols must be inverted
    // Polarity is determined during the IDLE/sync word checking phase
    unsigned char sym_rectified;
    if (flex->Sync.polarity) {
        sym_rectified = 3 - sym;  // Invert symbol for negative polarity
    } else {
        sym_rectified = sym;      // Use symbol as-is for positive polarity
    }

    // FLEX State Machine - handles 4 main states
    switch (flex->State.Current) {
        case FLEX_STATE_SYNC1:
        {
            // Continually compare the received symbol stream against known FLEX sync words
            unsigned int sync_code = flex_sync(flex, sym); // Use unrectified symbol for sync detection

            if (sync_code != 0) {
                // Valid sync word found - decode the transmission parameters
                decode_mode(flex, sync_code);

                if (flex->Sync.baud != 0 && flex->Sync.levels != 0) {
                    // Valid mode detected - move to Frame Information Word state
                    flex->State.Current = FLEX_STATE_FIW;

                    verbprintf(2, "FLEX_NEXT: SyncInfoWord: sync_code=0x%04x baud=%i levels=%i polarity=%s zero=%f envelope=%f symrate=%f\n",
                        sync_code, flex->Sync.baud, flex->Sync.levels,
                        flex->Sync.polarity ? "NEG" : "POS",
                        flex->Modulation.zero, flex->Modulation.envelope, flex->Modulation.symbol_rate);
                } else {
                    verbprintf(2, "FLEX_NEXT: Unknown Sync code = 0x%04x\n", sync_code);
                    flex->State.Current = FLEX_STATE_SYNC1;  // Stay in sync search
                }
            } else {
                flex->State.Current = FLEX_STATE_SYNC1;  // Continue searching for sync
            }

            // Initialize FIW processing
            flex->State.fiwcount = 0;
            flex->FIW.rawdata = 0;
            break;
        }

        case FLEX_STATE_FIW:
        {
            // Skip 16 bits of dotting, then accumulate 32 bits of Frame Information Word
            // FIW contains frame timing and cycle information
            flex->State.fiwcount++;

            if (flex->State.fiwcount >= 16) {
                // After 16 bits of dotting, start collecting FIW data using 2FSK
                read_2fsk(flex, sym_rectified, &flex->FIW.rawdata);
            }

            if (flex->State.fiwcount == 48) {
                // Complete FIW received (16 bits dotting + 32 bits data)
                if (decode_fiw(flex) == 0) {
                    // FIW decoded successfully - move to second sync phase
                    flex->State.sync2_count = 0;
                    flex->Demodulator.baud = flex->Sync.baud;  // Switch to frame baud rate
                    flex->State.Current = FLEX_STATE_SYNC2;
                } else {
                    // FIW decode failed - return to sync search
                    flex->State.Current = FLEX_STATE_SYNC1;
                }
            }
            break;
        }

        case FLEX_STATE_SYNC2:
        {
            // The second SYNC header is 25ms of idle bits at the frame baud rate
            // Skip 25 ms = 40 bits @ 1600 bps, 80 bits @ 3200 bps
            if (++flex->State.sync2_count == flex->Sync.baud * 25 / 1000) {
                // Second sync period complete - prepare for data reception
                flex->State.data_count = 0;
                clear_phase_data(flex);  // Clear all phase buffers
                flex->State.Current = FLEX_STATE_DATA;
            }
            break;
        }

        case FLEX_STATE_DATA:
        {
            // The data portion of the frame is 1760 ms long at either baudrate
            // This is 2816 bits @ 1600 bps and 5632 bits @ 3200 bps
            // Symbols are decoded and distributed to the four FLEX phases (A,B,C,D)
            int idle = read_data(flex, sym_rectified);

            if (++flex->State.data_count == flex->Sync.baud * 1760 / 1000 || idle) {
                // Data section complete (time elapsed or all phases idle)
                decode_data(flex);  // Process all received phase data

                // Reset for next frame
                flex->Demodulator.baud = 1600;  // Return to sync baud rate
                flex->State.Current = FLEX_STATE_SYNC1;
                flex->State.data_count = 0;
            }
            break;
        }
    }
}

static void report_state(struct Flex_Next* flex) {
    // Report state changes for debugging
    if (flex->State.Current != flex->State.Previous) {
        flex->State.Previous = flex->State.Current;

        const char* state = "Unknown";
        switch (flex->State.Current) {
            case FLEX_STATE_SYNC1:
                state = "SYNC1";
                break;
            case FLEX_STATE_FIW:
                state = "FIW";
                break;
            case FLEX_STATE_SYNC2:
                state = "SYNC2";
                break;
            case FLEX_STATE_DATA:
                state = "DATA";
                break;
            default:
                break;
        }
        verbprintf(1, "FLEX_NEXT: State: %s\n", state);
    }
}

// Bit counting utility function
static unsigned int count_bits(struct Flex_Next* flex, unsigned int data) {
    if (flex == nullptr) return 0;

#ifdef USE_BUILTIN_POPCOUNT
    return __builtin_popcount(data);
#else
    // Software implementation for counting set bits (popcount)
    unsigned int n = (data >> 1) & 0x77777777;
    data = data - n;
    n = (n >> 1) & 0x77777777;
    data = data - n;
    n = (n >> 1) & 0x77777777;
    data = data - n;
    data = (data + (data >> 4)) & 0x0f0f0f0f;
    data = data * 0x01010101;
    return data >> 24;
#endif
}

// FLEX Sync Pattern Verification
static unsigned int flex_sync_check(struct Flex_Next* flex, uint64_t buf) {
    if (flex == nullptr) return 0;

    // 64-bit FLEX sync code structure:
    // AAAA:BBBBBBBB:CCCC
    //
    // Where BBBBBBBB is always 0xA6C6AAAA (FLEX_SYNC_MARKER)
    // and AAAA^CCCC is 0xFFFF
    //
    // Specific values of AAAA determine what bps and encoding the
    // packet uses beyond the frame information word
    //
    // First we match on the marker field with a hamming distance < 4
    // Then we match on the outer code with a hamming distance < 4

    unsigned int marker =     (buf & 0x0000FFFFFFFF0000ULL) >> 16;  // Extract middle 32 bits
    unsigned short codehigh = (buf & 0xFFFF000000000000ULL) >> 48;  // Extract upper 16 bits
    unsigned short codelow  = ~(buf & 0x000000000000FFFFULL);       // Extract lower 16 bits (inverted)

    int retval = 0;

    // Check if marker matches FLEX_SYNC_MARKER with less than 4 bit errors
    // AND if the outer code (codehigh XOR codelow inverted) has less than 4 bit errors
    if (count_bits(flex, marker ^ FLEX_SYNC_MARKER) < 4 &&
        count_bits(flex, codelow ^ codehigh) < 4) {
        retval = codehigh;  // Return the sync code identifier
    } else {
        retval = 0;         // No valid sync pattern found
    }

    return retval;
}

// FLEX Synchronization Detection - Main Function
static unsigned int flex_sync(struct Flex_Next* flex, unsigned char sym) {
    if (flex == nullptr) return 0;

    int retval = 0;

    // Shift new symbol into 64-bit sync buffer
    // Convert 4-level FSK symbol to binary: symbols 0,1 -> 1, symbols 2,3 -> 0
    flex->Sync.syncbuf = (flex->Sync.syncbuf << 1) | ((sym < 2) ? 1 : 0);

    // Check for positive (normal) polarity sync pattern
    retval = flex_sync_check(flex, flex->Sync.syncbuf);
    if (retval != 0) {
        flex->Sync.polarity = 0;  // Normal polarity detected
    } else {
        // If positive sync pattern was not found, look for negative (inverted) polarity
        retval = flex_sync_check(flex, ~flex->Sync.syncbuf);
        if (retval != 0) {
            flex->Sync.polarity = 1;  // Inverted polarity detected
        }
    }

    return retval;  // Return sync code (0 if no sync found)
}

// FLEX Mode Decoding - Determines baud rate and FSK levels from sync code
static void decode_mode(struct Flex_Next* flex, unsigned int sync_code) {
    if (flex == nullptr) return;

    // FLEX mode lookup table - maps sync codes to transmission parameters
    // Note: Some mode assignments seem unusual (comments from original multimon-ng):
    //   * Where is 6400/4?
    //   * Why are there two 3200/4?
    //   * Why is there a 1600/4?
    struct {
        int sync;            // Sync code identifier
        unsigned int baud;   // Baud rate (1600, 3200, 6400)
        unsigned int levels; // FSK levels (2 or 4)
    } flex_modes[] = {
        { 0x870C, 1600, 2 },  // 1600 baud, 2-level FSK
        { 0xB068, 1600, 4 },  // 1600 baud, 4-level FSK
        { 0x7B18, 3200, 2 },  // 3200 baud, 2-level FSK
        { 0xDEA0, 3200, 4 },  // 3200 baud, 4-level FSK
        { 0x4C7C, 3200, 4 },  // 3200 baud, 4-level FSK (alternate)
        { 0, 0, 0 }           // Terminator
    };

    bool mode_found = false;

    // Search through the mode table for a matching sync code
    for (int i = 0; flex_modes[i].sync != 0; i++) {
        // Allow up to 4 bit errors in sync code matching (same as sync detection)
        if (count_bits(flex, flex_modes[i].sync ^ sync_code) < 4) {
            // Mode found - set transmission parameters
            flex->Sync.sync = sync_code;
            flex->Sync.baud = flex_modes[i].baud;
            flex->Sync.levels = flex_modes[i].levels;
            mode_found = true;

            verbprintf(2, "FLEX_NEXT: Mode detected - %u baud, %u-level FSK\n",
                      flex->Sync.baud, flex->Sync.levels);
            break;
        }
    }

    // If no valid mode found, default to most common FLEX mode
    if (!mode_found) {
        verbprintf(3, "FLEX_NEXT: Sync Code 0x%04x not found, defaulting to 1600bps 2FSK\n", sync_code);
        flex->Sync.sync = sync_code;
        flex->Sync.baud = 1600;    // Default baud rate
        flex->Sync.levels = 2;     // Default to 2-level FSK
    }
}

// 2FSK Data Reading - Converts FSK symbols to binary data
static void read_2fsk(struct Flex_Next* flex, unsigned int sym, unsigned int* dat) {
    if (flex == nullptr) return;

    // Convert 4-level FSK symbol to binary bit:
    // Symbols 0,1 -> binary 0
    // Symbols 2,3 -> binary 1
    //
    // Shift the data word right by 1 bit and insert new bit at MSB
    *dat = (*dat >> 1) | ((sym > 1) ? 0x80000000 : 0);
}

// BCH Error Correction for FIW data
static int bch3121_fix_errors(struct Flex_Next* flex, uint32_t* data_to_fix, char PhaseNo) {
    if (flex == nullptr) return -1;

    int recd[31];

    // Convert the data pattern into an array of coefficients
    unsigned int data = *data_to_fix;
    for (int i = 0; i < 31; i++) {
        recd[i] = (data >> 30) & 1;
        data <<= 1;
    }

    // Decode and correct the coefficients using BCH
    int decode_error = BCHCode_Decode(flex->Decode.BCHCode, recd);

    // Decode successful?
    if (!decode_error) {
        // Convert the coefficient array back to a bit pattern
        data = 0;
        for (int i = 0; i < 31; i++) {
            data <<= 1;
            data |= recd[i];
        }

        // Count the number of fixed errors
        int fixed = count_bits(flex, (*data_to_fix & 0x7FFFFFFF) ^ data);
        if (fixed > 0) {
            verbprintf(3, "FLEX_NEXT: Phase %c Fixed %i errors @ 0x%08x  (0x%08x -> 0x%08x)\n",
                      PhaseNo, fixed, (*data_to_fix & 0x7FFFFFFF) ^ data,
                      (*data_to_fix & 0x7FFFFFFF), data);
        }

        // Write the fixed data back to the caller
        *data_to_fix = data;
    } else {
        verbprintf(3, "FLEX_NEXT: Phase %c Data corruption - Unable to fix errors.\n", PhaseNo);
    }

    return decode_error;
}

// Frame Information Word Decoding
static int decode_fiw(struct Flex_Next* flex) {
    if (flex == nullptr) return -1;

    unsigned int fiw = flex->FIW.rawdata;

    // Apply BCH error correction to the FIW
    int decode_error = bch3121_fix_errors(flex, &fiw, 'F');

    if (decode_error) {
        verbprintf(3, "FLEX_NEXT: Unable to decode FIW, too much data corruption\n");
        return 1;
    }

    // Extract fields from the corrected FIW
    // The only relevant bits in the FIW word are those masked by 0x001FFFFF
    flex->FIW.checksum = fiw & 0xF;           // Bits 0-3: Checksum
    flex->FIW.cycleno = (fiw >> 4) & 0xF;     // Bits 4-7: Cycle number (0-15)
    flex->FIW.frameno = (fiw >> 8) & 0x7F;    // Bits 8-14: Frame number (0-127)
    flex->FIW.fix3 = (fiw >> 15) & 0x3F;      // Bits 15-20: Reserved field

    // Calculate and verify checksum
    unsigned int checksum = (fiw & 0xF);         // Bits 0-3
    checksum += ((fiw >> 4) & 0xF);              // Bits 4-7
    checksum += ((fiw >> 8) & 0xF);              // Bits 8-11
    checksum += ((fiw >> 12) & 0xF);             // Bits 12-15
    checksum += ((fiw >> 16) & 0xF);             // Bits 16-19
    checksum += ((fiw >> 20) & 0x01);            // Bit 20

    checksum &= 0xF;  // Keep only lower 4 bits

    if (checksum == 0xF) {
        // Checksum is valid - calculate and display timing information
        int timeseconds = flex->FIW.cycleno * 4 * 60 + flex->FIW.frameno * 4 * 60 / 128;

        verbprintf(2, "FLEX_NEXT: FrameInfoWord: cycleno=%02i frameno=%03i fix3=0x%02x time=%02i:%02i\n",
                  flex->FIW.cycleno, flex->FIW.frameno, flex->FIW.fix3,
                  timeseconds / 60, timeseconds % 60);

        // Check for missed group messages and clean up if necessary
        for (int g = 0; g < GROUP_BITS; g++) {
            // Do we have a group message pending for this groupbit?
            if (flex->GroupHandler.GroupFrame[g] >= 0) {
                int Reset = 0;

                verbprintf(4, "FLEX_NEXT: GroupBit %i, FrameNo: %i, Cycle No: %i target Cycle No: %i\n",
                          g, flex->GroupHandler.GroupFrame[g], flex->GroupHandler.GroupCycle[g],
                          (int)flex->FIW.cycleno);

                // Check if message was expected in this frame
                if ((int)flex->FIW.cycleno == flex->GroupHandler.GroupCycle[g]) {
                    if (flex->GroupHandler.GroupFrame[g] < (int)flex->FIW.frameno) {
                        Reset = 1;
                    }
                }
                // Check if we should have sent a group message in the previous cycle
                else if (flex->FIW.cycleno == 0) {
                    if (flex->GroupHandler.GroupCycle[g] == 15) {
                        Reset = 1;
                    }
                }
                // If we are waiting for the cycle to roll over then continue
                else if (flex->FIW.cycleno == 15 && flex->GroupHandler.GroupCycle[g] == 0) {
                    continue;
                }
                // Otherwise if the target cycle is less than the current cycle, reset the data
                else if (flex->GroupHandler.GroupCycle[g] < (int)flex->FIW.cycleno) {
                    Reset = 1;
                }

                if (Reset == 1) {
                    // Report missed group messages
                    int endpoint = flex->GroupHandler.GroupCodes[g][CAPCODES_INDEX];

                    if (REPORT_GROUP_CODES > 0) {
                        verbprintf(3, "FLEX_NEXT: Group messages seem to have been missed; Groupbit: %i; Total Capcodes: %i; Clearing Data; Capcodes: ",
                                  g, endpoint);
                    }

                    for (int capIndex = 1; capIndex <= endpoint; capIndex++) {
                        if (REPORT_GROUP_CODES == 0) {
                            verbprintf(3, "FLEX_NEXT: Group messages seem to have been missed; Groupbit: %i; Clearing data; Capcode: [%010" PRId64 "]\n",
                                      g, flex->GroupHandler.GroupCodes[g][capIndex]);
                        } else {
                            if (capIndex > 1) {
                                verbprintf(3, ",");
                            }
                            verbprintf(3, "[%010" PRId64 "]", flex->GroupHandler.GroupCodes[g][capIndex]);
                        }
                    }

                    if (REPORT_GROUP_CODES > 0) {
                        verbprintf(3, "\n");
                    }

                    // Reset the group message data
                    flex->GroupHandler.GroupCodes[g][CAPCODES_INDEX] = 0;
                    flex->GroupHandler.GroupFrame[g] = -1;
                    flex->GroupHandler.GroupCycle[g] = -1;
                }
            }
        }

        return 0;  // Success
    } else {
        verbprintf(3, "FLEX_NEXT: Bad Checksum 0x%x\n", checksum);
        return 1;  // Checksum failure
    }
}

// Clear Phase Data - Initialize all phase buffers for data collection
static void clear_phase_data(struct Flex_Next* flex) {
    if (flex == nullptr) return;

    // Clear all phase data buffers (4 phases × 88 words each)
    for (int i = 0; i < PHASE_WORDS; i++) {
        flex->Data.PhaseA.buf[i] = 0;
        flex->Data.PhaseB.buf[i] = 0;
        flex->Data.PhaseC.buf[i] = 0;
        flex->Data.PhaseD.buf[i] = 0;
    }

    // Reset idle counters for each phase
    flex->Data.PhaseA.idle_count = 0;
    flex->Data.PhaseB.idle_count = 0;
    flex->Data.PhaseC.idle_count = 0;
    flex->Data.PhaseD.idle_count = 0;

    // Reset data collection state
    flex->Data.phase_toggle = 0;        // Start with first phase pair (A/B)
    flex->Data.data_bit_counter = 0;    // Reset bit position counter

    verbprintf(3, "FLEX_NEXT: Phase data buffers cleared and initialized\n");
}

// FLEX Data Reading - Distributes symbols across phases with interleaving
static int read_data(struct Flex_Next* flex, unsigned char sym) {
    if (flex == nullptr) return -1;

    // FLEX can transmit the data portion of the frame at either
    // 1600 bps or 3200 bps, and can use either two- or four-level
    // FSK encoding.
    //
    // At 1600 bps, 2-level, a single "phase" is transmitted with bit
    // value '0' using level '3' and bit value '1' using level '0'.
    //
    // At 1600 bps, 4-level, a second "phase" is transmitted, and the
    // di-bits are encoded with a gray code:
    //
    // Symbol Phase A  Phase B
    // ------   -------  -------
    //   0         1        1
    //   1         1        0
    //   2         0        0
    //   3         0        1
    //
    // At 1600 bps, 4-level, these are called PHASE A and PHASE B.
    //
    // At 3200 bps, the same 1 or 2 bit encoding occurs, except that
    // additionally two streams are interleaved on alternating symbols.
    // Thus, PHASE A (and PHASE B if 4-level) are decoded on one symbol,
    // then PHASE C (and PHASE D if 4-level) are decoded on the next.

    // Convert FSK symbol to phase bits
    int bit_a = 0;  // Received data bit for Phase A
    int bit_b = 0;  // Received data bit for Phase B

    // Phase A: symbols 0,1 -> 1, symbols 2,3 -> 0
    bit_a = (sym > 1);

    // Phase B (only for 4-level FSK): Gray code decoding
    if (flex->Sync.levels == 4) {
        bit_b = (sym == 1) || (sym == 2);
    }

    // For 1600 baud, don't toggle phases (use same phases each symbol)
    if (flex->Sync.baud == 1600) {
        flex->Data.phase_toggle = 0;
    }

    // Calculate data word index with deinterleaving
    // Bits 0, 1, and 2 map straight through to give a 0-7 sequence that repeats 32 times
    // before moving to 8-15 repeating 32 times, etc.
    unsigned int idx = ((flex->Data.data_bit_counter >> 5) & 0xFFF8) |
                       (flex->Data.data_bit_counter & 0x0007);

    if (flex->Data.phase_toggle == 0) {
        // Process Phase A and Phase B
        flex->Data.PhaseA.buf[idx] = (flex->Data.PhaseA.buf[idx] >> 1) |
                                     (bit_a ? 0x80000000 : 0);
        flex->Data.PhaseB.buf[idx] = (flex->Data.PhaseB.buf[idx] >> 1) |
                                     (bit_b ? 0x80000000 : 0);

        flex->Data.phase_toggle = 1;

        // Check for idle words at word boundaries
        if ((flex->Data.data_bit_counter & 0xFF) == 0xFF) {
            if (flex->Data.PhaseA.buf[idx] == 0x00000000 ||
                flex->Data.PhaseA.buf[idx] == 0xffffffff) {
                flex->Data.PhaseA.idle_count++;
            }
            if (flex->Data.PhaseB.buf[idx] == 0x00000000 ||
                flex->Data.PhaseB.buf[idx] == 0xffffffff) {
                flex->Data.PhaseB.idle_count++;
            }
        }
    } else {
        // Process Phase C and Phase D
        flex->Data.PhaseC.buf[idx] = (flex->Data.PhaseC.buf[idx] >> 1) |
                                     (bit_a ? 0x80000000 : 0);
        flex->Data.PhaseD.buf[idx] = (flex->Data.PhaseD.buf[idx] >> 1) |
                                     (bit_b ? 0x80000000 : 0);

        flex->Data.phase_toggle = 0;

        // Check for idle words at word boundaries
        if ((flex->Data.data_bit_counter & 0xFF) == 0xFF) {
            if (flex->Data.PhaseC.buf[idx] == 0x00000000 ||
                flex->Data.PhaseC.buf[idx] == 0xffffffff) {
                flex->Data.PhaseC.idle_count++;
            }
            if (flex->Data.PhaseD.buf[idx] == 0x00000000 ||
                flex->Data.PhaseD.buf[idx] == 0xffffffff) {
                flex->Data.PhaseD.idle_count++;
            }
        }
    }

    // Increment bit counter when appropriate
    if (flex->Sync.baud == 1600 || flex->Data.phase_toggle == 0) {
        flex->Data.data_bit_counter++;
    }

    // Check if all active phases have gone idle
    int idle = 0;
    if (flex->Sync.baud == 1600) {
        if (flex->Sync.levels == 2) {
            // 1600 baud, 2-level: only Phase A is active
            idle = (flex->Data.PhaseA.idle_count > IDLE_THRESHOLD);
        } else {
            // 1600 baud, 4-level: Phase A and Phase B are active
            idle = ((flex->Data.PhaseA.idle_count > IDLE_THRESHOLD) &&
                    (flex->Data.PhaseB.idle_count > IDLE_THRESHOLD));
        }
    } else {
        // 3200 baud
        if (flex->Sync.levels == 2) {
            // 3200 baud, 2-level: Phase A and Phase C are active
            idle = ((flex->Data.PhaseA.idle_count > IDLE_THRESHOLD) &&
                    (flex->Data.PhaseC.idle_count > IDLE_THRESHOLD));
        } else {
            // 3200 baud, 4-level: all phases are active
            idle = ((flex->Data.PhaseA.idle_count > IDLE_THRESHOLD) &&
                    (flex->Data.PhaseB.idle_count > IDLE_THRESHOLD) &&
                    (flex->Data.PhaseC.idle_count > IDLE_THRESHOLD) &&
                    (flex->Data.PhaseD.idle_count > IDLE_THRESHOLD));
        }
    }

    return idle;  // Return 1 if all active phases are idle, 0 otherwise
}

// FLEX Data Decoding - Process all collected phase data
static void decode_data(struct Flex_Next* flex) {
    if (flex == nullptr) return;

    verbprintf(3, "FLEX_NEXT: Decoding data for %u baud, %u-level FSK\n",
              flex->Sync.baud, flex->Sync.levels);

    // Process phases based on transmission mode
    if (flex->Sync.baud == 1600) {
        if (flex->Sync.levels == 2) {
            // 1600 baud, 2-level FSK: Only Phase A is used
            decode_phase(flex, 'A');
        } else {
            // 1600 baud, 4-level FSK: Phase A and Phase B are used
            decode_phase(flex, 'A');
            decode_phase(flex, 'B');
        }
    } else {
        // 3200 baud modes
        if (flex->Sync.levels == 2) {
            // 3200 baud, 2-level FSK: Phase A and Phase C are used (interleaved)
            decode_phase(flex, 'A');
            decode_phase(flex, 'C');
        } else {
            // 3200 baud, 4-level FSK: All phases are used
            decode_phase(flex, 'A');
            decode_phase(flex, 'B');
            decode_phase(flex, 'C');
            decode_phase(flex, 'D');
        }
    }

    verbprintf(3, "FLEX_NEXT: Data decoding complete\n");
}

//=============================================================================
// FLEX PHASE DECODING - Main Message Processing Function
//=============================================================================

static void decode_phase(struct Flex_Next* flex, char PhaseNo) {
    if (flex == nullptr) return;

    verbprintf(3, "FLEX_NEXT: Decoding phase %c\n", PhaseNo);

    uint32_t* phaseptr = nullptr;

    // Select the appropriate phase buffer based on phase number
    switch (PhaseNo) {
        case 'A': phaseptr = flex->Data.PhaseA.buf; break;
        case 'B': phaseptr = flex->Data.PhaseB.buf; break;
        case 'C': phaseptr = flex->Data.PhaseC.buf; break;
        case 'D': phaseptr = flex->Data.PhaseD.buf; break;
        default:
            verbprintf(3, "FLEX_NEXT: Invalid phase number %c\n", PhaseNo);
            return;
    }

    // Apply BCH error correction to each data word in the phase buffer
    for (unsigned int i = 0; i < PHASE_WORDS; i++) {
        int decode_error = bch3121_fix_errors(flex, &phaseptr[i], PhaseNo);

        if (decode_error) {
            verbprintf(3, "FLEX_NEXT: Garbled message at block %u\n", i);
            // If the previous frame was a short message then we need to null out the Group Message pointer
            // This issue and suggested resolution was presented by 'bertinholland'
            return;
        }

        // Extract just the message bits (mask out unused upper bits)
        phaseptr[i] &= 0x1FFFFF;
    }

    // Block Information Word is the first data word in frame
    uint32_t biw = phaseptr[0];

    // Check for empty or invalid BIW
    if (biw == 0 || (biw & 0x1FFFFF) == 0x1FFFFF) {
        verbprintf(3, "FLEX_NEXT: Nothing to see here, please move along\n");
        return;
    }

    // Extract BIW fields:
    // Address start offset is bits 9-8, plus one for offset (to account for biw)
    unsigned int aoffset = ((biw >> 8) & 0x3) + 1;
    // Vector start index is bits 15-10
    unsigned int voffset = (biw >> 10) & 0x3F;

    // Validate BIW structure
    if (voffset < aoffset) {
        verbprintf(3, "FLEX_NEXT: Invalid biw");
        return;
    }

    // Long addresses use double AW and VW, so there are anywhere between ceil(v-a/2) to v-a pages in this frame
    verbprintf(3, "FLEX_NEXT: BlockInfoWord: (Phase %c) BIW:%08X AW %02u VW %02u (up to %u pages)\n",
              PhaseNo, biw, aoffset, voffset, voffset - aoffset);

    int flex_groupmessage = 0;
    int flex_groupbit = 0;

    // Iterate through pages and dispatch to appropriate handler
    for (unsigned int i = aoffset; i < voffset; i++) {
        verbprintf(3, "FLEX_NEXT: Processing page offset #%u AW:%08X VW:%08X\n",
                  i - aoffset + 1, phaseptr[i], phaseptr[voffset + i - aoffset]);

        // Check for idle/invalid address words
        if (phaseptr[i] == 0 || (phaseptr[i] & 0x1FFFFF) == 0x1FFFFF) {
            verbprintf(3, "FLEX_NEXT: Idle codewords, invalid address\n");
            continue;
        }

        /*********************
         * Parse Address Word (AW)
         */
        uint32_t aiw = phaseptr[i];

        // Determine if this is a long address based on specific ranges
        flex->Decode.long_address = (aiw < 0x8001L) ||
                                   (aiw > 0x1E0000L && aiw < 0x1F0001L) ||
                                   (aiw > 0x1F7FFEL);

        // Calculate capcode based on address type
        flex->Decode.capcode = aiw - 0x8000L;  // Short address calculation
        if (flex->Decode.long_address) {
            // Long address calculation - couldn't find spec on this, credit to PDW
            flex->Decode.capcode = phaseptr[i + 1] ^ 0x1FFFFFL;
            // 0x8000 or 32768 is 16b, use as upper part of 64b capcode
            flex->Decode.capcode = flex->Decode.capcode << 15;
            // Add in 2068480 and first word, credit to PDW
            // NOTE per PDW: this is not number given (2067456) in the patent for FLEX
            flex->Decode.capcode += 2068480L + aiw;
        }

        // Validate capcode range
        if (flex->Decode.capcode > 4297068542LL || flex->Decode.capcode < 0) {
            // Invalid address (by spec, maximum address)
            verbprintf(3, "FLEX_NEXT: Invalid address, capcode out of range %" PRId64 "\n", flex->Decode.capcode);
            continue;
        }

        verbprintf(3, "FLEX_NEXT: CAPCODE:%016" PRIx64 " %" PRId64 "\n", flex->Decode.capcode, flex->Decode.capcode);

        // Check for group messaging
        flex_groupmessage = 0;
        flex_groupbit = 0;
        if ((flex->Decode.capcode >= 2029568) && (flex->Decode.capcode <= 2029583)) {
            flex_groupmessage = 1;
            flex_groupbit = flex->Decode.capcode - 2029568;
            if (flex_groupbit < 0) continue;
        }

        if (flex_groupmessage && flex->Decode.long_address) {
            // Invalid (by spec)
            verbprintf(3, "FLEX_NEXT: Don't process group messages if a long address\n");
            return;
        }

        verbprintf(3, "FLEX_NEXT: AIW %u: capcode:%" PRId64 " long:%d group:%d groupbit:%d\n",
                  i, flex->Decode.capcode, flex->Decode.long_address, flex_groupmessage, flex_groupbit);

        /*********************
         * Parse Vector Information Word (VW)
         */
        // Parse vector information word for address @ offset 'i'
        unsigned int j = voffset + i - aoffset;    // Start of vector field for address @ i
        uint32_t viw = phaseptr[j];

        // Extract VIW fields
        flex->Decode.type = ((viw >> 4) & 0x7L);
        unsigned int mw1 = (viw >> 7) & 0x7F;
        unsigned int len = (viw >> 14) & 0x7F;
        unsigned int hdr;

        if (flex->Decode.long_address) {
            // The header is within the next VW
            hdr = j + 1;
            if (len >= 1) {
                // Per PDW
                len--;
            }
        } else {  // Short address
            // The header is within the message
            hdr = mw1;
            mw1++;
            if (!flex_groupmessage && len >= 1) {
                // Not in spec, possible decode issue, but this fixed repeatedly observed len issues
                len--;
            }
        }

        if (hdr >= PHASE_WORDS) {
            verbprintf(3, "FLEX_NEXT: Invalid VIW\n");
            continue;
        }

        // Get message fragment number (bits 11 and 12) from first header word
        // If frag != 3 then this is a continued message
        int frag = (int) (phaseptr[hdr] >> 11) & 0x3L;
        // Which spec documents a cont flag? It is used to derive the K/F/C frag_flag
        int cont = (int) (phaseptr[hdr] >> 10) & 0x1L;

        verbprintf(3, "FLEX_NEXT: VIW %u: type:%d mw1:%u len:%u frag:%i\n",
                  j, flex->Decode.type, mw1, len, frag);

        // Handle Short Instruction messages (group messaging setup)
        if (flex->Decode.type == FLEX_PAGETYPE_SHORT_INSTRUCTION) {
            unsigned int iAssignedFrame = (int)((viw >> 10) & 0x7f);  // Frame with groupmessage
            int groupbit = (int)((viw >> 17) & 0x7f);    // Listen to this groupcode

            // Add capcode to group handler
            flex->GroupHandler.GroupCodes[groupbit][CAPCODES_INDEX]++;
            int CapcodePlacement = flex->GroupHandler.GroupCodes[groupbit][CAPCODES_INDEX];
            verbprintf(1, "FLEX_NEXT: Found Short Instruction, Group bit: %i capcodes in group so far %i, adding Capcode: [%010" PRId64 "]\n",
                      groupbit, CapcodePlacement, flex->Decode.capcode);

            flex->GroupHandler.GroupCodes[groupbit][CapcodePlacement] = flex->Decode.capcode;
            flex->GroupHandler.GroupFrame[groupbit] = iAssignedFrame;

            // Calculate which cycle the message frame will be in
            if (iAssignedFrame > flex->FIW.frameno) {
                flex->GroupHandler.GroupCycle[groupbit] = (int)flex->FIW.cycleno;
                verbprintf(4, "FLEX_NEXT: Message frame is in this cycle: %i\n", flex->GroupHandler.GroupCycle[groupbit]);
            } else {
                if (flex->FIW.cycleno == 15) {
                    flex->GroupHandler.GroupCycle[groupbit] = 0;
                } else {
                    flex->GroupHandler.GroupCycle[groupbit] = (int)flex->FIW.cycleno++;
                }
                verbprintf(4, "FLEX_NEXT: Message frame is in the next cycle: %i\n", flex->GroupHandler.GroupCycle[groupbit]);
            }

            // Nothing else to do with this word.. move on!!
            continue;
        }

        // Validate message word parameters
        if (len < 1 || mw1 < (voffset + (voffset - aoffset)) || mw1 >= PHASE_WORDS) {
            verbprintf(3, "FLEX_NEXT: Invalid VIW\n");
            continue;
        }

        // Truncate length if it would exceed phase buffer
        if ((mw1 + len) > PHASE_WORDS) {
            len = PHASE_WORDS - mw1;
        }

        // For tone pages, reset message word parameters
        if (is_tone_page(flex)) {
            mw1 = len = 0;
        }

        // Output message header information
        verbprintf(0, "FLEX_NEXT|%i/%i|%02i.%03i.%c|%010" PRId64 "|%c%c|%1d|",
                  flex->Sync.baud, flex->Sync.levels, flex->FIW.cycleno, flex->FIW.frameno, PhaseNo,
                  flex->Decode.capcode, (flex->Decode.long_address ? 'L' : 'S'),
                  (flex_groupmessage ? 'G' : 'S'), flex->Decode.type);

        // Dispatch to appropriate message parser based on page type
        if (is_alphanumeric_page(flex)) {
            verbprintf(0, "ALN|");
            parse_alphanumeric(flex, phaseptr, mw1, len, frag, cont, flex_groupmessage, flex_groupbit);
        }
        else if (is_numeric_page(flex)) {
            verbprintf(0, "NUM|");
            parse_numeric(flex, phaseptr, j);
        }
        else if (is_tone_page(flex)) {
            verbprintf(0, "TON|");
            parse_tone_only(flex, phaseptr, j);
        }
        else if (is_binary_page(flex)) {
            verbprintf(0, "BIN|");
            parse_binary(flex, phaseptr, mw1, len);
        }
        else {
            verbprintf(0, "UNK|");
            parse_binary(flex, phaseptr, mw1, len);  // Default to binary output for unknown types
        }

        verbprintf(0, "\n");

        // Long addresses consume 2 AW and 2 VW, so skip the next AW-VW pair
        if (flex->Decode.long_address) {
            i++;
        }
    }
}

//=============================================================================
// PAGE TYPE HELPER FUNCTIONS
//=============================================================================

// Check if the current page is an alphanumeric page
static int is_alphanumeric_page(struct Flex_Next* flex) {
    if (flex == nullptr) return 0;

    return (flex->Decode.type == FLEX_PAGETYPE_ALPHANUMERIC ||
            flex->Decode.type == FLEX_PAGETYPE_SECURE);
}

// Check if the current page is a numeric page
static int is_numeric_page(struct Flex_Next* flex) {
    if (flex == nullptr) return 0;

    return (flex->Decode.type == FLEX_PAGETYPE_STANDARD_NUMERIC ||
            flex->Decode.type == FLEX_PAGETYPE_SPECIAL_NUMERIC ||
            flex->Decode.type == FLEX_PAGETYPE_NUMBERED_NUMERIC);
}

// Check if the current page is a tone-only page
static int is_tone_page(struct Flex_Next* flex) {
    if (flex == nullptr) return 0;

    return (flex->Decode.type == FLEX_PAGETYPE_TONE);
}

// Check if the current page is a binary page
static int is_binary_page(struct Flex_Next* flex) {
    if (flex == nullptr) return 0;

    return (flex->Decode.type == FLEX_PAGETYPE_BINARY);
}

//=============================================================================
// MESSAGE PARSING FUNCTIONS
//=============================================================================

// Helper function to add characters to alphanumeric messages with proper escaping
static unsigned int add_ch(unsigned char ch, unsigned char* buf, unsigned int idx) {
    // Avoid buffer overflow
    if (idx >= MAX_ALN) {
        verbprintf(3, "FLEX_NEXT: idx %u >= MAX_ALN %u\n", idx, MAX_ALN);
        return 0;
    }

    // Handle special characters that need escaping
    if (ch == 0x09 && idx < (MAX_ALN - 2)) {  // '\t' (tab)
        buf[idx] = '\\';
        buf[idx + 1] = 't';
        return 2;
    }
    if (ch == 0x0a && idx < (MAX_ALN - 2)) {  // '\n' (newline)
        buf[idx] = '\\';
        buf[idx + 1] = 'n';
        return 2;
    }
    if (ch == 0x0d && idx < (MAX_ALN - 2)) {  // '\r' (carriage return)
        buf[idx] = '\\';
        buf[idx + 1] = 'r';
        return 2;
    }

    // Escape % character to prevent format string vulnerabilities
    if (ch == '%') {
        if (idx < (MAX_ALN - 2)) {
            buf[idx] = '%';
            buf[idx + 1] = '%';
            return 2;
        }
        return 0;
    }

    // Only store ASCII printable characters (32-126)
    if (ch >= 32 && ch <= 126) {
        buf[idx] = ch;
        return 1;
    }

    // Skip non-printable characters
    return 0;
}

// Parse alphanumeric messages (including group messages)
static void parse_alphanumeric(struct Flex_Next* flex, unsigned int* phaseptr, unsigned int mw1,
                              unsigned int len, int frag, int cont, int flex_groupmessage, int flex_groupbit) {
    if (flex == nullptr) return;

    // Determine fragment flag based on frag and cont values
    char frag_flag = '?';
    if (cont == 0 && frag == 3) frag_flag = 'K'; // complete, ready to send
    if (cont == 0 && frag != 3) frag_flag = 'C'; // incomplete until appended to 1 or more 'F's
    if (cont == 1             ) frag_flag = 'F'; // incomplete until a 'C' fragment is appended

    verbprintf(0, "%1d.%1d.%c|", frag, cont, frag_flag);

    unsigned char message[MAX_ALN];
    memset(message, '\0', MAX_ALN);
    int currentChar = 0;

    // Extract message characters from data words
    // (mw + i) < PHASE_WORDS (aka mw+len<=PW) enforced within decode_phase
    for (unsigned int i = 0; i < len; i++) {
        unsigned int dw = phaseptr[mw1 + i];

        // Skip first character if this is not the first fragment
        if (i > 0 || frag != 0x03) {
            currentChar += add_ch(dw & 0x7F, message, currentChar);
        }
        // Extract and add the remaining two 7-bit characters from each data word
        currentChar += add_ch((dw >> 7) & 0x7F, message, currentChar);
        currentChar += add_ch((dw >> 14) & 0x7F, message, currentChar);
    }
    message[currentChar] = '\0';

    // Handle group messages - implemented from bierviltje code (GitHub issue #123)
    if (flex_groupmessage == 1) {
        int endpoint = flex->GroupHandler.GroupCodes[flex_groupbit][CAPCODES_INDEX];

        // Output all capcodes in this group
        for (int g = 1; g <= endpoint; g++) {
            verbprintf(1, "FLEX Group message output: Groupbit: %i Total Capcodes; %i; index %i; Capcode: [%010" PRId64 "]\n",
                      flex_groupbit, endpoint, g, flex->GroupHandler.GroupCodes[flex_groupbit][g]);
            verbprintf(0, "%010" PRId64 "|", flex->GroupHandler.GroupCodes[flex_groupbit][g]);
        }

        // Reset the group message data
        flex->GroupHandler.GroupCodes[flex_groupbit][CAPCODES_INDEX] = 0;
        flex->GroupHandler.GroupFrame[flex_groupbit] = -1;
        flex->GroupHandler.GroupCycle[flex_groupbit] = -1;
    }

    // Output the decoded message
    verbprintf(0, "%s", message);
}

// Parse numeric messages using BCD encoding
static void parse_numeric(struct Flex_Next* flex, unsigned int* phaseptr, int j) {
    if (flex == nullptr) return;

    // BCD lookup table for FLEX numeric messages
    static const unsigned char flex_bcd[17] = "0123456789 U -][";

    // Extract message length and starting word from vector information
    int w1 = phaseptr[j] >> 7;
    int w2 = w1 >> 7;
    w1 = w1 & 0x7f;
    w2 = (w2 & 0x07) + w1;  // numeric message is 7 words max

    // Get first dataword from message field or from second vector word if long address
    int dw;
    if (!flex->Decode.long_address) {
        dw = phaseptr[w1];
        w1++;
        w2++;
    } else {
        dw = phaseptr[j + 1];
    }

    unsigned char digit = 0;
    int count = 4;

    // Skip header bits based on message type
    if (flex->Decode.type == FLEX_PAGETYPE_NUMBERED_NUMERIC) {
        count += 10;        // Skip 10 header bits for numbered numeric pages
    } else {
        count += 2;         // Otherwise skip 2
    }

    // Process each data word
    for (int i = w1; i <= w2; i++) {
        // Process 21 bits from each data word
        for (int k = 0; k < 21; k++) {
            // Shift LSB from data word into digit
            digit = (digit >> 1) & 0x0F;
            if (dw & 0x01) {
                digit ^= 0x08;
            }
            dw >>= 1;

            if (--count == 0) {
                // Output digit if it's not a fill character
                if (digit != 0x0C) { // Skip fill characters
                    verbprintf(0, "%c", flex_bcd[digit]);
                }
                count = 4;
            }
        }
        dw = phaseptr[i];
    }
}

// Parse tone-only messages (can also contain short numeric data)
static void parse_tone_only(struct Flex_Next* flex, unsigned int* phaseptr, int j) {
    if (flex == nullptr) return;

    // BCD lookup table for numeric digits
    static const unsigned char flex_bcd[17] = "0123456789 U -][";

    // Check message type: 1=tone-only, 0=short numeric
    int w1 = phaseptr[j] >> 7 & 0x03;

    if (!w1) {
        // Short numeric message - extract digits from specific bit positions
        unsigned char digit = 0;

        // Extract digits from positions 9, 13, and 17 in the vector word
        for (int i = 9; i <= 17; i += 4) {
            digit = (phaseptr[j] >> i) & 0x0f;
            verbprintf(0, "%c", flex_bcd[digit]);
        }

        // If long address, extract additional digits from the next word
        if (flex->Decode.long_address) {
            for (int i = 0; i <= 16; i += 4) {
                digit = (phaseptr[j + 1] >> i) & 0x0f;
                verbprintf(0, "%c", flex_bcd[digit]);
            }
        }
    }
    // For pure tone-only messages (w1 != 0), no additional data is output
}

// Parse binary messages as hexadecimal output
static void parse_binary(struct Flex_Next* flex, unsigned int* phaseptr, unsigned int mw1, unsigned int len) {
    if (flex == nullptr) return;

    // Output each data word as hexadecimal
    for (unsigned int i = 0; i < len; i++) {
        verbprintf(0, "%08x", phaseptr[mw1 + i]);
        // Add space separator between words (except for the last word)
        if (i < (len - 1)) {
            verbprintf(0, " ");
        }
    }
}
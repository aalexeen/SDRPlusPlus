#pragma once

// Shared DSP utilities and constants for pager decoders
// This file contains common definitions used by both POCSAG and FLEX decoders

// Common sample rates
#define PAGER_AUDIO_SAMPLERATE 22050.0  // Standard audio rate for audio-based decoders

// Common DSP includes that might be needed by multiple decoders
#include <dsp/stream.h>
#include <dsp/processor.h>

// You can add shared DSP utility functions here if needed in the future
// For now, protocol-specific DSP processing is handled in their respective folders:
// - POCSAG: pocsag/dsp.h (if created later)  
// - FLEX: flex/dsp.h
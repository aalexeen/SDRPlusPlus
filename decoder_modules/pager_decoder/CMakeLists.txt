cmake_minimum_required(VERSION 3.13)
project(pager_decoder)

# Collect source files manually to ensure all flex_next_decoder files are included
set(SRC
        # Main module files
        "src/main.cpp"

        # POCSAG decoder files
        "src/pocsag/pocsag.cpp"

        # FLEX decoder files (old wrapper - will be removed later)
        # Note: flex_next.cpp and flex_next.h are being phased out

        # Core flex_next_decoder implementation
        "src/flex/flex_next_decoder/FlexDecoder.cpp"
        "src/flex/flex_next_decoder/FlexDemodulator.cpp"
        "src/flex/flex_next_decoder/FlexStateMachine.cpp"
        "src/flex/flex_next_decoder/FlexSynchronizer.cpp"
        "src/flex/flex_next_decoder/FlexFrameProcessor.cpp"
        "src/flex/flex_next_decoder/FlexDataCollector.cpp"
        "src/flex/flex_next_decoder/FlexErrorCorrector.cpp"
        "src/flex/flex_next_decoder/FlexMessageDecoder.cpp"
        "src/flex/flex_next_decoder/FlexGroupHandler.cpp"

        # Message parser implementations
        "src/flex/flex_next_decoder/parsers/IMessageParser.cpp"
        "src/flex/flex_next_decoder/parsers/AlphanumericParser.cpp"
        "src/flex/flex_next_decoder/parsers/NumericParser.cpp"
        "src/flex/flex_next_decoder/parsers/ToneParser.cpp"
        "src/flex/flex_next_decoder/parsers/BinaryParser.cpp"
)

# Add the correct BCH implementation based on the global option
if (FLEX_USE_FULL_BCH)
    list(APPEND SRC "src/BCHCode.cpp")
    message(STATUS "Pager Decoder: Using full BCH error correction")
else ()
    list(APPEND SRC "src/BCHCode_stub.cpp")
    message(STATUS "Pager Decoder: Using BCH stub (no error correction)")
endif ()

# Check if FlexOutputFormatter.cpp exists (it might be header-only)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/flex/flex_next_decoder/FlexOutputFormatter.cpp")
    list(APPEND SRC "src/flex/flex_next_decoder/FlexOutputFormatter.cpp")
endif ()

# Create the module using the standard SDR++ module system
include(${SDRPP_MODULE_CMAKE})

# Add include directories for all subdirectories
target_include_directories(pager_decoder PRIVATE
        "src/"
        "src/flex/"
        "src/flex/flex_next_decoder/"
        "src/flex/flex_next_decoder/parsers/"
        "src/pocsag/"
)

# Configure BCH implementation
if (FLEX_USE_FULL_BCH)
    target_compile_definitions(pager_decoder PRIVATE USE_FULL_BCH=1)
else ()
    target_compile_definitions(pager_decoder PRIVATE USE_FULL_BCH=0)
endif ()

# Add C++17 features we're using
target_compile_features(pager_decoder PRIVATE cxx_std_17)

# Print configuration for debugging
message(STATUS "Pager Decoder Configuration:")
message(STATUS "  - BCH Implementation: ${FLEX_BCH_SOURCE}")
message(STATUS "  - Total source files: ${SRC}")
message(STATUS "  - Include directories: src/, src/flex/, src/flex/flex_next_decoder/, src/pocsag/")

# Verify all source files exist
foreach (src_file ${SRC})
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}")
        message(WARNING "Source file does not exist: ${src_file}")
    endif ()
endforeach ()
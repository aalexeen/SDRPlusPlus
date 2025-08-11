# Get needed values depending on if this is in-tree or out-of-tree
if (NOT SDRPP_CORE_ROOT)
    set(SDRPP_CORE_ROOT "@SDRPP_CORE_ROOT@")
endif ()
if (NOT SDRPP_MODULE_COMPILER_FLAGS)
    set(SDRPP_MODULE_COMPILER_FLAGS @SDRPP_MODULE_COMPILER_FLAGS@)
endif ()

# Create shared lib and link to core
# Only use the ${SRC} variable - each module's CMakeLists.txt populates this
add_library(${PROJECT_NAME} SHARED ${SRC}
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDecoder.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexTypes.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDemodulator.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexStateMachine.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexSynchronizer.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexFrameProcessor.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDataCollector.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexErrorCorrector.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexMessageDecoder.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/IMessageParser.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/AlphanumericParser.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/NumericParser.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/ToneParser.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/BinaryParser.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexGroupHandler.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexOutputFormatter.h
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexErrorCorrector.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexStateMachine.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexSynchronizer.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/IMessageParser.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/AlphanumericParser.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/NumericParser.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/ToneParser.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/parsers/BinaryParser.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexMessageDecoder.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDataCollector.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexGroupHandler.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexFrameProcessor.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDemodulator.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDemodulator.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDemodulator.cpp
        decoder_modules/pager_decoder/src/flex/flex_next_decoder/FlexDecoder.cpp
        decoder_modules/pager_decoder/src/flex/decoder.cpp
        decoder_modules/pager_decoder/src/flex/dsp.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE sdrpp_core)
target_include_directories(${PROJECT_NAME} PRIVATE "${SDRPP_CORE_ROOT}/src/")
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

# Set compile arguments
target_compile_options(${PROJECT_NAME} PRIVATE ${SDRPP_MODULE_COMPILER_FLAGS})

# Install directives
install(TARGETS ${PROJECT_NAME} DESTINATION lib/sdrpp/plugins)
cmake_minimum_required(VERSION 3.13)
project(pager_decoder)

# Collect all source files recursively
file(GLOB_RECURSE SRC
        "src/*.cpp"
        "src/*.c"
)

# Remove both BCH implementations from the list, we'll add the correct one
list(FILTER SRC EXCLUDE REGEX ".*BCHCode.*\\.cpp$")

# Add the correct BCH implementation based on the global option
if(FLEX_USE_FULL_BCH)
    list(APPEND SRC "src/BCHCode.cpp")
    message(STATUS "Pager Decoder: Using full BCH error correction")
else()
    list(APPEND SRC "src/BCHCode_stub.cpp")
    message(STATUS "Pager Decoder: Using BCH stub (no error correction)")
endif()

# Create the module using the standard SDR++ module system
include(${SDRPP_MODULE_CMAKE})

# Add include directories for all subdirectories
target_include_directories(pager_decoder PRIVATE
        "src/"
        "src/flex/"
        "src/pocsag/"
)

# Configure BCH implementation
if(FLEX_USE_FULL_BCH)
    target_compile_definitions(pager_decoder PRIVATE USE_FULL_BCH=1)
else()
    target_compile_definitions(pager_decoder PRIVATE USE_FULL_BCH=0)
endif()

# Print found source files for debugging
message(STATUS "Pager Decoder Sources: ${SRC}")
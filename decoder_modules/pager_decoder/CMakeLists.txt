cmake_minimum_required(VERSION 3.13)
project(pager_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(pager_decoder PRIVATE "src/")

# Add flex subdirectory sources
target_sources(pager_decoder PRIVATE
        src/flex/flex_next.cpp
        src/flex/BCHCode.cpp
        # Add other flex-related sources
)

# Include directories for flex headers
target_include_directories(pager_decoder PRIVATE
        src/flex
)

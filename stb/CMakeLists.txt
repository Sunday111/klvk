cmake_minimum_required(VERSION 3.29)

# stb is a pile of headers with no build of its own, so the checkout is added as
# an include directory and the one translation unit that instantiates the
# implementations lives here.
set(target_name stb)
add_library(${target_name} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/stb_implementation.cpp)
target_include_directories(${target_name} SYSTEM PUBLIC ${YAE_stb_sources_SOURCES})

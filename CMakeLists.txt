# ============================================================================
# Minimum + project + policies
# ============================================================================
cmake_minimum_required(VERSION 4.0)
project(Channels)

# ============================================================================
# Global language / standard settings
# ============================================================================
set(CMAKE_CXX_STANDARD 23)

# ============================================================================
# Build options (user toggles)
# ============================================================================
option(ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(ENABLE_ASAN "Enable AddressSanitizer (+LeakSan if available)" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" ON)
option(ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ASAN)" ON)
option(ENABLE_LTO "Enable Link-Time Optimization (IPO) in Release/RelWithDebInfo" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy if available" OFF)

# ============================================================================
# Helper logic
# ============================================================================
# Warnings
set(PROJECT_WARNING_FLAGS)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND PROJECT_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
elseif (MSVC)
    list(APPEND PROJECT_WARNING_FLAGS /W4 /permissive-)
endif ()
if (ENABLE_WARNINGS_AS_ERRORS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        list(APPEND PROJECT_WARNING_FLAGS -Werror)
    elseif (MSVC)
        list(APPEND PROJECT_WARNING_FLAGS /WX)
    endif ()
endif ()

# Sanitizers
if (ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive.")
endif ()
set(SANITIZER_COMPILE_FLAGS)
set(SANITIZER_LINK_FLAGS)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if (ENABLE_ASAN AND (CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo"))
        list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer)
        list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
    elseif (ENABLE_TSAN AND (CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo"))
        list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
        list(APPEND SANITIZER_LINK_FLAGS -fsanitize=thread)
    endif ()
    if (ENABLE_UBSAN AND (CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo"))
        list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=undefined)
        list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
    endif ()
endif ()

# Constexpr evaluation limit tweaks
set(CONSTEXPR_LIMIT_FLAGS)
if (ENABLE_CONSTEXPR_LIMIT_TWEAKS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")  # Matches Clang and AppleClang
        list(APPEND CONSTEXPR_LIMIT_FLAGS -fconstexpr-steps=200000000 -fconstexpr-depth=4096)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND CONSTEXPR_LIMIT_FLAGS -fconstexpr-ops-limit=1000000000 -fconstexpr-depth=4096 -fconstexpr-loop-limit=100000000)
    elseif (MSVC)
        list(APPEND CONSTEXPR_LIMIT_FLAGS /constexpr:steps200000000 /constexpr:depth4096)
    endif ()
endif ()

# Global fallback (affects every target, including OBJECT libs)
# Enable experimental library mode only when using Clang/libc++ where the flag is supported.
set(EXPERIMENTAL_LIB_FLAG)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND EXPERIMENTAL_LIB_FLAG -fexperimental-library)
endif ()
add_compile_options(${PROJECT_WARNING_FLAGS} ${SANITIZER_COMPILE_FLAGS} ${CONSTEXPR_LIMIT_FLAGS} ${EXPERIMENTAL_LIB_FLAG})
add_link_options(${SANITIZER_LINK_FLAGS})


# LTO (IPO)
if (ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
    if (ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else ()
        message(WARNING "IPO/LTO requested but not supported: ${ipo_msg}")
    endif ()
endif ()

# ============================================================================
# Dependencies
# ============================================================================
include(FetchContent)
FetchContent_Declare(
        googletest
        # Specify the commit you depend on and update it regularly.
        URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
)
FetchContent_MakeAvailable(googletest)

# ============================================================================
# Targets
# ============================================================================

# Main executable
add_executable(Channels main.cpp
        Channel.hpp
        unbuffered_channel.cpp
        unbuffered_channel.hpp
        buffered_channel.cpp
        buffered_channel.hpp)

# Tests
include(CTest)


# ============================================================================
# Developer summary
# ============================================================================
message(STATUS "Config summary:")
message(STATUS "  Build type              : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Warnings as errors      : ${ENABLE_WARNINGS_AS_ERRORS}")
message(STATUS "  ASan                    : ${ENABLE_ASAN}")
message(STATUS "  UBSan                   : ${ENABLE_UBSAN}")
message(STATUS "  TSan                    : ${ENABLE_TSAN}")
message(STATUS "  LTO/IPO                 : ${ENABLE_LTO}")
message(STATUS "  clang-tidy              : ${ENABLE_CLANG_TIDY}")
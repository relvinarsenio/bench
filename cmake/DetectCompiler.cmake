# =============================================================================
# DetectCompiler.cmake - Smart Compiler Detection for C++23 Static Builds
# =============================================================================
# Detects the best available compiler configuration for static builds:
#
# Priority order:
#   1. GCC 14+ with libstdc++ (if std::print works) - most portable
#   2. Clang 20+ with libstdc++ (RHEL/Fedora style) - if std::print works
#   3. Clang 20+ with libc++ (Ubuntu/Alpine style) - guaranteed C++23
#
# This allows the project to work on:
#   - Ubuntu 24.04+ (Clang 20 + libc++)
#   - RHEL 9+ / Fedora 40+ (Clang + libstdc++ or GCC 14+)
#   - Alpine Linux (Clang + libc++)
#   - Any system with GCC 14+ and updated libstdc++
# =============================================================================

set(BENCH_MIN_GCC_VERSION 14)
set(BENCH_MIN_CLANG_VERSION 20)

# =============================================================================
# Helper: Test if std::print works with current compiler/stdlib
# =============================================================================
function(test_std_print_support OUT_RESULT)
    # We only need to check if <print> header exists and compiles
    # Don't try to link - that can fail due to missing libraries during detection
    set(TEST_CODE "
#include <print>
void test() { std::print(\"test\"); }
")
    
    include(CheckCXXSourceCompiles)
    
    # Save current state
    set(CMAKE_REQUIRED_FLAGS_BACKUP "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE_BACKUP "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
    
    # Use STATIC_LIBRARY to avoid linking issues during detection
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_REQUIRED_FLAGS "-std=c++23 ${EXTRA_CXX_FLAGS}")
    
    check_cxx_source_compiles("${TEST_CODE}" ${OUT_RESULT})
    
    # Restore
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS_BACKUP}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${CMAKE_TRY_COMPILE_TARGET_TYPE_BACKUP}")
    
    # Propagate result
    set(${OUT_RESULT} ${${OUT_RESULT}} PARENT_SCOPE)
endfunction()

# =============================================================================
# Helper: Find libc++ static libraries
# =============================================================================
function(find_libcxx_static OUT_PATH OUT_LIBS)
    set(SEARCH_PATHS
        "/usr/lib/llvm-20/lib"
        "/usr/lib/llvm-21/lib"
        "/usr/lib/llvm-19/lib"
        "/usr/lib"
        "/usr/local/lib"
        "/opt/llvm/lib"
    )

    set(FOUND_PATH "")
    foreach(LIB_PATH ${SEARCH_PATHS})
        if(EXISTS "${LIB_PATH}/libc++.a")
            set(FOUND_PATH "${LIB_PATH}")
            break()
        endif()
    endforeach()

    if(NOT FOUND_PATH)
        set(${OUT_PATH} "" PARENT_SCOPE)
        set(${OUT_LIBS} "" PARENT_SCOPE)
        return()
    endif()

    set(STATIC_LIBS "${FOUND_PATH}/libc++.a")
    
    if(EXISTS "${FOUND_PATH}/libc++abi.a")
        list(APPEND STATIC_LIBS "${FOUND_PATH}/libc++abi.a")
    endif()
    
    if(EXISTS "${FOUND_PATH}/libunwind.a")
        list(APPEND STATIC_LIBS "${FOUND_PATH}/libunwind.a")
    endif()

    set(${OUT_PATH} "${FOUND_PATH}" PARENT_SCOPE)
    set(${OUT_LIBS} "${STATIC_LIBS}" PARENT_SCOPE)
endfunction()

# =============================================================================
# Main Detection Logic
# =============================================================================

set(COMPILER_DETECTED FALSE)
set(USE_LIBCXX FALSE)
set(USE_LIBSTDCXX FALSE)
set(STL_STATIC_LIBS "")

# -----------------------------------------------------------------------------
# Option 1: Try GCC 14+ with libstdc++
# -----------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL ${BENCH_MIN_GCC_VERSION})
        message(STATUS "🔍 Testing GCC ${CMAKE_CXX_COMPILER_VERSION} with libstdc++...")
        
        set(EXTRA_CXX_FLAGS "")
        test_std_print_support(GCC_HAS_PRINT)
        
        if(GCC_HAS_PRINT)
            message(STATUS "✅ GCC ${CMAKE_CXX_COMPILER_VERSION} with libstdc++ supports std::print!")
            set(COMPILER_DETECTED TRUE)
            set(USE_LIBSTDCXX TRUE)
            set(DETECTED_COMPILER "GCC")
            set(DETECTED_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
        else()
            message(STATUS "⚠️  GCC ${CMAKE_CXX_COMPILER_VERSION} found but libstdc++ lacks std::print support")
        endif()
    else()
        message(STATUS "⚠️  GCC ${CMAKE_CXX_COMPILER_VERSION} found but minimum required is ${BENCH_MIN_GCC_VERSION}")
    endif()
endif()

# -----------------------------------------------------------------------------
# Option 2: Try Clang with libc++ FIRST (Alpine/Ubuntu - better for static builds)
# -----------------------------------------------------------------------------
if(NOT COMPILER_DETECTED AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Extract Clang version
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} --version
        OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCH "clang version ([0-9]+)" _ "${CLANG_VERSION_OUTPUT}")
    set(CLANG_VERSION ${CMAKE_MATCH_1})
    
    if(CLANG_VERSION VERSION_GREATER_EQUAL ${BENCH_MIN_CLANG_VERSION})
        # Check if user explicitly requested libc++ via CMAKE_CXX_FLAGS
        if(CMAKE_CXX_FLAGS MATCHES "-stdlib=libc\\+\\+")
            set(USER_WANTS_LIBCXX TRUE)
        else()
            set(USER_WANTS_LIBCXX FALSE)
        endif()
        
        message(STATUS "🔍 Testing Clang ${CLANG_VERSION} with libc++...")
        
        # Find libc++ static libraries
        find_libcxx_static(LLVM_LIB_PATH STL_STATIC_LIBS)
        
        if(LLVM_LIB_PATH)
            set(EXTRA_CXX_FLAGS "-stdlib=libc++")
            test_std_print_support(CLANG_LIBCXX_HAS_PRINT)
            
            if(CLANG_LIBCXX_HAS_PRINT)
                message(STATUS "✅ Clang ${CLANG_VERSION} with libc++ supports std::print!")
                set(COMPILER_DETECTED TRUE)
                set(USE_LIBCXX TRUE)
                set(DETECTED_COMPILER "Clang")
                set(DETECTED_COMPILER_VERSION "${CLANG_VERSION}")
                message(STATUS "🔧 Found libc++ at: ${LLVM_LIB_PATH}")
            else()
                message(STATUS "⚠️  Clang ${CLANG_VERSION} with libc++ lacks std::print")
            endif()
        else()
            message(STATUS "⚠️  libc++ static libraries not found")
        endif()
    else()
        message(WARNING 
            "❌ Clang version ${CLANG_VERSION} detected, but minimum required is ${BENCH_MIN_CLANG_VERSION}.\n"
            "   Please install Clang ${BENCH_MIN_CLANG_VERSION}+ or GCC ${BENCH_MIN_GCC_VERSION}+."
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# Option 3: Try Clang with libstdc++ (RHEL/Fedora fallback)
# -----------------------------------------------------------------------------
if(NOT COMPILER_DETECTED AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # CLANG_VERSION should already be set from above
    if(CLANG_VERSION VERSION_GREATER_EQUAL ${BENCH_MIN_CLANG_VERSION})
        message(STATUS "🔍 Testing Clang ${CLANG_VERSION} with libstdc++...")
        
        # Test with libstdc++ (no -stdlib flag = default to libstdc++ on most systems)
        set(EXTRA_CXX_FLAGS "")
        test_std_print_support(CLANG_LIBSTDCXX_HAS_PRINT)
        
        if(CLANG_LIBSTDCXX_HAS_PRINT)
            message(STATUS "✅ Clang ${CLANG_VERSION} with libstdc++ supports std::print!")
            set(COMPILER_DETECTED TRUE)
            set(USE_LIBSTDCXX TRUE)
            set(DETECTED_COMPILER "Clang")
            set(DETECTED_COMPILER_VERSION "${CLANG_VERSION}")
        else()
            message(STATUS "⚠️  Clang ${CLANG_VERSION} with libstdc++ lacks std::print")
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# Final Check
# -----------------------------------------------------------------------------
if(NOT COMPILER_DETECTED)
    message(FATAL_ERROR 
        "❌ No suitable compiler found for C++23 static builds!\n"
        "\n"
        "This project requires std::print support. Options:\n"
        "\n"
        "1. GCC ${BENCH_MIN_GCC_VERSION}+ with updated libstdc++:\n"
        "   - Fedora 41+, RHEL 10+, or manually update libstdc++\n"
        "\n"
        "2. Clang ${BENCH_MIN_CLANG_VERSION}+ with libc++:\n"
        "   Ubuntu: sudo apt install clang-20 libc++-20-dev libc++abi-20-dev lld-20\n"
        "   Alpine: apk add clang lld libc++-static libc++-dev\n"
        "\n"
        "3. Clang ${BENCH_MIN_CLANG_VERSION}+ with libstdc++ (if libstdc++ has std::print):\n"
        "   RHEL/Fedora: Install clang and ensure libstdc++ supports C++23 fully\n"
    )
endif()

# =============================================================================
# Export Results
# =============================================================================
message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "  Compiler: ${DETECTED_COMPILER} ${DETECTED_COMPILER_VERSION}")
if(USE_LIBCXX)
    message(STATUS "  Stdlib:   libc++ (LLVM)")
else()
    message(STATUS "  Stdlib:   libstdc++ (GNU)")
endif()
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "")


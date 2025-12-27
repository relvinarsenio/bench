cmake_minimum_required(VERSION 3.24)

# Fix FetchContent timestamp warning (CMP0135)
cmake_policy(SET CMP0135 NEW)

# =============================================================================
# Bench - Modern Linux Server Benchmark
# =============================================================================
project(bench VERSION 6.9.6 LANGUAGES CXX C)

# C++23 Required
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# LLVM Toolchain Detection (for Clang builds)
# =============================================================================
# Use full LLVM toolchain for consistent builds when using Clang compiler.
# This applies to ALL targets: zlib, libcurl, and main project code.
# =============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    find_program(LLVM_AR llvm-ar)
    find_program(LLVM_RANLIB llvm-ranlib)
    find_program(LLVM_NM llvm-nm)
    
    if(LLVM_AR)
        set(CMAKE_AR "${LLVM_AR}" CACHE FILEPATH "Archiver" FORCE)
        message(STATUS "🔧 Using llvm-ar: ${LLVM_AR}")
    endif()
    if(LLVM_RANLIB)
        set(CMAKE_RANLIB "${LLVM_RANLIB}" CACHE FILEPATH "Ranlib" FORCE)
        message(STATUS "🔧 Using llvm-ranlib: ${LLVM_RANLIB}")
    endif()
    if(LLVM_NM)
        set(CMAKE_NM "${LLVM_NM}" CACHE FILEPATH "NM" FORCE)
        message(STATUS "🔧 Using llvm-nm: ${LLVM_NM}")
    endif()
endif()

# =============================================================================
# Optimization flags (compiler-specific)
# =============================================================================
# Clang: -Oz (aggressive size optimization) - smaller binary
# GCC:   -O3 (aggressive speed optimization) - faster code
# =============================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Common flags for both compilers
    add_compile_options(
        -ffunction-sections     # Put each function in its own section
        -fdata-sections         # Put each data item in its own section
        -fvisibility=hidden     # Hide symbols by default
    )
    add_link_options(
        -Wl,--gc-sections       # Remove unused sections (linker garbage collection)
        -Wl,--as-needed         # Only link libraries that are actually used
    )
    
    # Compiler-specific optimization level
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Oz -flto)
        add_link_options(-Oz -flto)
        message(STATUS "🚀 Optimization: Clang -Oz + LTO + GC sections (size optimized)")
    else()
        add_compile_options(-O3 -flto)
        add_link_options(-O3 -flto)
        message(STATUS "🚀 Optimization: GCC -O3 + LTO + GC sections (speed optimized)")
    endif()
endif()

# Add cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# =============================================================================
# 1. Smart Compiler Detection (GCC 14+ or Clang 20+)
# =============================================================================
# Tries in order:
#   1. GCC 14+ with libstdc++ (if std::print works)
#   2. Clang 20+ with libstdc++ (RHEL/Fedora)
#   3. Clang 20+ with libc++ (Ubuntu/Alpine)
# =============================================================================
include(DetectCompiler)

# =============================================================================
# 2. Configure Static Dependencies (ALL FROM SOURCE)
# =============================================================================
include(FetchContent)
include(StaticDeps)

# Fetch nlohmann/json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Fetch libcurl (uses OpenSSL and ZLIB built in StaticDeps.cmake)
FetchContent_Declare(
    CURL
    URL https://github.com/curl/curl/releases/download/curl-8_17_0/curl-8.17.0.tar.xz
)
FetchContent_MakeAvailable(CURL)

# =============================================================================
# 3. Compiler Configuration (based on DetectCompiler.cmake results)
# =============================================================================
add_link_options("-static")

if(USE_LIBCXX)
    # Clang with libc++ (Ubuntu/Alpine)
    message(STATUS "🔧 Mode: Clang ${DETECTED_COMPILER_VERSION} + libc++ (LLVM Full Stack)")
    add_compile_options("-stdlib=libc++")
    add_link_options("-fuse-ld=lld")
    add_link_options("-nostdlib++")
    
elseif(USE_LIBSTDCXX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang with libstdc++ (RHEL/Fedora)
    message(STATUS "🔧 Mode: Clang ${DETECTED_COMPILER_VERSION} + libstdc++ (GNU)")
    add_link_options("-fuse-ld=lld")
    
elseif(USE_LIBSTDCXX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC with libstdc++
    message(STATUS "🔧 Mode: GCC ${DETECTED_COMPILER_VERSION} + libstdc++ (Full GNU)")
    # GCC uses gold or bfd linker by default
endif()

# =============================================================================
# 4. Optimization Flags
# =============================================================================
add_compile_options(-O3 -Wall -Wextra -fPIE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    add_compile_options(-march=x86-64-v3)
endif()

# =============================================================================
# 5. Build Target
# =============================================================================
add_executable(bench
    src/app/main.cpp
    src/core/interrupts.cpp
    src/core/utils.cpp
    src/os/file_descriptor.cpp
    src/os/shell_pipe.cpp
    src/system/system_info.cpp
    src/net/http_client.cpp
    src/io/disk_benchmark.cpp
    src/net/speed_test.cpp
    src/ui/cli_renderer.cpp
)

target_include_directories(bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# =============================================================================
# 6. Linking
# =============================================================================
target_link_libraries(bench 
    PRIVATE 
    CURL::libcurl
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
    m
)

# Add static C++ stdlib libs based on detected configuration
if(USE_LIBCXX AND STL_STATIC_LIBS)
    # Clang + libc++: link static libc++.a, libc++abi.a, etc.
    target_link_libraries(bench PRIVATE ${STL_STATIC_LIBS})
    
    # On musl systems (Alpine), also need to link libc++abi explicitly
    # and potentially compiler-rt builtins
    if(EXISTS "/usr/lib/libc++abi.a")
        target_link_libraries(bench PRIVATE /usr/lib/libc++abi.a)
    endif()
elseif(USE_LIBSTDCXX)
    # GCC or Clang + libstdc++: link static libstdc++
    target_link_libraries(bench PRIVATE -static-libstdc++ -static-libgcc)
endif()

# =============================================================================
# 7. Install & Packaging (CPack)
# =============================================================================
install(TARGETS bench DESTINATION bin)

set(CPACK_PACKAGE_NAME "bench")
set(CPACK_PACKAGE_VENDOR "Alfie Ardinata")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Alfie Ardinata <alfieardinata@outlook.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/relvinarsenio/bench")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "System Information & Network Benchmark Tool")
set(CPACK_PACKAGE_DESCRIPTION "Bench is a high-performance Linux server benchmarking tool written in Modern C++ (C++23).
Features:
- Network Speed Test (Upload/Download)
- Disk I/O Benchmark
- System Information Monitoring
- Fully Static Build (No Dependencies Required)")

# Debian package settings
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Alfie Ardinata <alfieardinata@outlook.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_STRIP_FILES TRUE)
set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")

include(CPack)

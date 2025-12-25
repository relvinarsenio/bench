cmake_minimum_required(VERSION 3.20)

# --- 1. Setup Compiler ---
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(GCC_PATH "g++")
    if(GCC_PATH)
        set(CMAKE_CXX_COMPILER "${GCC_PATH}")
    endif()
endif()

project(bench VERSION 6.9.6 LANGUAGES CXX)

# --- 2. Dependencies ---
find_package(CURL REQUIRED)

# nlohmann/json dependency from source (GitHub)
include(FetchContent)
FetchContent_Declare(
    json 
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# --- 3. Force Standard C++23 ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# --- 4. Compiler Specific Configuration ---
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    include(CheckIncludeFileCXX)
    
    # 1. Cek dulu apakah library bawaan sistem (libstdc++) support <print>
    message(STATUS "🔍 Checking system stdlib support for <print>...")
    check_include_file_cxx("print" CLANG_HAS_SYSTEM_PRINT)

    if (CLANG_HAS_SYSTEM_PRINT)
        message(STATUS "✅ Clang: System libstdc++ supports <print>. Using system defaults.")
        # Gunakan LLD agar support LTO (Clang bitcode)
        add_link_options("-fuse-ld=lld") 
    else()
        message(STATUS "⚠️ Clang: System libstdc++ does NOT support <print>.")
        message(STATUS "🔹 Switching to LLVM Full Stack (libc++, lld)...")
        
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++ -fuse-ld=lld)

        # Attempt to find LLVM library path relative to compiler
        get_filename_component(COMPILER_BIN ${CMAKE_CXX_COMPILER} REALPATH)
        get_filename_component(COMPILER_DIR ${COMPILER_BIN} DIRECTORY)
        get_filename_component(LLVM_ROOT ${COMPILER_DIR} DIRECTORY)
        
        set(LLVM_LIB_DIR "${LLVM_ROOT}/lib")
        
        if(EXISTS "${LLVM_LIB_DIR}/libc++.so")
            message(STATUS "   Using LLVM libraries from: ${LLVM_LIB_DIR}")
            link_directories(${LLVM_LIB_DIR})
            add_link_options("-Wl,-rpath,${LLVM_LIB_DIR}")
            # Ensure we link against the correct libraries
            add_link_options(-lc++ -lc++abi)
        endif()
    endif()
    
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(STATUS "🔸 GCC detected: Using default GNU stack")
endif()

# --- 5. STRICT CHECK: Wajib ada <print> ---
# Reset variable check agar dicek ulang dengan flag yang sudah diset di atas
unset(HAVE_STD_PRINT CACHE)

include(CheckIncludeFileCXX)
# Ensure check uses the same flags
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CLANG_HAS_SYSTEM_PRINT)
    set(CMAKE_REQUIRED_FLAGS "-stdlib=libc++")
endif()
check_include_file_cxx("print" HAVE_STD_PRINT)

if(NOT HAVE_STD_PRINT)
    message(FATAL_ERROR "❌ ERROR: Header <print> tidak ditemukan!\n"
    "Harap gunakan GCC 14+ atau Clang 18+ dengan libc++ terbaru.")
endif()

# --- 6. Flags (Optimasi & Security) ---
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -Wall -Wextra
        -fstack-protector-strong -fPIE
        -D_FORTIFY_SOURCE=2
        -Wformat -Werror=format-security
    )
    
    # LTO Configuration
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(STATUS "🚀 Clang: Enabling Full LTO")
        add_compile_options(-flto=full)
        add_link_options(-flto=full)
    else()
        add_compile_options(-flto=auto)
        add_link_options(-flto=auto)
    endif()
    
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        add_compile_options(-march=x86-64-v3 -fcf-protection)
    endif()

    add_link_options(-Wl,-z,relro -Wl,-z,now -Wl,--gc-sections -pie)
endif()

# --- 7. Build ---
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

# Link ke CURL dan nlohmann_json
target_link_libraries(bench PRIVATE CURL::libcurl nlohmann_json::nlohmann_json m)
#pragma once

#include <cstddef>
#include <string_view>

namespace Config {
    constexpr std::size_t IO_BLOCK_SIZE = 4 * 1024 * 1024;
    constexpr std::size_t IO_ALIGNMENT = 4096;
    constexpr std::string_view BENCH_FILENAME = "benchtest_file";
    constexpr std::string_view SPEEDTEST_CLI_PATH = "speedtest-cli/speedtest";
    constexpr std::string_view SPEEDTEST_TGZ = "speedtest.tgz";
}

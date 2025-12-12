#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

void print_line();
[[nodiscard]] std::string trim(const std::string& str);
[[nodiscard]] constexpr std::string_view trim_sv(std::string_view str) noexcept;
std::string format_bytes(std::uint64_t bytes);
void cleanup_artifacts();
std::filesystem::path get_exe_dir();

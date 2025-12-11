#pragma once

#include <cstdint>
#include <string>
#include <string_view>

void print_line();
std::string trim(const std::string& str);
std::string_view trim_sv(std::string_view str);
std::string format_bytes(std::uint64_t bytes);
void cleanup_artifacts();

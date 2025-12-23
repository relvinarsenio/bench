#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

void print_line();

namespace detail {

template <typename StringType>
[[nodiscard]] constexpr auto trim_generic(const StringType& str) {
	auto first = str.find_first_not_of(" \t\n\r");
	if (first == StringType::npos) return StringType{};
	auto last = str.find_last_not_of(" \t\n\r");
	return str.substr(first, last - first + 1);
}

}

[[nodiscard]] std::string trim(const std::string& str);
[[nodiscard]] constexpr std::string_view trim_sv(std::string_view str) noexcept {
	return detail::trim_generic(str);
}

std::string format_bytes(std::uint64_t bytes);
void cleanup_artifacts();
std::filesystem::path get_exe_dir();

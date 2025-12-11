#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "results.hpp"

class HttpClient;

enum class SpinnerEvent { Start, Stop };
using SpinnerCallback = std::function<void(SpinnerEvent, std::string_view)>;

class SpeedTest {
    HttpClient& http_;
    std::filesystem::path base_dir_;
    std::filesystem::path cli_dir_;
    std::filesystem::path cli_path_;
    std::filesystem::path tgz_path_;

    std::string get_arch();
    std::vector<std::string> parse_csv_line(const std::string& line);

public:
    explicit SpeedTest(HttpClient& h);
    void install();
    SpeedTestResult run(const SpinnerCallback& spinner_cb = {});
};

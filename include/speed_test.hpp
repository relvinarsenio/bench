#pragma once

#include <string>
#include <vector>

class HttpClient;

class SpeedTest {
    HttpClient& http_;
    const std::string cli_path;

    std::string get_arch();
    std::vector<std::string> parse_csv_line(const std::string& line);

public:
    explicit SpeedTest(HttpClient& h);
    void install();
    void run();
};

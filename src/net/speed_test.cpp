#include "include/speed_test.hpp"

#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/utsname.h>

#include "include/color.hpp"
#include "include/config.hpp"
#include "include/http_client.hpp"
#include "include/interrupts.hpp"
#include "include/shell_pipe.hpp"

namespace fs = std::filesystem;

SpeedTest::SpeedTest(HttpClient& h) : http_(h), cli_path(Config::SPEEDTEST_CLI_PATH) {}

std::string SpeedTest::get_arch() {
    struct utsname buf; uname(&buf);
    std::string m(buf.machine);
    if (m == "x86_64") return "x86_64";
    if (m == "aarch64" || m == "arm64") return "aarch64";
    if (m == "i386" || m == "i686") return "i386";
    if (m == "armv7l") return "armhf";
    throw std::runtime_error("Unsupported architecture: " + m);
}

std::vector<std::string> SpeedTest::parse_csv_line(const std::string& line) {
    std::vector<std::string> tokens;
    bool in_quotes = false;
    std::string current_token;
    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            tokens.push_back(current_token);
            current_token.clear();
        } else {
            current_token += c;
        }
    }
    tokens.push_back(current_token);
    return tokens;
}

void SpeedTest::install() {
    if (fs::exists(cli_path)) return;

    std::println("Downloading Speedtest CLI...");
    std::string url = std::format("https://install.speedtest.net/app/cli/ookla-speedtest-1.2.0-linux-{}.tgz", get_arch());

    try {
        http_.download(url, std::string(Config::SPEEDTEST_TGZ));
    } catch (...) {
        throw;
    }

    fs::create_directories("speedtest-cli");
    std::string tar_cmd = std::format("tar zxf {} -C ./speedtest-cli 2>&1", Config::SPEEDTEST_TGZ);
    ShellPipe pipe(tar_cmd);
    pipe.read_all();

    if (!fs::exists(cli_path)) throw std::runtime_error("Failed to extract speedtest-cli");

    fs::permissions(cli_path, fs::perms::owner_all, fs::perm_options::add);
    fs::remove(Config::SPEEDTEST_TGZ);
}

void SpeedTest::run() {
    struct Node { std::string id; std::string name; };

    std::vector<Node> nodes = {
        {"", "Speedtest.net (Auto)"},
        {"3864", "Los Angeles, US"},
        {"57114", "Dallas, US"},
        {"12723",  "Montreal, CA"},
        {"5976",  "Paris, FR"},
        {"3386",  "Amsterdam, NL"},
        {"22223",  "Beijing, CN"},
        {"3633", "Shanghai, CN"},
        {"17251", "Guangzhou, CN"},
        {"1536",  "Hong Kong, CN"},
        {"13623", "Singapore, SG"},
        {"22247", "Tokyo, JP"}
    };

    std::println("{:<24}{:<18}{:<18}{:<12}{:<8}", " Node Name", "Upload", "Download", "Latency", "Loss");

    for (const auto& node : nodes) {
        check_interrupted();

        std::string cmd = cli_path + " -f csv --accept-license --accept-gdpr";
        if (!node.id.empty()) cmd += " --server-id=" + node.id;
        cmd += " 2>&1";

        try {
            ShellPipe pipe(cmd);
            std::string output = pipe.read_all();
            std::stringstream ss(output);
            std::string line;
            bool success = false;

            while(std::getline(ss, line)) {
                if (line.find("[error]") != std::string::npos) {
                    std::string clean_err = line;
                    size_t msg_pos = line.find("] [error] ");
                    if (msg_pos != std::string::npos) {
                        clean_err = line.substr(msg_pos + 10);
                    } else {
                        msg_pos = line.find("[error] ");
                        if (msg_pos != std::string::npos) clean_err = line.substr(msg_pos + 8);
                    }

                    if (clean_err.find("Limit reached") != std::string::npos) {
                        std::print("{}{: <24}{}Error: Rate Limit (Too many requests). Aborting.{}\n",
                            Color::YELLOW, " " + node.name, Color::RED, Color::RESET);
                        return;
                    }
                    else if (clean_err.find("No servers defined") != std::string::npos) {
                         std::print("{}{: <24}{}Error: Server ID Changed/Offline{}\n",
                            Color::YELLOW, " " + node.name, Color::RED, Color::RESET);
                         success = true;
                         break;
                    }
                    continue;
                }

                if (line.find(',') != std::string::npos && line.find('"') != std::string::npos) {
                    auto cols = parse_csv_line(line);

                    if (cols.size() >= 7) {
                        try {
                            double dl_bytes = std::stod(cols[5]);
                            double ul_bytes = std::stod(cols[6]);
                            double lat_val = std::stod(cols[2]);

                            std::string loss_raw = cols[4];
                            std::string loss_formatted;
                            try {
                                double loss_val = std::stod(loss_raw);
                                loss_formatted = std::format("{:.2f}%", loss_val);
                            } catch (...) {
                                if (loss_raw == "N/A" || loss_raw.empty()) loss_formatted = "-";
                                else loss_formatted = loss_raw;
                            }

                            double dl_mbps = (dl_bytes * 8.0) / 1000000.0;
                            double ul_mbps = (ul_bytes * 8.0) / 1000000.0;

                            std::print("{}{: <24}{}{:<18}{}{:<18}{}{:<12}{}{:<8}{}\n",
                                Color::YELLOW, " " + node.name,
                                Color::GREEN, std::format("{:.2f} Mbps", ul_mbps),
                                Color::RED,   std::format("{:.2f} Mbps", dl_mbps),
                                Color::CYAN,  std::format("{:.2f} ms", lat_val),
                                Color::RED,   loss_formatted,
                                Color::RESET);

                            success = true;
                            break;
                        } catch (...) { continue; }
                    }
                }
            }

            if (!success) {
                std::string error_msg = output;
                if (!output.empty() && output.back() == '\n') output.pop_back();

                size_t err_idx = output.find("[error] ");
                if (err_idx != std::string::npos) {
                    error_msg = output.substr(err_idx + 8);
                }

                if (error_msg.length() > 45) error_msg = error_msg.substr(0, 42) + "...";

                std::print("{}{: <24}{}Error: {}{}\n",
                    Color::YELLOW, " " + node.name, Color::RED, error_msg, Color::RESET);
            }

        } catch (const std::exception& e) {
            std::print("{}{: <24}{}Error: {}{}\n", Color::YELLOW, " " + node.name, Color::RED, e.what(), Color::RESET);
        }
        std::cout << std::flush;
    }
}

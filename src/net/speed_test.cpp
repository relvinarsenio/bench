#include "include/speed_test.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/utsname.h>

#include "include/color.hpp"
#include "include/config.hpp"
#include "include/http_client.hpp"
#include "include/interrupts.hpp"
#include "include/results.hpp"
#include "include/shell_pipe.hpp"
#include "include/utils.hpp"

namespace {

class Spinner {
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::string text_;

public:
    explicit Spinner(std::string text) : running_(true), text_(std::move(text)) {
        worker_ = std::thread([this] {
            static constexpr char frames[] = {'|', '/', '-', '\\'};
            std::size_t idx = 0;
            while (running_.load(std::memory_order_relaxed)) {
                std::print("\r{} {}", text_, frames[idx++ % 4]);
                std::cout.flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        });
    }

    void stop() {
        if (running_.exchange(false)) {
            if (worker_.joinable()) worker_.join();
            std::print("\r{}\r", std::string(text_.size() + 2, ' '));
            std::cout.flush();
        }
    }

    ~Spinner() { stop(); }
};

}

namespace fs = std::filesystem;

SpeedTest::SpeedTest(HttpClient& h) : http_(h) {
    base_dir_ = get_exe_dir();
    std::filesystem::path cli_rel(Config::SPEEDTEST_CLI_PATH);
    cli_dir_ = base_dir_ / cli_rel.parent_path();
    cli_path_ = base_dir_ / cli_rel;
    tgz_path_ = base_dir_ / Config::SPEEDTEST_TGZ;
}

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
    if (fs::exists(cli_path_)) return;

    std::println("Downloading Speedtest CLI...");
    std::string url = std::format("https://install.speedtest.net/app/cli/ookla-speedtest-1.2.0-linux-{}.tgz", get_arch());

    try {
        http_.download(url, tgz_path_.string());
    } catch (...) {
        throw;
    }

    fs::create_directories(cli_dir_);
    std::string tar_cmd = std::format("tar zxf {} -C {} 2>&1", tgz_path_.string(), cli_dir_.string());
    ShellPipe pipe(tar_cmd);
    pipe.read_all();

    if (!fs::exists(cli_path_)) throw std::runtime_error("Failed to extract speedtest-cli");

    fs::permissions(cli_path_, fs::perms::owner_all, fs::perm_options::add);
    fs::remove(tgz_path_);
}

SpeedTestResult SpeedTest::run() {
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

    SpeedTestResult result;

    for (const auto& node : nodes) {
        check_interrupted();

        std::string cmd = cli_path_.string() + " -f csv --accept-license --accept-gdpr";
        Spinner spinner(std::format("Testing {}", node.name));
        if (!node.id.empty()) cmd += " --server-id=" + node.id;
        cmd += " 2>&1";

        SpeedEntryResult entry;
        entry.server_id = node.id;
        entry.node_name = node.name;

        try {
            ShellPipe pipe(cmd);
            std::string output = pipe.read_all();
            spinner.stop();
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
                        entry.error = "Rate Limit (Too many requests). Aborting.";
                        entry.rate_limited = true;
                        result.entries.push_back(entry);
                        result.rate_limited = true;
                        return result;
                    }
                    else if (clean_err.find("No servers defined") != std::string::npos) {
                        entry.error = "Server ID Changed/Offline";
                        entry.success = false;
                        result.entries.push_back(entry);
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
                                loss_formatted = std::format("{:.2f} %", loss_val);
                            } catch (...) {
                                if (loss_raw == "N/A" || loss_raw.empty()) loss_formatted = "-";
                                else loss_formatted = loss_raw;
                            }

                            double dl_mbps = (dl_bytes * 8.0) / 1000000.0;
                            double ul_mbps = (ul_bytes * 8.0) / 1000000.0;

                            entry.upload_mbps = ul_mbps;
                            entry.download_mbps = dl_mbps;
                            entry.latency_ms = lat_val;
                            entry.loss = loss_formatted;
                            entry.success = true;

                            success = true;
                            break;
                        } catch (...) { continue; }
                    }
                }
            }

            if (!success && !entry.success) {
                std::string error_msg = output;
                if (!output.empty() && output.back() == '\n') output.pop_back();

                size_t err_idx = output.find("[error] ");
                if (err_idx != std::string::npos) {
                    error_msg = output.substr(err_idx + 8);
                }
                entry.error = error_msg;
            }

        } catch (const std::exception& e) {
            spinner.stop();
            entry.error = e.what();
        }
        spinner.stop();
        result.entries.push_back(entry);
    }

    return result;
}

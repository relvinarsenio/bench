#include "include/cli_renderer.hpp"

#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <thread>

#include "include/color.hpp"
#include "include/speed_test.hpp"

namespace CliRenderer {

namespace {

class UiSpinner {
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::string text_;

public:
    void start(std::string_view text) {
        stop();
        text_ = text;
        running_.store(true, std::memory_order_relaxed);
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
        if (running_.exchange(false, std::memory_order_relaxed)) {
            if (worker_.joinable()) worker_.join();
            std::print("\r{}\r", std::string(text_.size() + 2, ' '));
            std::cout.flush();
        }
    }

    ~UiSpinner() { stop(); }
};

}

void render_disk_suite(const DiskSuiteResult& suite) {
    std::println("Running I/O Test (1GB File)...");
    for (const auto& run : suite.runs) {
        std::println("{}{}", run.label, Color::colorize(std::format("{:.1f} MB/s", run.mbps), Color::YELLOW));
    }
    if (!suite.runs.empty()) {
        double avg = suite.average_mbps;
        std::println(" I/O Speed (Average) : {}", Color::colorize(std::format("{:.1f} MB/s", avg), Color::YELLOW));
    }
}

void render_speed_results(const SpeedTestResult& result) {
    std::println("{:<24}{:<18}{:<18}{:<12}{:<8}", " Node Name", "Upload", "Download", "Latency", "Loss");
    for (const auto& entry : result.entries) {
        if (!entry.success) {
            std::string err = entry.error;
            if (err.length() > 45) err = err.substr(0, 42) + "...";
            std::print("{}{: <24}{}Error: {}{}\n",
                Color::YELLOW, " " + entry.node_name, Color::RED, err, Color::RESET);
            continue;
        }
        std::print("{}{: <24}{}{:<18}{}{:<18}{}{:<12}{}{:<8}{}\n",
            Color::YELLOW, " " + entry.node_name,
            Color::GREEN, std::format("{:.2f} Mbps", entry.upload_mbps),
            Color::RED,   std::format("{:.2f} Mbps", entry.download_mbps),
            Color::CYAN,  std::format("{:.2f} ms", entry.latency_ms),
            Color::RED,   entry.loss.empty() ? "-" : entry.loss,
            Color::RESET);
    }
}

SpinnerCallback make_spinner_callback() {
    auto spinner = std::make_shared<UiSpinner>();
    return [spinner](SpinnerEvent ev, std::string_view label) {
        switch (ev) {
            case SpinnerEvent::Start:
                spinner->start(label);
                break;
            case SpinnerEvent::Stop:
                spinner->stop();
                break;
        }
    };
}

}

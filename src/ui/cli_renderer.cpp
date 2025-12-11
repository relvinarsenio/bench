#include "include/cli_renderer.hpp"

#include <format>
#include <print>
#include <string>

#include "include/color.hpp"

namespace CliRenderer {

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

} // namespace CliRenderer

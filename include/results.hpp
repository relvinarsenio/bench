#pragma once

#include <string>
#include <vector>

struct DiskRunResult {
    std::string label;
    double mbps = 0.0;
};

struct DiskSuiteResult {
    std::vector<DiskRunResult> runs;
    double average_mbps = 0.0;
};

struct SpeedEntryResult {
    std::string server_id;
    std::string node_name;
    double upload_mbps = 0.0;
    double download_mbps = 0.0;
    double latency_ms = 0.0;
    std::string loss;
    bool success = false;
    std::string error;    
    bool rate_limited = false;
};

struct SpeedTestResult {
    std::vector<SpeedEntryResult> entries;
    bool rate_limited = false;
};

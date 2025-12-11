#pragma once

#include <string_view>

class DiskBenchmark {
public:
    static double run_write_test(int size_mb, std::string_view label);
};

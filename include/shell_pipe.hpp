#pragma once

#include <cstdio>
#include <memory>
#include <string>

class ShellPipe {
    struct PipeCloser {
        void operator()(FILE* fp) const;
    };

    std::unique_ptr<FILE, PipeCloser> pipe_;

public:
    explicit ShellPipe(const std::string& command);
    std::string read_all();
};
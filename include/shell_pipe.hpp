#pragma once

#include <cstdio>
#include <string>

class ShellPipe {
    FILE* pipe_ = nullptr;
public:
    explicit ShellPipe(const std::string& cmd);
    ~ShellPipe();
    ShellPipe(const ShellPipe&) = delete;
    ShellPipe& operator=(const ShellPipe&) = delete;

    FILE* get() const;
    std::string read_all();
};

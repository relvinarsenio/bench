#include "include/shell_pipe.hpp"

#include <array>
#include <cerrno>
#include <system_error>

#include "include/interrupts.hpp"

ShellPipe::ShellPipe(const std::string& cmd) {
    pipe_ = ::popen(cmd.c_str(), "r");
    if (!pipe_) {
        throw std::system_error(errno, std::generic_category(), "popen failed for: " + cmd);
    }
}

ShellPipe::~ShellPipe() {
    if (pipe_) ::pclose(pipe_);
}

FILE* ShellPipe::get() const { return pipe_; }

std::string ShellPipe::read_all() {
    std::array<char, 256> buffer;
    std::string result;
    while (::fgets(buffer.data(), buffer.size(), pipe_) != nullptr) {
        check_interrupted();
        result += buffer.data();
    }
    return result;
}

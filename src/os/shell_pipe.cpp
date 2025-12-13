#include "include/shell_pipe.hpp"

#include <array>
#include <stdexcept>

void ShellPipe::PipeCloser::operator()(FILE* fp) const {
    if (fp) pclose(fp);
}

ShellPipe::ShellPipe(const std::string& command) {
    pipe_.reset(popen(command.c_str(), "r"));
    if (!pipe_) throw std::runtime_error("Failed to execute command");
}

std::string ShellPipe::read_all() {
    std::array<char, 3072> buffer;
    std::string result;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe_.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}
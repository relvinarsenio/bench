#include "include/file_descriptor.hpp"

#include <cerrno>
#include <unistd.h>

FileDescriptor::FileDescriptor(int fd) : fd_(fd) {
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "Invalid file descriptor");
    }
}

FileDescriptor::~FileDescriptor() {
    if (fd_ >= 0) ::close(fd_);
}

int FileDescriptor::get() const { return fd_; }

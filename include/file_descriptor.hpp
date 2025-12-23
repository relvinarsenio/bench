#pragma once

#include <system_error>

class FileDescriptor {
    int fd_ = -1;
public:
    explicit FileDescriptor(int fd);
    ~FileDescriptor();
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const;
};

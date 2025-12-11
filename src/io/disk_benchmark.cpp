#include "include/disk_benchmark.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

#include "include/config.hpp"
#include "include/file_descriptor.hpp"
#include "include/interrupts.hpp"

using namespace std::chrono;

class DiskBenchmarkBuffer {
    struct AlignedFree {
        void operator()(void* ptr) { std::free(ptr); }
    };
    using AlignedPtr = std::unique_ptr<void, AlignedFree>;

    AlignedPtr buffer_;

public:
    explicit DiskBenchmarkBuffer(std::size_t size, std::size_t alignment) {
        if (size % alignment != 0) size = (size + alignment - 1) & ~(alignment - 1);
        void* ptr = std::aligned_alloc(alignment, size);
        if (!ptr) throw std::bad_alloc();
        buffer_.reset(ptr);
    }

    void* data() const { return buffer_.get(); }
};

double DiskBenchmark::run_write_test(int size_mb, std::string_view label) {
    const std::string filename(Config::BENCH_FILENAME);
    const size_t block_size = Config::IO_BLOCK_SIZE;

    DiskBenchmarkBuffer buffer(block_size, Config::IO_ALIGNMENT);
    std::memset(buffer.data(), 0, block_size);

    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    #ifdef O_DIRECT
    flags |= O_DIRECT;
    #endif

    int fd_raw = ::open(filename.c_str(), flags, 0644);
    if (fd_raw < 0 && errno == EINVAL) {
         #ifdef O_DIRECT
         flags &= ~O_DIRECT;
         #endif
         fd_raw = ::open(filename.c_str(), flags, 0644);
    }

    if (fd_raw < 0) {
        throw std::system_error(errno, std::generic_category(),
            std::format("Failed to open file for benchmark: {}", filename));
    }

    FileDescriptor fd(fd_raw);

    auto start = high_resolution_clock::now();
    size_t blocks = (size_t(size_mb) * 1024 * 1024) / block_size;

    auto print_progress = [&](size_t current, size_t total) {
        int percent = static_cast<int>((current * 100) / total);
        std::print("\r{} [{:3}%] ", label, percent);
        std::cout << std::flush;
    };

    for (size_t i = 0; i < blocks; ++i) {
        check_interrupted();
        ssize_t written = ::write(fd.get(), buffer.data(), block_size);
        if (written != static_cast<ssize_t>(block_size)) {
            throw std::system_error(errno, std::generic_category(), "Disk write failed");
        }
        if (i % 2 == 0) print_progress(i + 1, blocks);
    }
    print_progress(blocks, blocks);

    ::fdatasync(fd.get());
    auto end = high_resolution_clock::now();
    std::error_code ec; std::filesystem::remove(filename, ec);

    std::print("\r{}", label);
    std::cout << std::flush;

    duration<double> diff = end - start;
    if (diff.count() <= 0) return 0.0;
    return static_cast<double>(size_mb) / diff.count();
}

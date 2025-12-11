#include "include/disk_benchmark.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "include/config.hpp"
#include "include/file_descriptor.hpp"
#include "include/interrupts.hpp"
#include "include/results.hpp"

using namespace std::chrono;

class DiskBenchmarkBuffer {
    std::vector<std::byte> storage_;
    void* aligned_ = nullptr;

public:
    explicit DiskBenchmarkBuffer(std::size_t size, std::size_t alignment) : storage_(size + alignment) {
        void* ptr = storage_.data();
        std::size_t space = storage_.size();
        if (std::align(alignment, size, ptr, space) == nullptr) {
            throw std::bad_alloc();
        }
        aligned_ = ptr;
    }

    void* data() const { return aligned_; }
};

DiskRunResult DiskBenchmark::run_write_test(
    int size_mb,
    std::string_view label,
    const std::function<void(std::size_t, std::size_t, std::string_view)>& progress_cb,
    std::stop_token stop) {
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

    for (size_t i = 0; i < blocks; ++i) {
        check_interrupted();
        if (stop.stop_requested()) {
            throw std::runtime_error("Operation interrupted");
        }
        ssize_t written = ::write(fd.get(), buffer.data(), block_size);
        if (written != static_cast<ssize_t>(block_size)) {
            throw std::system_error(errno, std::generic_category(), "Disk write failed");
        }
        if (progress_cb && i % 2 == 0) progress_cb(i + 1, blocks, label);
    }
    if (progress_cb) progress_cb(blocks, blocks, label);

    ::fdatasync(fd.get());
    auto end = high_resolution_clock::now();
    std::error_code ec; std::filesystem::remove(filename, ec);

    duration<double> diff = end - start;
    double speed = diff.count() <= 0 ? 0.0 : static_cast<double>(size_mb) / diff.count();
    return DiskRunResult{std::string(label), speed};
}

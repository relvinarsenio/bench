#include "include/interrupts.hpp"

volatile sig_atomic_t g_interrupted = 0;

void signal_handler(int) noexcept {
    g_interrupted = 1;
}

void check_interrupted() {
    if (g_interrupted) {
        throw std::runtime_error("Operation interrupted by user");
    }
}

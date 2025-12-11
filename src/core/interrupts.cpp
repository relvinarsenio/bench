#include "include/interrupts.hpp"

std::atomic_flag g_interrupted = ATOMIC_FLAG_INIT;

void signal_handler(int) {
    g_interrupted.test_and_set();
}

void check_interrupted() {
    if (g_interrupted.test()) {
        throw std::runtime_error("Operation interrupted by user");
    }
}

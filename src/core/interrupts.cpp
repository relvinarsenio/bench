#include "include/interrupts.hpp"

#include <csignal>
#include <stdexcept>

std::atomic<bool> g_interrupted{false};

void signal_handler(int) noexcept {
    g_interrupted = true;
}

void check_interrupted() {
    if (g_interrupted) {
        throw std::runtime_error("Operation interrupted by user");
    }
}

SignalGuard::SignalGuard() {
    struct sigaction sa = {};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}
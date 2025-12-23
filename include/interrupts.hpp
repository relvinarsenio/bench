#pragma once

#include <csignal>
#include <stdexcept>
#include <atomic>

extern std::atomic<bool> g_interrupted;

void signal_handler(int) noexcept;
void check_interrupted();

class SignalGuard {
public:
    SignalGuard();
    ~SignalGuard() = default;
    
    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;
};
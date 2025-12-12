#pragma once

#include <csignal>
#include <stdexcept>

extern volatile sig_atomic_t g_interrupted;

void signal_handler(int) noexcept;
void check_interrupted();

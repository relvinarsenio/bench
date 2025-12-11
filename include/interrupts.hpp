#pragma once

#include <atomic>
#include <stdexcept>

extern std::atomic_flag g_interrupted;

void signal_handler(int);
void check_interrupted();

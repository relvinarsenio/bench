#pragma once

#include <string>

class SystemInfo {
    static const std::string& get_cpuinfo_cache();
public:
    static std::string get_model_name();
    static std::string get_cpu_cores_freq();
    static std::string get_cpu_cache();
    static bool has_aes();
    static bool has_vmx();
    static std::string get_virtualization();
    static std::string get_os();
    static std::string get_arch();
    static std::string get_kernel();
    static std::string get_tcp_cc();
    static std::string get_uptime();
    static std::string get_load_avg();
    static std::string get_swap_details();
};

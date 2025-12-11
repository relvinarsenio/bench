#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <format>
#include <print>
#include <chrono>
#include <memory>
#include <array>
#include <algorithm>
#include <cstring>
#include <csignal>
#include <bit>          
#include <system_error> 
#include <atomic>
#include <set>

#include <unistd.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <sys/sysinfo.h>

#include <curl/curl.h>

namespace fs = std::filesystem;
using namespace std::chrono;
using namespace std::string_literals;

namespace Config {
    constexpr size_t IO_BLOCK_SIZE = 4 * 1024 * 1024; 
    constexpr size_t IO_ALIGNMENT = 4096;
    constexpr std::string_view BENCH_FILENAME = "benchtest_file";
    constexpr std::string_view SPEEDTEST_CLI_PATH = "./speedtest-cli/speedtest";
    constexpr std::string_view SPEEDTEST_TGZ = "speedtest.tgz";
}

std::atomic_flag g_interrupted = ATOMIC_FLAG_INIT;

void signal_handler(int) {
    g_interrupted.test_and_set();
}

void check_interrupted() {
    if (g_interrupted.test()) {
        throw std::runtime_error("Operation interrupted by user");
    }
}

class FileDescriptor {
    int fd_ = -1;
public:
    explicit FileDescriptor(int fd) : fd_(fd) {
        if (fd_ < 0) {
            throw std::system_error(errno, std::generic_category(), "Invalid file descriptor");
        }
    }
    ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return fd_; }
};

class ShellPipe {
    FILE* pipe_ = nullptr;
public:
    ShellPipe(const std::string& cmd) {
        pipe_ = ::popen(cmd.c_str(), "r");
        if (!pipe_) {
            throw std::system_error(errno, std::generic_category(), "popen failed for: " + cmd);
        }
    }
    ~ShellPipe() { if (pipe_) ::pclose(pipe_); }
    
    FILE* get() const { return pipe_; }
    
    std::string read_all() {
        std::array<char, 256> buffer;
        std::string result;
        while (::fgets(buffer.data(), buffer.size(), pipe_) != nullptr) {
            check_interrupted();
            result += buffer.data();
        }
        return result;
    }
};

namespace Color {
    constexpr std::string_view RESET  = "\033[0m";
    constexpr std::string_view RED    = "\033[31m";
    constexpr std::string_view GREEN  = "\033[32m";
    constexpr std::string_view YELLOW = "\033[33m";
    constexpr std::string_view CYAN   = "\033[36m"; 
    constexpr std::string_view BOLD   = "\033[1m";

    std::string colorize(std::string_view text, std::string_view color) {
        return std::format("{}{}{}", color, text, RESET);
    }
}

void print_line() {
    std::println("{}", std::string(78, '-')); 
    std::cout << std::flush;
}

std::string trim(const std::string& str) {
    auto first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return "";
    auto last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string_view trim_sv(std::string_view str) {
    auto first = str.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) return {};
    auto last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string format_bytes(uint64_t bytes) {
    if (bytes == 0) return "0";
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double d = static_cast<double>(bytes);
    while (d >= 1024 && i < 4) {
        d /= 1024;
        i++;
    }
    return std::format("{:.1f} {}", d, units[i]);
}

void cleanup_artifacts() {
    const std::vector<std::string> paths = {
        std::string(Config::SPEEDTEST_TGZ), 
        "speedtest-cli", 
        std::string(Config::BENCH_FILENAME)
    };
    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            fs::remove_all(p, ec);
        }
    }
}

class SystemInfo {
    static const std::string& get_cpuinfo_cache() {
        static std::string cache = []{
            std::ifstream f("/proc/cpuinfo");
            std::stringstream buffer;
            buffer << f.rdbuf();
            return buffer.str();
        }();
        return cache;
    }

public:
    static std::string get_model_name() {
        std::stringstream ss(get_cpuinfo_cache());
        std::string line;
        while (std::getline(ss, line)) {
            if (line.find("model name") != std::string::npos) {
                std::string raw = line.substr(line.find(':') + 1);
                return std::string(trim_sv(raw));
            }
        }
        return "Unknown CPU";
    }

    static std::string get_cpu_cores_freq() {
        int cores = get_nprocs();
        double freq_mhz = 0.0;
        
        std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
        if (f >> freq_mhz) {
            freq_mhz /= 1000.0;
        } else {
            std::stringstream ss(get_cpuinfo_cache());
            std::string line;
            while(std::getline(ss, line)) {
                if (line.starts_with("cpu MHz")) {
                    try {
                        freq_mhz = std::stod(line.substr(line.find(':') + 1));
                        break;
                    } catch (...) {}
                }
            }
        }
        return std::format("{} @ {:.1f} MHz", cores, freq_mhz);
    }

    static std::string get_cpu_cache() {
        auto parse_cache = [](std::string s) -> std::string {
            s = trim(s);
            if (s.empty()) return "Unknown";
            uint64_t size = 0;
            try {
                size_t idx;
                size = std::stoull(s, &idx);
                if (idx < s.size()) {
                    char suffix = std::toupper(s[idx]);
                    if (suffix == 'K') size *= 1024;
                    else if (suffix == 'M') size *= 1024 * 1024;
                } else {
                    size *= 1024; 
                }
            } catch (...) { return s; }

            if (size >= 1024 * 1024) return std::format("{:.0f} MB", size / (1024.0 * 1024.0));
            if (size >= 1024) return std::format("{:.0f} KB", size / 1024.0);
            return std::format("{} B", size);
        };

        std::vector<std::string> caches = {"3", "2", "1", "0"};
        for(const auto& idx : caches) {
            std::string path = "/sys/devices/system/cpu/cpu0/cache/index" + idx + "/size";
            std::ifstream f(path);
            std::string size;
            if (f >> size) return parse_cache(size);
        }
        return "Unknown";
    }

    static bool has_aes() {
        return get_cpuinfo_cache().find("aes") != std::string::npos;
    }

    static bool has_vmx() {
        const auto& content = get_cpuinfo_cache();
        return content.find("vmx") != std::string::npos || content.find("svm") != std::string::npos;
    }

    static std::string get_virtualization() {
        auto get_cpuid_vendor = [](unsigned int leaf) -> std::string {
            #if defined(__x86_64__) || defined(__i386__)
            unsigned int eax, ebx, ecx, edx;
            __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf));
            std::array<char, 13> vendor;
            auto copy_reg = [&](unsigned int reg, size_t offset) {
                auto bytes = std::bit_cast<std::array<char, 4>>(reg);
                std::copy(bytes.begin(), bytes.end(), vendor.begin() + offset);
            };
            copy_reg(ebx, 0); copy_reg(ecx, 4); copy_reg(edx, 8);
            vendor[12] = '\0';
            return std::string(vendor.data());
            #else
            return std::string("");
            #endif
        };

        struct utsname buffer;
        if (uname(&buffer) == 0) {
            std::string release = buffer.release;
            if (release.find("Microsoft") != std::string::npos || 
                release.find("WSL") != std::string::npos) {
                return "WSL";
            }
        }

        bool hv_bit = false;
        #if defined(__x86_64__) || defined(__i386__)
        unsigned int eax, ebx, ecx, edx;
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        if (ecx & (1 << 31)) hv_bit = true;
        #endif

        if (hv_bit) {
            std::string sig = get_cpuid_vendor(0x40000000);
            if (sig == "KVMKVMKVM") return "KVM";
            if (sig == "Microsoft Hv") return "Hyper-V";
            if (sig == "VMwareVMware") return "VMware";
            if (sig == "XenVMMXenVMM") return "Xen";
            if (sig == "VBoxVBoxVBox") return "VirtualBox";
            if (sig == "prl hyperv  ") return "Parallels";
            if (sig == "TCGTCGTCGTCG") return "QEMU"; 
        }

        auto read_file = [](const std::string& p) {
            std::ifstream f(p); std::string s; std::getline(f, s); return trim(s);
        };
        
        if (fs::exists("/proc/1/environ")) {
            std::ifstream f("/proc/1/environ");
            std::string env;
            while (std::getline(f, env, '\0')) {
                if (env.find("container=lxc") != std::string::npos) return "LXC";
            }
        }

        if (fs::exists("/.dockerenv") || fs::exists("/run/.containerenv")) return "Docker";
        if (fs::exists("/proc/user_beancounters")) return "OpenVZ";
        
        std::string product = read_file("/sys/class/dmi/id/product_name");
        if (product.find("KVM") != std::string::npos) return "KVM";
        if (product.find("QEMU") != std::string::npos) return "QEMU";
        if (product.find("VirtualBox") != std::string::npos) return "VirtualBox";

        return hv_bit ? "Dedicated (Virtual)" : "Dedicated";
    }

    static std::string get_os() {
        std::ifstream f("/etc/os-release");
        std::string line;
        while(std::getline(f, line)) {
            if (line.starts_with("PRETTY_NAME=")) {
                auto val = line.substr(12);
                if (val.size() >= 2 && val.front() == '"') val = val.substr(1, val.size()-2);
                return val;
            }
        }
        return "Linux";
    }

    static std::string get_arch() {
        struct utsname buffer;
        if (uname(&buffer) == 0) {
            std::string arch = buffer.machine;
            int bits = sizeof(void*) * 8;
            return std::format("{} ({} Bit)", arch, bits);
        }
        return "Unknown";
    }

    static std::string get_kernel() {
        struct utsname buffer;
        if (uname(&buffer) == 0) return buffer.release;
        return "Unknown";
    }

    static std::string get_tcp_cc() {
        std::ifstream f("/proc/sys/net/ipv4/tcp_congestion_control");
        std::string s; 
        if(f >> s) return s;
        return "Unknown";
    }

    static std::string get_uptime() {
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            long up = si.uptime;
            int days = up / 86400;
            int hours = (up % 86400) / 3600;
            int mins = (up % 3600) / 60;
            return std::format("{} days, {} hour {} min", days, hours, mins);
        }
        return "Unknown";
    }

    static std::string get_load_avg() {
        double loads[3];
        if (getloadavg(loads, 3) != -1) {
            return std::format("{:.2f}, {:.2f}, {:.2f}", loads[0], loads[1], loads[2]);
        }
        return "Unknown";
    }

    static std::string get_swap_details() {
        std::ifstream f("/proc/swaps");
        std::string line;
        std::set<std::string> types;
        
        if (!std::getline(f, line)) return ""; 

        while (std::getline(f, line)) {
            std::stringstream ss(line);
            std::string filename, type;
            ss >> filename >> type;
            
            if (filename.find("zram") != std::string::npos) {
                types.insert("zram");
            } else {
                types.insert(type);
            }
        }

        if (types.empty()) return "";

        std::string details;
        for (const auto& t : types) {
            if (!details.empty()) details += ", ";
            details += t;
        }

        std::ifstream z("/sys/module/zswap/parameters/enabled");
        char c;
        if (z >> c && (c == 'Y' || c == 'y' || c == '1')) {
            details += " + zswap";
        }
        
        return details;
    }
};

class HttpClient {
    using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlPtr handle_;

    static size_t write_string(void* ptr, size_t size, size_t nmemb, std::string* s) {
        s->append(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    }

    static size_t write_file(void* ptr, size_t size, size_t nmemb, std::ofstream* f) {
        f->write(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    }

public:
    HttpClient() : handle_(curl_easy_init(), curl_easy_cleanup) {
        if (!handle_) throw std::runtime_error("Failed to create curl handle");
    }

    std::string get(const std::string& url) {
        curl_easy_reset(handle_.get());
        
        std::string response;
        CURL* curl = handle_.get();
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L); 
        
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, 
            +[](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return g_interrupted.test() ? 1 : 0;
            });
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        CURLcode res = curl_easy_perform(curl);
        check_interrupted(); 

        if (res != CURLE_OK) {
            throw std::runtime_error(std::format("Network error: {}", curl_easy_strerror(res)));
        }
        return response;
    }

    void download(const std::string& url, const std::string& filepath) {
        curl_easy_reset(handle_.get());
        
        std::ofstream outfile(filepath, std::ios::binary);
        if (!outfile) throw std::system_error(errno, std::generic_category(), "Failed to create file");

        CURL* curl = handle_.get();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outfile);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, 
            +[](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return g_interrupted.test() ? 1 : 0;
            });
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        CURLcode res = curl_easy_perform(curl);
        check_interrupted();

        if (res != CURLE_OK) {
            outfile.close();
            fs::remove(filepath); 
            throw std::runtime_error(std::format("Download failed: {}", curl_easy_strerror(res)));
        }
    }

    bool check_connectivity(const std::string& host) {
        try {
            curl_easy_reset(handle_.get());
            CURL* curl = handle_.get();
            curl_easy_setopt(curl, CURLOPT_URL, ("http://" + host).c_str());
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); 
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L); 
            return curl_easy_perform(curl) == CURLE_OK;
        } catch (...) {
            return false;
        }
    }
};

class DiskBenchmark {
    struct AlignedFree {
        void operator()(void* ptr) { std::free(ptr); }
    };
    using AlignedBuffer = std::unique_ptr<void, AlignedFree>;

    static AlignedBuffer allocate_aligned(size_t size, size_t alignment = 4096) {
        if (size % alignment != 0) size = (size + alignment - 1) & ~(alignment - 1);
        void* ptr = std::aligned_alloc(alignment, size);
        if (!ptr) throw std::bad_alloc();
        return AlignedBuffer(ptr);
    }

public:
    static double run_write_test(int size_mb, std::string_view label) {
        const std::string filename(Config::BENCH_FILENAME);
        const size_t block_size = Config::IO_BLOCK_SIZE;
        
        auto buffer = allocate_aligned(block_size, Config::IO_ALIGNMENT);
        std::memset(buffer.get(), 0, block_size);

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
            ssize_t written = ::write(fd.get(), buffer.get(), block_size);
            if (written != static_cast<ssize_t>(block_size)) {
                throw std::system_error(errno, std::generic_category(), "Disk write failed");
            }
            if (i % 2 == 0) print_progress(i + 1, blocks);
        }
        print_progress(blocks, blocks);
        
        ::fdatasync(fd.get());
        auto end = high_resolution_clock::now();
        std::error_code ec; fs::remove(filename, ec);
        
        std::print("\r{}", label); 
        std::cout << std::flush;

        duration<double> diff = end - start;
        if (diff.count() <= 0) return 0.0;
        return static_cast<double>(size_mb) / diff.count();
    }
};

class SpeedTest {
    HttpClient& http_;
    const std::string cli_path{Config::SPEEDTEST_CLI_PATH};

    std::string get_arch() {
        struct utsname buf; uname(&buf);
        std::string m(buf.machine);
        if (m == "x86_64") return "x86_64";
        if (m == "aarch64" || m == "arm64") return "aarch64";
        if (m == "i386" || m == "i686") return "i386";
        if (m == "armv7l") return "armhf";
        throw std::runtime_error("Unsupported architecture: " + m);
    }

    std::vector<std::string> parse_csv_line(const std::string& line) {
        std::vector<std::string> tokens;
        bool in_quotes = false;
        std::string current_token;
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes; 
            } else if (c == ',' && !in_quotes) {
                tokens.push_back(current_token);
                current_token.clear();
            } else {
                current_token += c;
            }
        }
        tokens.push_back(current_token);
        return tokens;
    }

public:
    SpeedTest(HttpClient& h) : http_(h) {}

    void install() {
        if (fs::exists(cli_path)) return;
        
        std::println("Downloading Speedtest CLI...");
        std::string url = std::format("https://install.speedtest.net/app/cli/ookla-speedtest-1.2.0-linux-{}.tgz", get_arch());
        
        try {
            http_.download(url, std::string(Config::SPEEDTEST_TGZ));
        } catch (...) {
            throw;
        }

        fs::create_directories("speedtest-cli");
        std::string tar_cmd = std::format("tar zxf {} -C ./speedtest-cli 2>&1", Config::SPEEDTEST_TGZ);
        ShellPipe pipe(tar_cmd);
        pipe.read_all(); 
        
        if (!fs::exists(cli_path)) throw std::runtime_error("Failed to extract speedtest-cli");
        
        fs::permissions(cli_path, fs::perms::owner_all, fs::perm_options::add);
        fs::remove(Config::SPEEDTEST_TGZ);
    }

    void run() {
        struct Node { std::string id; std::string name; };
        
        std::vector<Node> nodes = {
            {"", "Speedtest.net (Auto)"}, 
            {"3864", "Los Angeles, US"}, 
            {"57114", "Dallas, US"},      
            {"12723",  "Montreal, CA"},    
            {"5976",  "Paris, FR"},       
            {"3386",  "Amsterdam, NL"},   
            {"22223",  "Beijing, CN"},     
            {"3633", "Shanghai, CN"},    
            {"17251", "Guangzhou, CN"},   
            {"1536",  "Hong Kong, CN"},   
            {"13623", "Singapore, SG"},   
            {"22247", "Tokyo, JP"}        
        };

        std::println("{:<24}{:<18}{:<18}{:<12}{:<8}", " Node Name", "Upload", "Download", "Latency", "Loss");

        for (const auto& node : nodes) {
            check_interrupted();
            
            std::string cmd = cli_path + " -f csv --accept-license --accept-gdpr";
            if (!node.id.empty()) cmd += " --server-id=" + node.id;
            cmd += " 2>&1"; 

            try {
                ShellPipe pipe(cmd);
                std::string output = pipe.read_all();
                std::stringstream ss(output);
                std::string line;
                bool success = false;

                while(std::getline(ss, line)) {
                    if (line.find("[error]") != std::string::npos) {
                        std::string clean_err = line;
                        size_t msg_pos = line.find("] [error] ");
                        if (msg_pos != std::string::npos) {
                            clean_err = line.substr(msg_pos + 10); 
                        } else {
                            msg_pos = line.find("[error] ");
                            if (msg_pos != std::string::npos) clean_err = line.substr(msg_pos + 8);
                        }
                        
                        if (clean_err.find("Limit reached") != std::string::npos) {
                            std::print("{}{:<24}{}Error: Rate Limit (Too many requests). Aborting.{}\n", 
                                Color::YELLOW, " " + node.name, Color::RED, Color::RESET);
                            return; 
                        }
                        else if (clean_err.find("No servers defined") != std::string::npos) {
                             std::print("{}{:<24}{}Error: Server ID Changed/Offline{}\n", 
                                Color::YELLOW, " " + node.name, Color::RED, Color::RESET);
                             success = true; 
                             break;
                        }
                        continue;
                    }

                    if (line.find(',') != std::string::npos && line.find('"') != std::string::npos) {
                        auto cols = parse_csv_line(line);
                        
                        if (cols.size() >= 7) {
                            try {
                                double dl_bytes = std::stod(cols[5]);
                                double ul_bytes = std::stod(cols[6]);
                                double lat_val = std::stod(cols[2]);
                                std::string loss = cols[4];

                                double dl_mbps = (dl_bytes * 8.0) / 1000000.0;
                                double ul_mbps = (ul_bytes * 8.0) / 1000000.0;
                                
                                if (loss == "0") loss += "%";
                                if (loss == "N/A") loss = "-";
                                
                                std::print("{}{:<24}{}{:<18}{}{:<18}{}{:<12}{}{:<8}{}\n", 
                                    Color::YELLOW, " " + node.name,
                                    Color::GREEN, std::format("{:.2f} Mbps", ul_mbps),
                                    Color::RED,   std::format("{:.2f} Mbps", dl_mbps),
                                    Color::CYAN,  std::format("{:.2f} ms", lat_val),
                                    Color::RED,   loss,
                                    Color::RESET);
                                
                                success = true;
                                break; 
                            } catch (...) { continue; }
                        }
                    }
                }

                if (!success) {
                    std::string error_msg = output;
                    if (!output.empty() && output.back() == '\n') output.pop_back();
                    
                    size_t err_idx = output.find("[error] ");
                    if (err_idx != std::string::npos) {
                        error_msg = output.substr(err_idx + 8);
                    }

                    if (error_msg.length() > 45) error_msg = error_msg.substr(0, 42) + "...";

                    std::print("{}{:<24}{}Error: {}{}\n", 
                        Color::YELLOW, " " + node.name, Color::RED, error_msg, Color::RESET);
                }
                
            } catch (const std::exception& e) {
                std::print("{}{:<24}{}Error: {}{}\n", Color::YELLOW, " " + node.name, Color::RED, e.what(), Color::RESET);
            }
            std::cout << std::flush;
        }
    }
};

void run_app(std::string_view app_path) {
    HttpClient http;
    auto start_time = high_resolution_clock::now();

    std::string app_name = fs::path(app_path).filename().string();
    if (app_name.empty()) app_name = "bench";

    std::print("\033c"); 
    std::cout << std::flush;
    print_line();
    std::println(" A Bench Script (Wrapper CSV Edition v6.9.5)");
    std::println(" Usage : ./{}", app_name);
    print_line();

    std::println(" -> {}", Color::colorize("CPU & Hardware", Color::BOLD));
    std::println(" {:<20} : {}", "CPU Model", Color::colorize(SystemInfo::get_model_name(), Color::CYAN));
    std::println(" {:<20} : {}", "CPU Cores", Color::colorize(SystemInfo::get_cpu_cores_freq(), Color::CYAN));
    std::println(" {:<20} : {}", "CPU Cache", Color::colorize(SystemInfo::get_cpu_cache(), Color::CYAN));
    std::println(" {:<20} : {}", "AES-NI", SystemInfo::has_aes() ? Color::colorize("\u2713 Enabled", Color::GREEN) : Color::colorize("\u2717 Disabled", Color::RED));
    std::println(" {:<20} : {}", "VM-x/AMD-V", SystemInfo::has_vmx() ? Color::colorize("\u2713 Enabled", Color::GREEN) : Color::colorize("\u2717 Disabled", Color::RED));

    std::println("\n -> {}", Color::colorize("System Info", Color::BOLD));
    std::println(" {:<20} : {}", "OS", Color::colorize(SystemInfo::get_os(), Color::CYAN));
    std::println(" {:<20} : {}", "Arch", Color::colorize(SystemInfo::get_arch(), Color::YELLOW)); 
    std::println(" {:<20} : {}", "Kernel", Color::colorize(SystemInfo::get_kernel(), Color::YELLOW)); 
    std::println(" {:<20} : {}", "TCP CC", Color::colorize(SystemInfo::get_tcp_cc(), Color::YELLOW)); 
    std::println(" {:<20} : {}", "Virtualization", Color::colorize(SystemInfo::get_virtualization(), Color::CYAN));
    std::println(" {:<20} : {}", "System Uptime", Color::colorize(SystemInfo::get_uptime(), Color::CYAN));
    std::println(" {:<20} : {}", "Load Average", Color::colorize(SystemInfo::get_load_avg(), Color::YELLOW)); 
    
    struct sysinfo si; sysinfo(&si);
    uint64_t total_ram = si.totalram * si.mem_unit;
    uint64_t total_swap = si.totalswap * si.mem_unit;
    uint64_t used_swap = (si.totalswap - si.freeswap) * si.mem_unit;
    
    uint64_t available_ram = 0;
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while(std::getline(meminfo, line)) {
        if(line.starts_with("MemAvailable:")) {
            std::stringstream ss(line);
            std::string key, unit;
            uint64_t val;
            ss >> key >> val;
            available_ram = val * 1024;
            break;
        }
    }
    if(available_ram == 0) available_ram = si.freeram * si.mem_unit;
    uint64_t used_ram = total_ram - available_ram;
    
    struct statvfs disk; statvfs("/", &disk);
    uint64_t total_disk = disk.f_blocks * disk.f_frsize;
    uint64_t used_disk = (disk.f_blocks - disk.f_bfree) * disk.f_frsize;

    std::println("\n -> {}", Color::colorize("Storage & Memory", Color::BOLD));
    std::println(" {:<20} : {} ({} Used)", "Total Disk", Color::colorize(format_bytes(total_disk), Color::YELLOW), Color::colorize(format_bytes(used_disk), Color::CYAN));
    std::println(" {:<20} : {} ({} Used)", "Total Mem", Color::colorize(format_bytes(total_ram), Color::YELLOW), Color::colorize(format_bytes(used_ram), Color::CYAN));
    if (total_swap > 0) {
        std::string details = SystemInfo::get_swap_details();
        if (!details.empty()) details = " (" + details + ")";
        std::println(" {:<20} : {} ({} Used){}", "Total Swap", Color::colorize(format_bytes(total_swap), Color::YELLOW), Color::colorize(format_bytes(used_swap), Color::CYAN), Color::colorize(details, Color::CYAN));
    }

    std::println("\n -> {}", Color::colorize("Network", Color::BOLD));
    bool v4 = http.check_connectivity("ipv4.google.com");
    bool v6 = http.check_connectivity("ipv6.google.com");
    std::print(" {:<20} : {} / {}\n", "IPv4/IPv6", 
        v4 ? Color::colorize("\u2713 Online", Color::GREEN) : Color::colorize("\u2717 Offline", Color::RED),
        v6 ? Color::colorize("\u2713 Online", Color::GREEN) : Color::colorize("\u2717 Offline", Color::RED)
    );

    try {
        std::string json = http.get("http://ipinfo.io/json");
        auto extract = [&](std::string_view key) -> std::string {
            std::string search_key = std::format("\"{}\":", key);
            size_t pos = json.find(search_key);
            if (pos == std::string::npos) return "";
            
            pos += search_key.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;
            
            size_t end = json.find('"', pos);
            if (end == std::string::npos) return "";
            
            return json.substr(pos, end - pos);
        };

        std::string org = extract("org");
        if (!org.empty()) std::println(" {:<20} : {}", "ISP", Color::colorize(org, Color::CYAN));
        std::println(" {:<20} : {} / {}", "Location", Color::colorize(extract("city"), Color::CYAN), Color::colorize(extract("country"), Color::CYAN));
    } catch (const std::exception&) {
        std::println(" {:<20} : {}", "IP Info", Color::colorize("Failed to fetch", Color::RED));
    }

    print_line();

    std::println("Running I/O Test (1GB File)...");
    double total_speed = 0.0;
    for(int i=1; i<=3; ++i) {
        std::string label = std::format(" I/O Speed (Run #{}) : ", i);
        double speed = DiskBenchmark::run_write_test(1024, label);
        total_speed += speed;
        std::println("{}", Color::colorize(std::format("{:.1f} MB/s", speed), Color::YELLOW));
    }
    std::println(" I/O Speed (Average) : {}", Color::colorize(std::format("{:.1f} MB/s", total_speed / 3.0), Color::YELLOW));

    print_line();

    SpeedTest st(http);
    st.install();
    st.run();
    
    print_line();
    auto end_time = high_resolution_clock::now();
    std::println(" Finished in        : {:.0f} sec", duration<double>(end_time - start_time).count());
}

int main(int argc, char* argv[]) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        std::println(stderr, "Failed to init libcurl");
        return 1;
    }
    
    struct CurlGlobalCleaner { ~CurlGlobalCleaner() { curl_global_cleanup(); } } cleaner;

    struct sigaction sa = {};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    try {
        std::string_view app_path = (argc > 0) ? argv[0] : "bench";
        run_app(app_path);
    } catch (const std::exception& e) {
        std::println(stderr, "\n{}Error: {}{}", Color::RED, e.what(), Color::RESET);
        cleanup_artifacts();
        return 1;
    }

    cleanup_artifacts();
    return 0;
}

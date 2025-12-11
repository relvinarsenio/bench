#pragma once

#include <curl/curl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

class HttpClient {
    using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlPtr handle_;

    static size_t write_string(void* ptr, size_t size, size_t nmemb, std::string* s) noexcept;
    static size_t write_file(void* ptr, size_t size, size_t nmemb, std::ofstream* f) noexcept;

public:
    HttpClient();
    std::string get(const std::string& url);
    void download(const std::string& url, const std::string& filepath);
    bool check_connectivity(const std::string& host);
};

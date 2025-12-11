#include "include/http_client.hpp"

#include <cerrno>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "include/interrupts.hpp"

HttpClient::HttpClient() : handle_(curl_easy_init(), curl_easy_cleanup) {
    if (!handle_) throw std::runtime_error("Failed to create curl handle");
}

size_t HttpClient::write_string(void* ptr, size_t size, size_t nmemb, std::string* s) noexcept {
    try {
        s->append(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    } catch (...) {
        return 0;
    }
}

size_t HttpClient::write_file(void* ptr, size_t size, size_t nmemb, std::ofstream* f) noexcept {
    try {
        f->write(static_cast<char*>(ptr), size * nmemb);
        if (!*f) return 0;
        return size * nmemb;
    } catch (...) {
        return 0;
    }
}

std::string HttpClient::get(const std::string& url) {
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

void HttpClient::download(const std::string& url, const std::string& filepath) {
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
        std::filesystem::remove(filepath);
        throw std::runtime_error(std::format("Download failed: {}", curl_easy_strerror(res)));
    }
}

bool HttpClient::check_connectivity(const std::string& host) {
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

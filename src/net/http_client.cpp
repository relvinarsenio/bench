#include "include/http_client.hpp"
#include "include/interrupts.hpp"

#include <cerrno>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace {

struct CurlSlistDeleter {
    void operator()(struct curl_slist* list) const noexcept {
        if (list) curl_slist_free_all(list);
    }
};

class CurlHeaders {
    std::unique_ptr<struct curl_slist, CurlSlistDeleter> list_;

public:
    void add(const std::string& header) {
        auto new_head = curl_slist_append(list_.get(), header.c_str());

        if (new_head && !list_) {
            list_.reset(new_head);
        }
    }

    struct curl_slist* get() const { return list_.get(); }
};

}

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
    
    CurlHeaders headers;
    headers.add("Accept-Language: en-US,en;q=0.9");
    headers.add("Accept: application/json, text/javascript, */*; q=0.01");

    curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, write_string);
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle_.get(), CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(handle_.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle_.get(), CURLOPT_REFERER, "https://www.google.com/");
    curl_easy_setopt(handle_.get(), CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle_.get(), CURLOPT_TCP_KEEPALIVE, 1L);

    curl_easy_setopt(handle_.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return g_interrupted ? 1 : 0;
        });
    curl_easy_setopt(handle_.get(), CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(handle_.get());
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

    curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle_.get(), CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(handle_.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle_.get(), CURLOPT_REFERER, "https://www.google.com/");
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, write_file);
    curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, &outfile);
    curl_easy_setopt(handle_.get(), CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(handle_.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return g_interrupted ? 1 : 0;
        });
    curl_easy_setopt(handle_.get(), CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(handle_.get());
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
        
        curl_easy_setopt(handle_.get(), CURLOPT_URL, ("http://" + host).c_str());
        curl_easy_setopt(handle_.get(), CURLOPT_NOBODY, 1L);
        curl_easy_setopt(handle_.get(), CURLOPT_TIMEOUT, 3L);
        
        return curl_easy_perform(handle_.get()) == CURLE_OK;
    } catch (...) {
        return false;
    }
}
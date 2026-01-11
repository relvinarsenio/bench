/*
 * Copyright (c) 2025 Alfie Ardinata
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "include/http_context.hpp"

#include <stdexcept>

#include <curl/curl.h>
#include <openssl/crypto.h>

HttpContext::HttpContext() {
    if (OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr) == 0) {
        throw std::runtime_error("Failed to initialize OpenSSL crypto library");
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        throw std::runtime_error("Failed to initialize libcurl globally");
    }
}

HttpContext::~HttpContext() {
    curl_global_cleanup();
}
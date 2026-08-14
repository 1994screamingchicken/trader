#include "kraken/auth.hpp"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <chrono>
#include <cstring>
#include <vector>
#include <stdexcept>

namespace trader {

namespace {

// Base64 encode
std::string base64_encode(const unsigned char* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

// Base64 decode
std::vector<unsigned char> base64_decode(const std::string& encoded) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
    bmem = BIO_push(b64, bmem);
    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);

    std::vector<unsigned char> buffer(encoded.size());
    int decoded_len = BIO_read(bmem, buffer.data(), static_cast<int>(buffer.size()));
    BIO_free_all(bmem);

    if (decoded_len < 0) {
        throw std::runtime_error("Base64 decode failed");
    }
    buffer.resize(decoded_len);
    return buffer;
}

// SHA-256 hash
std::vector<unsigned char> sha256(const std::string& input) {
    std::vector<unsigned char> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(), hash.data());
    return hash;
}

// HMAC-SHA512
std::vector<unsigned char> hmac_sha512(const std::vector<unsigned char>& key,
                                        const std::vector<unsigned char>& data) {
    unsigned int len = EVP_MAX_MD_SIZE;
    std::vector<unsigned char> result(len);

    HMAC(EVP_sha512(),
         key.data(), static_cast<int>(key.size()),
         data.data(), data.size(),
         result.data(), &len);

    result.resize(len);
    return result;
}

}  // namespace

KrakenAuth::KrakenAuth(const std::string& api_key, const std::string& api_secret)
    : api_key_(api_key), api_secret_(api_secret) {}

std::string KrakenAuth::generate_nonce() const {
    auto now = std::chrono::system_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    return std::to_string(micros);
}

std::string KrakenAuth::compute_signature(const std::string& uri_path,
                                           const std::string& nonce,
                                           const std::string& post_data) const {
    // Step 1: SHA256(nonce + post_data)
    std::string nonce_postdata = nonce + post_data;
    auto sha256_hash = sha256(nonce_postdata);

    // Step 2: Concatenate uri_path bytes + SHA256 hash
    std::vector<unsigned char> message;
    message.insert(message.end(), uri_path.begin(), uri_path.end());
    message.insert(message.end(), sha256_hash.begin(), sha256_hash.end());

    // Step 3: HMAC-SHA512(base64_decode(api_secret), message)
    auto decoded_secret = base64_decode(api_secret_);
    auto hmac_result = hmac_sha512(decoded_secret, message);

    // Step 4: Base64 encode the result
    return base64_encode(hmac_result.data(), hmac_result.size());
}

bool KrakenAuth::has_credentials() const {
    return !api_key_.empty() && !api_secret_.empty();
}

}  // namespace trader

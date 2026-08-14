#pragma once

#include <string>
#include <cstdint>

namespace trader {

/// Kraken API authentication utilities
class KrakenAuth {
public:
    KrakenAuth(const std::string& api_key, const std::string& api_secret);

    /// Get the API key for request headers
    const std::string& api_key() const { return api_key_; }

    /// Generate a nonce for private API requests
    std::string generate_nonce() const;

    /// Compute the API-Sign header value for a private request
    /// @param uri_path The API endpoint path (e.g., "/0/private/Balance")
    /// @param nonce The nonce used in the request
    /// @param post_data The complete POST data string
    /// @return The Base64-encoded HMAC-SHA512 signature
    std::string compute_signature(const std::string& uri_path,
                                  const std::string& nonce,
                                  const std::string& post_data) const;

    /// Check if credentials are configured
    bool has_credentials() const;

private:
    std::string api_key_;
    std::string api_secret_;  // Base64-encoded secret from Kraken
};

}  // namespace trader

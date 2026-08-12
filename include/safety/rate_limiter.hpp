#pragma once

#include <string>
#include <mutex>
#include <chrono>
#include <deque>
#include <unordered_map>
#include <atomic>

#include "core/config.hpp"

namespace trader {

/// Token bucket / sliding window rate limiter for API calls
class RateLimiter {
public:
    explicit RateLimiter(const RateLimitConfig& config);

    /// Check if a request is allowed for a given script.
    /// Returns true if allowed, false if rate limited.
    bool allow_request(const std::string& script_name);

    /// Check if a global (non-script-specific) request is allowed
    bool allow_global_request();

    /// Check if currently in cooldown period
    bool in_cooldown() const { return in_cooldown_; }

    /// Get remaining cooldown time in seconds
    int cooldown_remaining() const;

    /// Reset all rate limit state
    void reset();

    /// Get statistics
    int total_allowed() const { return total_allowed_; }
    int total_rejected() const { return total_rejected_; }
    int global_calls_last_minute() const;

private:
    /// Sliding window rate check
    bool check_global_limits();

    /// Per-script sliding window rate check
    bool check_script_limit(const std::string& script_name);

    /// Enter cooldown mode
    void enter_cooldown();

    /// Check if cooldown has expired
    bool check_cooldown_expired();

    RateLimitConfig config_;

    mutable std::mutex mutex_;

    // Global sliding windows
    std::deque<std::chrono::steady_clock::time_point> global_second_window_;
    std::deque<std::chrono::steady_clock::time_point> global_minute_window_;

    // Per-script sliding windows (minute granularity)
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> script_windows_;

    // Cooldown state
    std::atomic<bool> in_cooldown_{false};
    std::chrono::steady_clock::time_point cooldown_start_;

    // Statistics
    std::atomic<int> total_allowed_{0};
    std::atomic<int> total_rejected_{0};
};

}  // namespace trader

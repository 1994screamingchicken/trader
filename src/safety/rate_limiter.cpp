#include "safety/rate_limiter.hpp"
#include "core/logger.hpp"

namespace trader {

RateLimiter::RateLimiter(const RateLimitConfig& config)
    : config_(config) {}

bool RateLimiter::allow_request(const std::string& script_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cooldown first
    if (in_cooldown_ && !check_cooldown_expired()) {
        total_rejected_++;
        return false;
    }

    // Check global rate limits
    if (!check_global_limits()) {
        enter_cooldown();
        total_rejected_++;
        auto logger = get_logger("rate_limiter");
        logger->warn("Global rate limit hit - entering cooldown for {} seconds",
                     config_.cooldown_seconds);
        return false;
    }

    // Check per-script rate limit
    if (!check_script_limit(script_name)) {
        total_rejected_++;
        auto logger = get_logger("rate_limiter");
        logger->debug("Per-script rate limit hit for '{}'", script_name);
        return false;
    }

    // Record the request
    auto now = std::chrono::steady_clock::now();
    global_second_window_.push_back(now);
    global_minute_window_.push_back(now);
    script_windows_[script_name].push_back(now);

    total_allowed_++;
    return true;
}

bool RateLimiter::allow_global_request() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (in_cooldown_ && !check_cooldown_expired()) {
        total_rejected_++;
        return false;
    }

    if (!check_global_limits()) {
        enter_cooldown();
        total_rejected_++;
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    global_second_window_.push_back(now);
    global_minute_window_.push_back(now);

    total_allowed_++;
    return true;
}

int RateLimiter::cooldown_remaining() const {
    if (!in_cooldown_) return 0;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - cooldown_start_).count();
    int remaining = config_.cooldown_seconds - static_cast<int>(elapsed);
    return remaining > 0 ? remaining : 0;
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    global_second_window_.clear();
    global_minute_window_.clear();
    script_windows_.clear();
    in_cooldown_ = false;
    total_allowed_ = 0;
    total_rejected_ = 0;
}

int RateLimiter::global_calls_last_minute() const {
    // Note: this is approximate since we don't hold the lock
    // For monitoring purposes only
    return static_cast<int>(global_minute_window_.size());
}

// ==================== Private Methods ====================

bool RateLimiter::check_global_limits() {
    auto now = std::chrono::steady_clock::now();

    // Clean up expired entries from per-second window
    auto one_second_ago = now - std::chrono::seconds(1);
    while (!global_second_window_.empty() && global_second_window_.front() < one_second_ago) {
        global_second_window_.pop_front();
    }

    // Clean up expired entries from per-minute window
    auto one_minute_ago = now - std::chrono::minutes(1);
    while (!global_minute_window_.empty() && global_minute_window_.front() < one_minute_ago) {
        global_minute_window_.pop_front();
    }

    // Check per-second limit
    if (static_cast<int>(global_second_window_.size()) >= config_.max_calls_per_second) {
        return false;
    }

    // Check per-minute limit
    if (static_cast<int>(global_minute_window_.size()) >= config_.max_calls_per_minute) {
        return false;
    }

    return true;
}

bool RateLimiter::check_script_limit(const std::string& script_name) {
    auto now = std::chrono::steady_clock::now();
    auto one_minute_ago = now - std::chrono::minutes(1);

    auto& window = script_windows_[script_name];

    // Clean up expired entries
    while (!window.empty() && window.front() < one_minute_ago) {
        window.pop_front();
    }

    // Check per-script per-minute limit
    if (static_cast<int>(window.size()) >= config_.per_script_calls_per_minute) {
        return false;
    }

    return true;
}

void RateLimiter::enter_cooldown() {
    in_cooldown_ = true;
    cooldown_start_ = std::chrono::steady_clock::now();
}

bool RateLimiter::check_cooldown_expired() {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - cooldown_start_).count();

    if (elapsed >= config_.cooldown_seconds) {
        in_cooldown_ = false;
        // Clear windows after cooldown
        global_second_window_.clear();
        global_minute_window_.clear();
        return true;
    }
    return false;
}

}  // namespace trader

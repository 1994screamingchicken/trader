#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/null_mutex.h>

namespace trader {

/// A thread-safe ring-buffer sink for spdlog that stores the last N log messages.
/// Used by the TUI to display recent log output in a scrollable panel.
template<typename Mutex>
class RingBufferSinkT : public spdlog::sinks::base_sink<Mutex> {
public:
    explicit RingBufferSinkT(size_t max_messages = 100)
        : max_messages_(max_messages) {}

    /// Get a copy of all stored messages (thread-safe)
    std::vector<std::string> get_messages() const {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        return {messages_.begin(), messages_.end()};
    }

    /// Clear all stored messages
    void clear() {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        messages_.clear();
    }

    /// Get current message count
    size_t size() const {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        return messages_.size();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        std::string message = fmt::to_string(formatted);

        std::lock_guard<std::mutex> lock(buffer_mutex_);
        messages_.push_back(std::move(message));
        while (messages_.size() > max_messages_) {
            messages_.pop_front();
        }
    }

    void flush_() override {}

private:
    size_t max_messages_;
    mutable std::mutex buffer_mutex_;
    std::deque<std::string> messages_;
};

/// Convenience type aliases
using RingBufferSink_mt = RingBufferSinkT<std::mutex>;
using RingBufferSink_st = RingBufferSinkT<spdlog::details::null_mutex>;

}  // namespace trader

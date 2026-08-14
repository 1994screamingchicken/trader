#pragma once

#include <memory>
#include <atomic>
#include <chrono>
#include <string>

#include "core/config.hpp"
#include "tui/log_sink.hpp"

namespace trader {

// Forward declarations
class ScriptManager;
class RateLimiter;
class RiskManager;
class KrakenRestClient;
class KrakenWebSocketClient;

/// Full-screen terminal user interface for the trading platform.
/// Replaces the text-based main loop with a rich, real-time dashboard.
class TuiApp {
public:
    TuiApp(ScriptManager& script_manager,
           std::shared_ptr<RateLimiter> rate_limiter,
           std::shared_ptr<RiskManager> risk_manager,
           std::shared_ptr<KrakenRestClient> rest_client,
           std::shared_ptr<KrakenWebSocketClient> ws_client,
           const AppConfig& config,
           std::shared_ptr<RingBufferSink_mt> log_sink);

    ~TuiApp();

    /// Run the TUI (blocking). This replaces the main event loop.
    /// Handles WebSocket polling, script ticking, and UI refresh internally.
    void run();

    /// Signal the TUI to stop gracefully
    void stop();

    /// Check if the TUI is still running
    bool is_running() const { return running_; }

private:
    ScriptManager& script_manager_;
    std::shared_ptr<RateLimiter> rate_limiter_;
    std::shared_ptr<RiskManager> risk_manager_;
    std::shared_ptr<KrakenRestClient> rest_client_;
    std::shared_ptr<KrakenWebSocketClient> ws_client_;
    AppConfig config_;
    std::shared_ptr<RingBufferSink_mt> log_sink_;

    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::chrono::steady_clock::time_point start_time_;

    /// Format uptime duration as HH:MM:SS
    std::string format_uptime() const;

    /// Get WsState as a display string
    std::string ws_state_string() const;
};

}  // namespace trader

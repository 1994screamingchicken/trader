#pragma once

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <nlohmann/json.hpp>
#include "core/config.hpp"

namespace trader {

using json = nlohmann::json;

/// WebSocket connection state
enum class WsState {
    Disconnected,
    Connecting,
    Connected,
    Closing,
    Error
};

/// Callback types for WebSocket events
using WsMessageCallback = std::function<void(const json&)>;
using WsErrorCallback = std::function<void(const std::string&)>;
using WsStateCallback = std::function<void(WsState)>;

/// Subscription request for Kraken WebSocket
struct WsSubscription {
    std::string channel;            // "ticker", "ohlc", "trade", "book", "spread"
    std::vector<std::string> pairs; // e.g., ["XBT/USD", "ETH/USD"]
    int depth = 10;                 // for book channel
    int interval = 1;              // for ohlc channel (minutes)
};

/// Kraken WebSocket client for real-time market data
class KrakenWebSocketClient {
public:
    KrakenWebSocketClient(const KrakenConfig& config);
    ~KrakenWebSocketClient();

    // --- Connection management ---

    /// Connect to the public WebSocket endpoint
    void connect();

    /// Connect to the authenticated WebSocket endpoint
    void connect_authenticated(const std::string& ws_token);

    /// Disconnect gracefully
    void disconnect();

    /// Check if connected
    bool is_connected() const { return state_ == WsState::Connected; }

    /// Get current connection state
    WsState state() const { return state_; }

    // --- Subscriptions ---

    /// Subscribe to a market data channel
    void subscribe(const WsSubscription& subscription);

    /// Unsubscribe from a channel
    void unsubscribe(const WsSubscription& subscription);

    /// Subscribe to private channels (requires auth token)
    void subscribe_private(const std::string& channel, const std::string& token);

    // --- Callbacks ---

    /// Set callback for received messages
    void on_message(WsMessageCallback callback) { message_callback_ = std::move(callback); }

    /// Set callback for errors
    void on_error(WsErrorCallback callback) { error_callback_ = std::move(callback); }

    /// Set callback for state changes
    void on_state_change(WsStateCallback callback) { state_callback_ = std::move(callback); }

    // --- Data access ---

    /// Get the latest ticker data for a pair (thread-safe)
    json get_ticker(const std::string& pair) const;

    /// Get the latest order book for a pair (thread-safe)
    json get_order_book(const std::string& pair) const;

    /// Process pending messages (call from main thread if needed)
    void poll();

private:
    /// Internal WebSocket implementation (pimpl pattern)
    class Impl;
    std::unique_ptr<Impl> impl_;

    /// Set connection state and notify callback
    void set_state(WsState new_state);

    /// Handle incoming WebSocket message
    void handle_message(const std::string& raw_message);

    /// Handle subscription status messages
    void handle_subscription_status(const json& msg);

    /// Handle heartbeat messages
    void handle_heartbeat(const json& msg);

    /// Handle channel data updates
    void handle_channel_data(const json& msg);

    /// Reconnection logic
    void attempt_reconnect();

    KrakenConfig config_;
    std::atomic<WsState> state_{WsState::Disconnected};
    WsMessageCallback message_callback_;
    WsErrorCallback error_callback_;
    WsStateCallback state_callback_;

    // Thread-safe data caches
    mutable std::mutex data_mutex_;
    std::unordered_map<std::string, json> ticker_cache_;
    std::unordered_map<std::string, json> book_cache_;

    // Message queue for thread-safe delivery
    mutable std::mutex queue_mutex_;
    std::queue<json> message_queue_;

    // Connection management
    std::string ws_token_;
    bool authenticated_ = false;
    std::atomic<bool> should_reconnect_{true};
    int reconnect_attempts_ = 0;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 10;
    static constexpr int RECONNECT_DELAY_MS = 5000;
};

}  // namespace trader

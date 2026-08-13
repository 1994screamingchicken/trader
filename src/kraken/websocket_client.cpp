#include "kraken/websocket_client.hpp"
#include "core/logger.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>

#include <thread>
#include <chrono>

namespace trader {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

/// Internal WebSocket implementation using Boost.Beast
class KrakenWebSocketClient::Impl {
public:
    Impl()
        : ssl_ctx_(ssl::context::tlsv12_client),
          resolver_(ioc_),
          ws_(ioc_, ssl_ctx_) {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);
    }

    ~Impl() {
        stop();
    }

    void connect(const std::string& url,
                 std::function<void(const std::string&)> on_message,
                 std::function<void()> on_open,
                 std::function<void(const std::string&)> on_close,
                 std::function<void(const std::string&)> on_fail) {

        on_message_ = std::move(on_message);
        on_open_ = std::move(on_open);
        on_close_ = std::move(on_close);
        on_fail_ = std::move(on_fail);

        // Parse the URL (expects wss://host/path or wss://host)
        std::string host;
        std::string path = "/";
        std::string port = "443";

        // Strip scheme
        std::string stripped = url;
        if (stripped.find("wss://") == 0) {
            stripped = stripped.substr(6);
        } else if (stripped.find("ws://") == 0) {
            stripped = stripped.substr(5);
            port = "80";
        }

        // Split host and path
        auto slash_pos = stripped.find('/');
        if (slash_pos != std::string::npos) {
            host = stripped.substr(0, slash_pos);
            path = stripped.substr(slash_pos);
        } else {
            host = stripped;
        }

        // Handle port in host
        auto colon_pos = host.find(':');
        if (colon_pos != std::string::npos) {
            port = host.substr(colon_pos + 1);
            host = host.substr(0, colon_pos);
        }

        host_ = host;

        // Set SNI hostname for TLS
        if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            if (on_fail_) on_fail_(ec.message());
            return;
        }

        // Resolve the host
        resolver_.async_resolve(
            host, port,
            [this, host, path](beast::error_code ec, tcp::resolver::results_type results) {
                if (ec) {
                    if (on_fail_) on_fail_(ec.message());
                    return;
                }
                on_resolve(ec, results, host, path);
            });

        // Run the io_context in a background thread
        ws_thread_ = std::thread([this]() {
            try {
                ioc_.run();
            } catch (const std::exception& e) {
                auto logger = get_logger("ws");
                logger->error("WebSocket event loop exception: {}", e.what());
            }
        });
    }

    void send(const std::string& message) {
        net::post(ioc_, [this, message]() {
            bool was_empty = write_queue_.empty();
            write_queue_.push(message);
            if (was_empty && connected_) {
                do_write();
            }
        });
    }

    void close() {
        net::post(ioc_, [this]() {
            if (connected_) {
                beast::error_code ec;
                ws_.close(websocket::close_code::normal, ec);
            }
        });
    }

    void stop() {
        ioc_.stop();
        if (ws_thread_.joinable()) {
            ws_thread_.join();
        }
    }

    bool is_open() const {
        return connected_;
    }

private:
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results,
                    const std::string& host, const std::string& path) {
        if (ec) {
            if (on_fail_) on_fail_(ec.message());
            return;
        }

        // Connect to the endpoint using beast::tcp_stream's async_connect
        beast::get_lowest_layer(ws_).async_connect(
            results,
            [this, host, path](beast::error_code ec, const tcp::endpoint&) {
                if (ec) {
                    if (on_fail_) on_fail_(ec.message());
                    return;
                }
                on_connect(ec, host, path);
            });
    }

    void on_connect(beast::error_code ec, const std::string& host, const std::string& path) {
        if (ec) {
            if (on_fail_) on_fail_(ec.message());
            return;
        }

        // Perform the SSL handshake
        ws_.next_layer().async_handshake(
            ssl::stream_base::client,
            [this, host, path](beast::error_code ec) {
                if (ec) {
                    if (on_fail_) on_fail_(ec.message());
                    return;
                }
                on_ssl_handshake(ec, host, path);
            });
    }

    void on_ssl_handshake(beast::error_code ec, const std::string& host, const std::string& path) {
        if (ec) {
            if (on_fail_) on_fail_(ec.message());
            return;
        }

        // Set the User-Agent
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent,
                        "KrakenLuaTrader/1.0");
            }));

        // Perform the WebSocket handshake
        ws_.async_handshake(host, path,
            [this](beast::error_code ec) {
                if (ec) {
                    if (on_fail_) on_fail_(ec.message());
                    return;
                }
                on_handshake(ec);
            });
    }

    void on_handshake(beast::error_code ec) {
        if (ec) {
            if (on_fail_) on_fail_(ec.message());
            return;
        }

        connected_ = true;
        if (on_open_) on_open_();

        // Start reading messages
        do_read();
    }

    void do_read() {
        ws_.async_read(buffer_,
            [this](beast::error_code ec, std::size_t /*bytes_transferred*/) {
                on_read(ec);
            });
    }

    void on_read(beast::error_code ec) {
        if (ec) {
            connected_ = false;
            if (ec == websocket::error::closed) {
                if (on_close_) on_close_("Connection closed");
            } else {
                if (on_close_) on_close_(ec.message());
            }
            return;
        }

        // Deliver message
        std::string msg = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        if (on_message_) on_message_(msg);

        // Continue reading
        do_read();
    }

    void do_write() {
        if (write_queue_.empty()) return;

        ws_.async_write(
            net::buffer(write_queue_.front()),
            [this](beast::error_code ec, std::size_t /*bytes_transferred*/) {
                if (ec) {
                    auto logger = get_logger("ws");
                    logger->error("WebSocket send error: {}", ec.message());
                    return;
                }
                write_queue_.pop();
                if (!write_queue_.empty()) {
                    do_write();
                }
            });
    }

    net::io_context ioc_;
    ssl::context ssl_ctx_;
    tcp::resolver resolver_;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    beast::flat_buffer buffer_;

    std::string host_;
    std::atomic<bool> connected_{false};
    std::thread ws_thread_;
    std::queue<std::string> write_queue_;

    std::function<void(const std::string&)> on_message_;
    std::function<void()> on_open_;
    std::function<void(const std::string&)> on_close_;
    std::function<void(const std::string&)> on_fail_;
};

// ==================== KrakenWebSocketClient ====================

KrakenWebSocketClient::KrakenWebSocketClient(const KrakenConfig& config)
    : config_(config), impl_(std::make_unique<Impl>()) {}

KrakenWebSocketClient::~KrakenWebSocketClient() {
    disconnect();
}

void KrakenWebSocketClient::connect() {
    auto logger = get_logger("ws");
    logger->info("Connecting to Kraken WebSocket: {}", config_.ws_url);

    set_state(WsState::Connecting);

    impl_->connect(
        config_.ws_url,
        // on_message
        [this](const std::string& msg) { handle_message(msg); },
        // on_open
        [this]() {
            auto logger = get_logger("ws");
            logger->info("WebSocket connected (public)");
            reconnect_attempts_ = 0;
            set_state(WsState::Connected);
        },
        // on_close
        [this](const std::string& reason) {
            auto logger = get_logger("ws");
            logger->warn("WebSocket closed: {}", reason);
            set_state(WsState::Disconnected);
            if (should_reconnect_) {
                attempt_reconnect();
            }
        },
        // on_fail
        [this](const std::string& reason) {
            auto logger = get_logger("ws");
            logger->error("WebSocket connection failed: {}", reason);
            set_state(WsState::Error);
            if (should_reconnect_) {
                attempt_reconnect();
            }
        }
    );
}

void KrakenWebSocketClient::connect_authenticated(const std::string& ws_token) {
    auto logger = get_logger("ws");
    logger->info("Connecting to Kraken authenticated WebSocket: {}", config_.ws_auth_url);

    ws_token_ = ws_token;
    authenticated_ = true;
    set_state(WsState::Connecting);

    impl_->connect(
        config_.ws_auth_url,
        // on_message
        [this](const std::string& msg) { handle_message(msg); },
        // on_open
        [this]() {
            auto logger = get_logger("ws");
            logger->info("WebSocket connected (authenticated)");
            reconnect_attempts_ = 0;
            set_state(WsState::Connected);
        },
        // on_close
        [this](const std::string& reason) {
            auto logger = get_logger("ws");
            logger->warn("Authenticated WebSocket closed: {}", reason);
            set_state(WsState::Disconnected);
            if (should_reconnect_) {
                attempt_reconnect();
            }
        },
        // on_fail
        [this](const std::string& reason) {
            auto logger = get_logger("ws");
            logger->error("Authenticated WebSocket connection failed: {}", reason);
            set_state(WsState::Error);
            if (should_reconnect_) {
                attempt_reconnect();
            }
        }
    );
}

void KrakenWebSocketClient::disconnect() {
    should_reconnect_ = false;
    set_state(WsState::Closing);
    impl_->close();
    impl_->stop();
    set_state(WsState::Disconnected);
}

void KrakenWebSocketClient::subscribe(const WsSubscription& subscription) {
    json msg;
    msg["event"] = "subscribe";
    msg["pair"] = subscription.pairs;
    msg["subscription"]["name"] = subscription.channel;

    if (subscription.channel == "book") {
        msg["subscription"]["depth"] = subscription.depth;
    } else if (subscription.channel == "ohlc") {
        msg["subscription"]["interval"] = subscription.interval;
    }

    auto logger = get_logger("ws");
    logger->info("Subscribing to {} for {} pair(s)", subscription.channel, subscription.pairs.size());

    impl_->send(msg.dump());
}

void KrakenWebSocketClient::unsubscribe(const WsSubscription& subscription) {
    json msg;
    msg["event"] = "unsubscribe";
    msg["pair"] = subscription.pairs;
    msg["subscription"]["name"] = subscription.channel;

    impl_->send(msg.dump());
}

void KrakenWebSocketClient::subscribe_private(const std::string& channel, const std::string& token) {
    json msg;
    msg["event"] = "subscribe";
    msg["subscription"]["name"] = channel;
    msg["subscription"]["token"] = token;

    auto logger = get_logger("ws");
    logger->info("Subscribing to private channel: {}", channel);

    impl_->send(msg.dump());
}

json KrakenWebSocketClient::get_ticker(const std::string& pair) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = ticker_cache_.find(pair);
    if (it != ticker_cache_.end()) {
        return it->second;
    }
    return json::object();
}

json KrakenWebSocketClient::get_order_book(const std::string& pair) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = book_cache_.find(pair);
    if (it != book_cache_.end()) {
        return it->second;
    }
    return json::object();
}

void KrakenWebSocketClient::poll() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!message_queue_.empty()) {
        auto msg = std::move(message_queue_.front());
        message_queue_.pop();
        if (message_callback_) {
            message_callback_(msg);
        }
    }
}

// ==================== Internal Methods ====================

void KrakenWebSocketClient::set_state(WsState new_state) {
    state_ = new_state;
    if (state_callback_) {
        state_callback_(new_state);
    }
}

void KrakenWebSocketClient::handle_message(const std::string& raw_message) {
    auto logger = get_logger("ws");

    try {
        json msg = json::parse(raw_message);

        // Kraken sends different message types:
        // 1. Events: {"event": "..."} - system messages
        // 2. Data: [...] - array format for channel data

        if (msg.is_object()) {
            // Event message
            std::string event = msg.value("event", "");

            if (event == "heartbeat") {
                handle_heartbeat(msg);
            } else if (event == "subscriptionStatus") {
                handle_subscription_status(msg);
            } else if (event == "systemStatus") {
                logger->info("Kraken system status: {}", msg.value("status", "unknown"));
            } else if (event == "pong") {
                // Pong response, ignore
            } else {
                logger->debug("Unknown event: {}", event);
            }
        } else if (msg.is_array()) {
            // Channel data update
            handle_channel_data(msg);
        }

        // Queue message for main-thread processing
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push(std::move(msg));
        }

    } catch (const json::exception& e) {
        logger->error("Failed to parse WebSocket message: {}", e.what());
        if (error_callback_) {
            error_callback_(std::string("Parse error: ") + e.what());
        }
    }
}

void KrakenWebSocketClient::handle_subscription_status(const json& msg) {
    auto logger = get_logger("ws");
    std::string status = msg.value("status", "");
    std::string channel = msg.value("channelName", "unknown");
    std::string pair = msg.value("pair", "");

    if (status == "subscribed") {
        logger->info("Subscribed to {} {}", channel, pair);
    } else if (status == "unsubscribed") {
        logger->info("Unsubscribed from {} {}", channel, pair);
    } else if (status == "error") {
        std::string error = msg.value("errorMessage", "Unknown error");
        logger->error("Subscription error for {} {}: {}", channel, pair, error);
        if (error_callback_) {
            error_callback_("Subscription error: " + error);
        }
    }
}

void KrakenWebSocketClient::handle_heartbeat(const json& /*msg*/) {
    // Heartbeat received - connection is alive
    auto logger = get_logger("ws");
    logger->trace("Heartbeat received");
}

void KrakenWebSocketClient::handle_channel_data(const json& msg) {
    // Kraken channel data format: [channelID, data, channelName, pair]
    // or for private: [data, channelName]
    if (msg.size() < 3) return;

    auto logger = get_logger("ws");

    try {
        std::string channel_name;
        std::string pair;

        if (msg.back().is_string()) {
            pair = msg.back().get<std::string>();
            if (msg.size() >= 4 && msg[msg.size() - 2].is_string()) {
                channel_name = msg[msg.size() - 2].get<std::string>();
            }
        }

        // Update caches based on channel type
        if (channel_name.find("ticker") != std::string::npos) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            ticker_cache_[pair] = msg[1];
        } else if (channel_name.find("book") != std::string::npos) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            book_cache_[pair] = msg[1];
        }

        logger->trace("Channel data: {} {}", channel_name, pair);
    } catch (const std::exception& e) {
        logger->debug("Error processing channel data: {}", e.what());
    }
}

void KrakenWebSocketClient::attempt_reconnect() {
    if (reconnect_attempts_ >= MAX_RECONNECT_ATTEMPTS) {
        auto logger = get_logger("ws");
        logger->error("Max reconnection attempts ({}) reached, giving up", MAX_RECONNECT_ATTEMPTS);
        set_state(WsState::Error);
        return;
    }

    reconnect_attempts_++;
    auto logger = get_logger("ws");
    int delay = RECONNECT_DELAY_MS * reconnect_attempts_;
    logger->info("Attempting reconnection {}/{} in {}ms",
                 reconnect_attempts_, MAX_RECONNECT_ATTEMPTS, delay);

    // Schedule reconnection (simplified - in production use a proper timer)
    std::thread([this, delay]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (should_reconnect_) {
            impl_ = std::make_unique<Impl>();
            if (authenticated_ && !ws_token_.empty()) {
                connect_authenticated(ws_token_);
            } else {
                connect();
            }
        }
    }).detach();
}

}  // namespace trader

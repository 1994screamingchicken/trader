#include "kraken/websocket_client.hpp"
#include "core/logger.hpp"

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <thread>
#include <chrono>

namespace trader {

using WsClient = websocketpp::client<websocketpp::config::asio_tls_client>;
using WsConnectionPtr = websocketpp::connection_hdl;
using SslContext = websocketpp::lib::shared_ptr<boost::asio::ssl::context>;

/// Internal WebSocket implementation using websocketpp
class KrakenWebSocketClient::Impl {
public:
    Impl() {
        client_.clear_access_channels(websocketpp::log::alevel::all);
        client_.clear_error_channels(websocketpp::log::elevel::all);

        client_.init_asio();

        client_.set_tls_init_handler([](WsConnectionPtr) -> SslContext {
            auto ctx = std::make_shared<boost::asio::ssl::context>(
                boost::asio::ssl::context::tlsv12_client);
            ctx->set_default_verify_paths();
            ctx->set_verify_mode(boost::asio::ssl::verify_peer);
            return ctx;
        });
    }

    ~Impl() {
        stop();
    }

    void connect(const std::string& url,
                 std::function<void(const std::string&)> on_message,
                 std::function<void()> on_open,
                 std::function<void(const std::string&)> on_close,
                 std::function<void(const std::string&)> on_fail) {

        websocketpp::lib::error_code ec;
        auto con = client_.get_connection(url, ec);
        if (ec) {
            if (on_fail) on_fail(ec.message());
            return;
        }

        con->set_message_handler(
            [on_message](WsConnectionPtr, WsClient::message_ptr msg) {
                if (on_message) on_message(msg->get_payload());
            });

        con->set_open_handler([on_open](WsConnectionPtr) {
            if (on_open) on_open();
        });

        con->set_close_handler([on_close](WsConnectionPtr) {
            if (on_close) on_close("Connection closed");
        });

        con->set_fail_handler([on_fail, &client = client_](WsConnectionPtr hdl) {
            auto con = client.get_con_from_hdl(hdl);
            std::string reason = con->get_ec().message();
            if (on_fail) on_fail(reason);
        });

        connection_ = con->get_handle();
        client_.connect(con);

        // Run the ASIO event loop in a background thread
        ws_thread_ = std::thread([this]() {
            try {
                client_.run();
            } catch (const std::exception& e) {
                auto logger = get_logger("ws");
                logger->error("WebSocket event loop exception: {}", e.what());
            }
        });
    }

    void send(const std::string& message) {
        websocketpp::lib::error_code ec;
        client_.send(connection_, message, websocketpp::frame::opcode::text, ec);
        if (ec) {
            auto logger = get_logger("ws");
            logger->error("WebSocket send error: {}", ec.message());
        }
    }

    void close() {
        websocketpp::lib::error_code ec;
        client_.close(connection_, websocketpp::close::status::normal, "Client disconnect", ec);
    }

    void stop() {
        if (!client_.stopped()) {
            client_.stop();
        }
        if (ws_thread_.joinable()) {
            ws_thread_.join();
        }
    }

    bool is_open() const {
        try {
            auto con = client_.get_con_from_hdl(connection_);
            return con && con->get_state() == websocketpp::session::state::open;
        } catch (...) {
            return false;
        }
    }

private:
    mutable WsClient client_;
    WsConnectionPtr connection_;
    std::thread ws_thread_;
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

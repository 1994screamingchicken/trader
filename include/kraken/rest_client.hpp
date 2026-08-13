#pragma once

#include <string>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include "core/config.hpp"
#include "kraken/auth.hpp"

namespace trader {

using json = nlohmann::json;

/// Result of an API call - either success with JSON data or an error
struct ApiResult {
    bool success = false;
    json data;
    std::string error;

    static ApiResult ok(json data) {
        return {true, std::move(data), ""};
    }

    static ApiResult fail(std::string error) {
        return {false, {}, std::move(error)};
    }
};

/// Order types supported by Kraken
enum class OrderType {
    Market,
    Limit,
    StopLoss,
    TakeProfit,
    StopLossLimit,
    TakeProfitLimit
};

/// Order side
enum class OrderSide {
    Buy,
    Sell
};

/// Convert OrderType to Kraken API string
std::string order_type_to_string(OrderType type);

/// Convert OrderSide to Kraken API string
std::string order_side_to_string(OrderSide side);

/// Order request parameters
struct OrderRequest {
    std::string pair;              // e.g., "XBTUSD"
    OrderSide side;                // buy or sell
    OrderType type;                // market, limit, etc.
    double volume;                 // order quantity
    double price = 0.0;            // limit price (0 for market orders)
    double price2 = 0.0;          // secondary price (stop-loss limit, take-profit limit)
    std::string leverage;          // leverage, e.g. "2:1" (empty for no leverage)
    bool validate_only = false;    // if true, validate but don't submit
    std::string close_type;        // optional close order type
    double close_price = 0.0;     // optional close price
};

/// Callback type for rate-limit-aware request gating
using RateLimitCallback = std::function<bool()>;

/// Kraken REST API client
class KrakenRestClient {
public:
    KrakenRestClient(const KrakenConfig& config, bool paper_trading = true);
    virtual ~KrakenRestClient();

    // --- Public API (no authentication required) ---

    /// Get server time
    virtual ApiResult get_server_time();

    /// Get tradeable asset pairs info
    virtual ApiResult get_asset_pairs(const std::string& pair = "");

    /// Get ticker information for a pair
    virtual ApiResult get_ticker(const std::string& pair);

    /// Get OHLC (candle) data
    virtual ApiResult get_ohlc(const std::string& pair, int interval = 1, int64_t since = 0);

    /// Get order book depth
    virtual ApiResult get_order_book(const std::string& pair, int count = 25);

    /// Get recent trades
    virtual ApiResult get_recent_trades(const std::string& pair, int64_t since = 0);

    // --- Private API (authentication required) ---

    /// Get account balance
    virtual ApiResult get_balance();

    /// Get trade balance (equity, margin, etc.)
    virtual ApiResult get_trade_balance(const std::string& asset = "ZUSD");

    /// Get open orders
    virtual ApiResult get_open_orders();

    /// Get closed orders
    virtual ApiResult get_closed_orders(int64_t start = 0, int64_t end = 0);

    /// Query specific orders by transaction ID
    virtual ApiResult query_orders(const std::string& txid);

    /// Place a new order
    virtual ApiResult place_order(const OrderRequest& order);

    /// Cancel an order
    virtual ApiResult cancel_order(const std::string& txid);

    /// Cancel all open orders
    virtual ApiResult cancel_all_orders();

    /// Get open positions
    virtual ApiResult get_open_positions();

    /// Get trade history
    virtual ApiResult get_trades_history(int64_t start = 0, int64_t end = 0);

    /// Get WebSocket authentication token
    virtual ApiResult get_ws_token();

    // --- Configuration ---

    /// Set a rate limit check callback (called before each request)
    void set_rate_limit_callback(RateLimitCallback callback);

    /// Enable/disable paper trading mode
    void set_paper_trading(bool enabled) { paper_trading_ = enabled; }

    /// Check if in paper trading mode
    bool is_paper_trading() const { return paper_trading_; }

private:
    /// Perform a public API GET/POST request
    ApiResult public_request(const std::string& endpoint, const std::string& params = "");

    /// Perform a private (authenticated) API POST request
    ApiResult private_request(const std::string& endpoint, const std::string& extra_params = "");

    /// Low-level HTTP POST
    std::string http_post(const std::string& url, const std::string& post_data,
                          const std::vector<std::string>& headers = {});

    /// Parse Kraken API response and extract result or error
    ApiResult parse_response(const std::string& response_body);

    /// Check rate limit before making a request
    bool check_rate_limit();

    KrakenConfig config_;
    std::unique_ptr<KrakenAuth> auth_;
    RateLimitCallback rate_limit_callback_;
    bool paper_trading_;
};

}  // namespace trader

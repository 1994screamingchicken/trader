#pragma once

#include <string>
#include <memory>
#include <cstdint>

#include "kraken/rest_client.hpp"
#include "backtest/data_feed.hpp"
#include "backtest/backtest_broker.hpp"

namespace trader {

/// A REST client that serves historical data from a DataFeed instead of making HTTP calls.
/// Inherits from KrakenRestClient so it can be passed to LuaEngine unchanged.
class BacktestRestClient : public KrakenRestClient {
public:
    BacktestRestClient(DataFeed& feed, BacktestBroker& broker,
                       const std::string& pair);
    ~BacktestRestClient() override = default;

    /// Set the current candle index (called by BacktestEngine before each tick)
    void set_current_index(size_t index) { current_index_ = index; }

    /// Get the current candle index
    size_t current_index() const { return current_index_; }

    // --- Overridden public API methods ---
    ApiResult get_server_time() override;
    ApiResult get_asset_pairs(const std::string& pair = "") override;
    ApiResult get_ticker(const std::string& pair) override;
    ApiResult get_ohlc(const std::string& pair, int interval = 1, int64_t since = 0) override;
    ApiResult get_order_book(const std::string& pair, int count = 25) override;
    ApiResult get_recent_trades(const std::string& pair, int64_t since = 0) override;

    // --- Overridden private API methods ---
    ApiResult get_balance() override;
    ApiResult get_trade_balance(const std::string& asset = "ZUSD") override;
    ApiResult get_open_orders() override;
    ApiResult get_closed_orders(int64_t start = 0, int64_t end = 0) override;
    ApiResult query_orders(const std::string& txid) override;
    ApiResult place_order(const OrderRequest& order) override;
    ApiResult cancel_order(const std::string& txid) override;
    ApiResult cancel_all_orders() override;
    ApiResult get_open_positions() override;
    ApiResult get_trades_history(int64_t start = 0, int64_t end = 0) override;
    ApiResult get_ws_token() override;

private:
    /// Convert a pair name (e.g., "XBT/USD") to Kraken's internal format (e.g., "XXBTZUSD")
    std::string to_kraken_pair(const std::string& pair) const;

    DataFeed& feed_;
    BacktestBroker& broker_;
    std::string pair_;          // The primary trading pair for this backtest
    size_t current_index_ = 0;
};

}  // namespace trader

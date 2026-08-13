#include "backtest/backtest_rest_client.hpp"

#include <sstream>
#include <algorithm>
#include <chrono>

namespace trader {

namespace {
    // Create a default KrakenConfig for the base class (won't be used for HTTP)
    KrakenConfig make_dummy_config() {
        KrakenConfig cfg;
        cfg.api_key = "BACKTEST";
        cfg.api_secret = "BACKTEST";
        cfg.rest_url = "http://localhost";
        return cfg;
    }
}

BacktestRestClient::BacktestRestClient(DataFeed& feed, BacktestBroker& broker,
                                       const std::string& pair)
    : KrakenRestClient(make_dummy_config(), true),
      feed_(feed),
      broker_(broker),
      pair_(pair) {
}

std::string BacktestRestClient::to_kraken_pair(const std::string& pair) const {
    // Convert "XBT/USD" -> "XXBTZUSD", "ETH/USD" -> "XETHZUSD"
    // Or if it already looks like a Kraken pair, use as-is
    std::string result = pair;
    // Remove slash
    result.erase(std::remove(result.begin(), result.end(), '/'), result.end());

    // Prefix with X/Z convention for major pairs
    if (result.size() <= 6) {
        // Simple heuristic: base gets X prefix, quote gets Z prefix
        std::string base = result.substr(0, 3);
        std::string quote = result.substr(3);
        if (base.size() == 3 && quote.size() == 3) {
            return "X" + base + "Z" + quote;
        }
    }
    return result;
}

ApiResult BacktestRestClient::get_server_time() {
    json data;
    if (current_index_ < feed_.size()) {
        data["unixtime"] = feed_.at(current_index_).timestamp;
    } else {
        data["unixtime"] = 0;
    }
    data["rfc1123"] = "backtest";
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_asset_pairs(const std::string& /*pair*/) {
    std::string kraken_pair = to_kraken_pair(pair_);
    json data;
    data[kraken_pair] = {
        {"altname", pair_},
        {"wsname", pair_},
        {"base", "X" + pair_.substr(0, 3)},
        {"quote", "Z" + pair_.substr(pair_.size() - 3)},
        {"pair_decimals", 1},
        {"lot_decimals", 8}
    };
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_ticker(const std::string& pair) {
    if (current_index_ >= feed_.size()) {
        return ApiResult::fail("No data available");
    }

    const auto& candle = feed_.at(current_index_);
    std::string kraken_pair = to_kraken_pair(pair);

    std::string close_str = std::to_string(candle.close);
    std::string open_str = std::to_string(candle.open);
    std::string high_str = std::to_string(candle.high);
    std::string low_str = std::to_string(candle.low);
    std::string vol_str = std::to_string(candle.volume);

    // Match exact Kraken ticker format
    json ticker;
    ticker["a"] = json::array({close_str, "1", "1.000"});      // ask [price, whole lot volume, lot volume]
    ticker["b"] = json::array({close_str, "1", "1.000"});      // bid
    ticker["c"] = json::array({close_str, "0.001"});            // last trade closed [price, lot volume]
    ticker["v"] = json::array({vol_str, vol_str});              // volume [today, last 24h]
    ticker["p"] = json::array({close_str, close_str});          // vwap [today, last 24h]
    ticker["t"] = json::array({100, 1000});                     // number of trades
    ticker["l"] = json::array({low_str, low_str});              // low [today, last 24h]
    ticker["h"] = json::array({high_str, high_str});            // high [today, last 24h]
    ticker["o"] = open_str;                                     // today's opening price

    json data;
    data[kraken_pair] = ticker;
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_ohlc(const std::string& pair, int /*interval*/, int64_t /*since*/) {
    std::string kraken_pair = to_kraken_pair(pair);

    auto candles = feed_.candles_up_to(current_index_);

    json ohlc_array = json::array();
    for (const auto& c : candles) {
        // Kraken OHLC format: [time, open, high, low, close, vwap, volume, count]
        json row = json::array({
            c.timestamp,
            std::to_string(c.open),
            std::to_string(c.high),
            std::to_string(c.low),
            std::to_string(c.close),
            std::to_string(c.close),  // vwap approximated as close
            std::to_string(c.volume),
            100  // trade count placeholder
        });
        ohlc_array.push_back(row);
    }

    json data;
    data[kraken_pair] = ohlc_array;
    data["last"] = candles.empty() ? 0 : candles.back().timestamp;
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_order_book(const std::string& pair, int /*count*/) {
    if (current_index_ >= feed_.size()) {
        return ApiResult::fail("No data available");
    }

    const auto& candle = feed_.at(current_index_);
    std::string kraken_pair = to_kraken_pair(pair);
    std::string price_str = std::to_string(candle.close);

    json data;
    data[kraken_pair]["asks"] = json::array({{price_str, "1.0", candle.timestamp}});
    data[kraken_pair]["bids"] = json::array({{price_str, "1.0", candle.timestamp}});
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_recent_trades(const std::string& pair, int64_t /*since*/) {
    std::string kraken_pair = to_kraken_pair(pair);
    json data;
    data[kraken_pair] = json::array();

    // Return last few candles as "trades"
    size_t start = current_index_ > 10 ? current_index_ - 10 : 0;
    for (size_t i = start; i <= current_index_ && i < feed_.size(); i++) {
        const auto& c = feed_.at(i);
        // [price, volume, time, buy/sell, market/limit, misc]
        json trade = json::array({
            std::to_string(c.close), std::to_string(c.volume),
            static_cast<double>(c.timestamp), "b", "m", ""
        });
        data[kraken_pair].push_back(trade);
    }
    data["last"] = std::to_string(feed_.at(current_index_).timestamp);
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_balance() {
    json data;
    // Return cash balance in USD equivalent
    data["ZUSD"] = std::to_string(broker_.cash());

    // Add position balances
    for (const auto& [pair, qty] : broker_.positions()) {
        if (std::abs(qty) > 1e-10) {
            // Use first 4 chars or "X" prefix
            std::string asset_key = "X" + pair.substr(0, 3);
            data[asset_key] = std::to_string(qty);
        }
    }
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_trade_balance(const std::string& /*asset*/) {
    // Compute equity
    std::unordered_map<std::string, double> prices;
    if (current_index_ < feed_.size()) {
        prices[pair_] = feed_.at(current_index_).close;
        // Also store the pair without slash
        std::string no_slash = pair_;
        no_slash.erase(std::remove(no_slash.begin(), no_slash.end(), '/'), no_slash.end());
        prices[no_slash] = feed_.at(current_index_).close;
    }
    double equity = broker_.get_equity(prices);

    json data;
    data["eb"] = std::to_string(equity);     // equivalent balance
    data["tb"] = std::to_string(equity);     // trade balance
    data["m"] = "0.0000";                    // margin
    data["n"] = "0.0000";                    // unrealized net P&L
    data["e"] = std::to_string(equity);      // equity
    data["mf"] = std::to_string(equity);     // free margin
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_open_orders() {
    // No pending orders in simple backtest (market orders fill instantly)
    json data;
    data["open"] = json::object();
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_closed_orders(int64_t /*start*/, int64_t /*end*/) {
    json data;
    data["closed"] = json::object();
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::query_orders(const std::string& /*txid*/) {
    return ApiResult::ok(json::object());
}

ApiResult BacktestRestClient::place_order(const OrderRequest& order) {
    if (current_index_ >= feed_.size()) {
        return ApiResult::fail("No data available");
    }

    const auto& candle = feed_.at(current_index_);
    bool filled = false;

    if (order.type == OrderType::Market) {
        filled = broker_.execute_market_order(
            order.pair, order.side, order.volume,
            candle.close, candle.timestamp);
    } else if (order.type == OrderType::Limit) {
        filled = broker_.execute_limit_order(
            order.pair, order.side, order.volume, order.price,
            candle.high, candle.low, candle.timestamp);
    } else {
        // Treat other order types as market for simplicity
        filled = broker_.execute_market_order(
            order.pair, order.side, order.volume,
            candle.close, candle.timestamp);
    }

    if (!filled) {
        return ApiResult::fail("Insufficient funds or order not filled");
    }

    // Return a simulated successful response in Kraken format
    json data;
    data["descr"]["order"] = order_side_to_string(order.side) + " " +
                             std::to_string(order.volume) + " " + order.pair +
                             " @ " + order_type_to_string(order.type);
    data["txid"] = json::array({"BT-" + std::to_string(candle.timestamp) + "-" +
                                std::to_string(broker_.num_trades())});
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::cancel_order(const std::string& /*txid*/) {
    json data;
    data["count"] = 0;
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::cancel_all_orders() {
    json data;
    data["count"] = 0;
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_open_positions() {
    json data = json::object();
    // Return positions as Kraken-formatted position objects
    for (const auto& [pair, qty] : broker_.positions()) {
        if (std::abs(qty) > 1e-10) {
            std::string pos_id = "BT-POS-" + pair;
            data[pos_id] = {
                {"pair", pair},
                {"type", qty > 0 ? "buy" : "sell"},
                {"vol", std::to_string(std::abs(qty))},
                {"cost", "0"},
                {"fee", "0"},
                {"margin", "0"},
                {"net", "0"}
            };
        }
    }
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_trades_history(int64_t /*start*/, int64_t /*end*/) {
    json data;
    data["trades"] = json::object();
    for (size_t i = 0; i < broker_.trade_history().size(); i++) {
        const auto& trade = broker_.trade_history()[i];
        std::string tid = "BT-TRADE-" + std::to_string(i);
        data["trades"][tid] = {
            {"pair", trade.pair},
            {"type", order_side_to_string(trade.side)},
            {"vol", std::to_string(trade.volume)},
            {"price", std::to_string(trade.price)},
            {"cost", std::to_string(trade.volume * trade.price)},
            {"fee", std::to_string(trade.commission)},
            {"time", trade.timestamp}
        };
    }
    data["count"] = broker_.trade_history().size();
    return ApiResult::ok(data);
}

ApiResult BacktestRestClient::get_ws_token() {
    json data;
    data["token"] = "backtest-token";
    data["expires"] = 900;
    return ApiResult::ok(data);
}

}  // namespace trader

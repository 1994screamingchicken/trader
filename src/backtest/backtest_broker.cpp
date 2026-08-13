#include "backtest/backtest_broker.hpp"

#include <cmath>
#include <algorithm>

namespace trader {

BacktestBroker::BacktestBroker(double initial_balance, double commission_pct)
    : initial_balance_(initial_balance),
      commission_pct_(commission_pct),
      cash_(initial_balance) {
}

bool BacktestBroker::execute_market_order(const std::string& pair, OrderSide side,
                                           double volume, double current_price, int64_t timestamp) {
    double notional = volume * current_price;
    double commission = notional * (commission_pct_ / 100.0);

    double realized_pnl = 0.0;
    double current_pos = position(pair);

    if (side == OrderSide::Buy) {
        // Check if we have enough cash
        double cost = notional + commission;
        if (cost > cash_) {
            return false;  // Insufficient funds
        }

        // If we're short, this is closing (partially or fully)
        if (current_pos < 0) {
            double close_volume = std::min(volume, std::abs(current_pos));
            double entry_price = avg_entry_prices_[pair];
            // Short P&L: (entry - exit) * volume
            realized_pnl = (entry_price - current_price) * close_volume;
        }

        cash_ -= cost;
        positions_[pair] += volume;

        // Update average entry price
        if (current_pos <= 0 && positions_[pair] > 0) {
            // Opened or flipped to a long position
            avg_entry_prices_[pair] = current_price;
        } else if (current_pos > 0) {
            // Adding to existing long
            double total_cost = avg_entry_prices_[pair] * current_pos + current_price * volume;
            avg_entry_prices_[pair] = total_cost / (current_pos + volume);
        }

    } else {  // Sell
        // If we're long, this is closing (partially or fully)
        if (current_pos > 0) {
            double close_volume = std::min(volume, current_pos);
            double entry_price = avg_entry_prices_[pair];
            // Long P&L: (exit - entry) * volume
            realized_pnl = (current_price - entry_price) * close_volume;
        }

        double revenue = notional - commission;
        cash_ += revenue;
        positions_[pair] -= volume;

        // Update average entry price for new short
        if (current_pos >= 0 && positions_[pair] < 0) {
            avg_entry_prices_[pair] = current_price;
        } else if (current_pos < 0) {
            // Adding to existing short
            double total_cost = avg_entry_prices_[pair] * std::abs(current_pos) + current_price * volume;
            avg_entry_prices_[pair] = total_cost / (std::abs(current_pos) + volume);
        }
    }

    // Clean up zero positions
    if (std::abs(positions_[pair]) < 1e-10) {
        positions_.erase(pair);
        avg_entry_prices_.erase(pair);
    }

    // Record trade
    Trade trade;
    trade.timestamp = timestamp;
    trade.pair = pair;
    trade.side = side;
    trade.volume = volume;
    trade.price = current_price;
    trade.commission = commission;
    trade.realized_pnl = realized_pnl;
    trades_.push_back(trade);

    return true;
}

bool BacktestBroker::execute_limit_order(const std::string& pair, OrderSide side,
                                          double volume, double limit_price,
                                          double candle_high, double candle_low, int64_t timestamp) {
    // Check if limit price was reached during the candle
    if (side == OrderSide::Buy) {
        // Buy limit fills if price dipped to or below limit
        if (candle_low <= limit_price) {
            return execute_market_order(pair, side, volume, limit_price, timestamp);
        }
    } else {
        // Sell limit fills if price reached or exceeded limit
        if (candle_high >= limit_price) {
            return execute_market_order(pair, side, volume, limit_price, timestamp);
        }
    }
    return false;
}

double BacktestBroker::position(const std::string& pair) const {
    auto it = positions_.find(pair);
    if (it == positions_.end()) return 0.0;
    return it->second;
}

double BacktestBroker::get_equity(const std::unordered_map<std::string, double>& current_prices) const {
    double equity = cash_;
    for (const auto& [pair, qty] : positions_) {
        auto it = current_prices.find(pair);
        if (it != current_prices.end()) {
            equity += qty * it->second;
        }
    }
    return equity;
}

void BacktestBroker::record_equity(double equity) {
    equity_curve_.push_back(equity);
}

}  // namespace trader

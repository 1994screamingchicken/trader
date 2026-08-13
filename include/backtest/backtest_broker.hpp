#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "kraken/rest_client.hpp"

namespace trader {

/// A single executed trade in the backtest
struct Trade {
    int64_t timestamp;
    std::string pair;
    OrderSide side;
    double volume;
    double price;
    double commission;
    double realized_pnl;  // P&L realized on this trade (for closing trades)
};

/// Simulated exchange for backtesting
class BacktestBroker {
public:
    BacktestBroker(double initial_balance, double commission_pct);

    /// Execute a market order at the given price
    /// @return true if the order was filled successfully
    bool execute_market_order(const std::string& pair, OrderSide side,
                              double volume, double current_price, int64_t timestamp);

    /// Execute a limit order if price crosses during candle
    /// @return true if the order was filled
    bool execute_limit_order(const std::string& pair, OrderSide side,
                             double volume, double limit_price,
                             double candle_high, double candle_low, int64_t timestamp);

    /// Get current cash balance
    double cash() const { return cash_; }

    /// Get position for a specific pair (positive = long, negative = short)
    double position(const std::string& pair) const;

    /// Get all positions
    const std::unordered_map<std::string, double>& positions() const { return positions_; }

    /// Calculate total equity given current prices
    double get_equity(const std::unordered_map<std::string, double>& current_prices) const;

    /// Get trade history
    const std::vector<Trade>& trade_history() const { return trades_; }

    /// Get number of trades
    size_t num_trades() const { return trades_.size(); }

    /// Record equity point for drawdown tracking
    void record_equity(double equity);

    /// Get equity curve
    const std::vector<double>& equity_curve() const { return equity_curve_; }

    /// Get initial balance
    double initial_balance() const { return initial_balance_; }

private:
    double initial_balance_;
    double commission_pct_;  // as percentage (e.g., 0.1 = 0.1%)
    double cash_;
    std::unordered_map<std::string, double> positions_;
    std::unordered_map<std::string, double> avg_entry_prices_;
    std::vector<Trade> trades_;
    std::vector<double> equity_curve_;
};

}  // namespace trader

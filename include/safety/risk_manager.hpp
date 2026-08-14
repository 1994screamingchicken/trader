#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <functional>

#include "core/config.hpp"
#include "kraken/rest_client.hpp"

namespace trader {

/// Tracks P&L and position exposure for risk management
class RiskManager {
public:
    explicit RiskManager(const RiskConfig& config);

    /// Check if an order is allowed by risk parameters.
    /// Returns true if allowed, sets reason string if blocked.
    bool check_order(const OrderRequest& order, std::string& reason);

    /// Record a fill/trade for P&L tracking
    void record_trade(const std::string& script_name, const std::string& pair,
                      double volume, double price, bool is_buy);

    /// Update current positions from exchange data
    void update_positions(const nlohmann::json& positions);

    /// Update current open order count
    void update_open_orders(int count);

    /// Get current total unrealized P&L
    double total_pnl() const;

    /// Get P&L for a specific script
    double script_pnl(const std::string& script_name) const;

    /// Get position size for a specific asset
    double position_size(const std::string& asset) const;

    /// Check if any risk limits have been breached
    bool limits_breached() const;

    /// Get description of breached limits
    std::string breach_description() const;

    /// Reset all P&L tracking (use with caution)
    void reset();

    /// Set callback for emergency stop (called when limits are breached)
    void set_emergency_callback(std::function<void()> callback) {
        emergency_callback_ = std::move(callback);
    }

    /// Get current open order count
    int open_order_count() const { return open_order_count_; }

private:
    /// Check position size limits
    bool check_position_limit(const OrderRequest& order, std::string& reason);

    /// Check loss limits
    bool check_loss_limit(const std::string& script_name, std::string& reason);

    /// Check open order limits
    bool check_order_count_limit(std::string& reason);

    /// Trigger emergency stop if needed
    void check_and_trigger_emergency();

    RiskConfig config_;
    mutable std::mutex mutex_;

    // Position tracking: asset -> net position size
    std::unordered_map<std::string, double> positions_;

    // P&L tracking per script
    std::unordered_map<std::string, double> script_pnl_;

    // Total P&L
    double total_pnl_ = 0.0;

    // Open order count
    int open_order_count_ = 0;

    // Emergency callback
    std::function<void()> emergency_callback_;

    // Whether limits have been breached
    bool limits_breached_ = false;
    std::string breach_reason_;
};

}  // namespace trader

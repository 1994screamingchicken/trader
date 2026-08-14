#include "safety/risk_manager.hpp"
#include "core/logger.hpp"

#include <cmath>
#include <sstream>

namespace trader {

RiskManager::RiskManager(const RiskConfig& config)
    : config_(config) {}

bool RiskManager::check_order(const OrderRequest& order, std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);

    // If limits already breached, block everything
    if (limits_breached_) {
        reason = "Risk limits already breached: " + breach_reason_;
        return false;
    }

    // Check position size limit
    if (config_.enforce_position_limits) {
        if (!check_position_limit(order, reason)) {
            return false;
        }
    }

    // Check loss limits
    if (config_.enforce_loss_limits) {
        // Use empty string for now - in production, script name would be passed through
        if (!check_loss_limit("", reason)) {
            return false;
        }
    }

    // Check open order count limit
    if (!check_order_count_limit(reason)) {
        return false;
    }

    return true;
}

void RiskManager::record_trade(const std::string& script_name, const std::string& pair,
                                double volume, double price, bool is_buy) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto logger = get_logger("risk");

    // Update position
    double signed_volume = is_buy ? volume : -volume;
    positions_[pair] += signed_volume;

    // Simple P&L tracking (mark-to-market would need current prices)
    double trade_value = volume * price;
    double pnl_impact = is_buy ? -trade_value : trade_value;  // Simplified

    script_pnl_[script_name] += pnl_impact;
    total_pnl_ += pnl_impact;

    logger->debug("Trade recorded: {} {} {} @ {} (script: {}, position: {}, total_pnl: {})",
                  is_buy ? "BUY" : "SELL", volume, pair, price,
                  script_name, positions_[pair], total_pnl_);

    // Check if limits are now breached
    check_and_trigger_emergency();
}

void RiskManager::update_positions(const nlohmann::json& positions) {
    std::lock_guard<std::mutex> lock(mutex_);

    positions_.clear();
    for (auto& [key, val] : positions.items()) {
        if (val.is_object() && val.contains("vol")) {
            try {
                double vol = std::stod(val["vol"].get<std::string>());
                positions_[key] = vol;
            } catch (...) {
                // Skip invalid entries
            }
        }
    }
}

void RiskManager::update_open_orders(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    open_order_count_ = count;
}

double RiskManager::total_pnl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_pnl_;
}

double RiskManager::script_pnl(const std::string& script_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = script_pnl_.find(script_name);
    if (it != script_pnl_.end()) {
        return it->second;
    }
    return 0.0;
}

double RiskManager::position_size(const std::string& asset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(asset);
    if (it != positions_.end()) {
        return it->second;
    }
    return 0.0;
}

bool RiskManager::limits_breached() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return limits_breached_;
}

std::string RiskManager::breach_description() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return breach_reason_;
}

void RiskManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_.clear();
    script_pnl_.clear();
    total_pnl_ = 0.0;
    open_order_count_ = 0;
    limits_breached_ = false;
    breach_reason_.clear();
}

// ==================== Private Methods ====================

bool RiskManager::check_position_limit(const OrderRequest& order, std::string& reason) {
    // Calculate what the new position would be
    double current_pos = 0.0;
    auto it = positions_.find(order.pair);
    if (it != positions_.end()) {
        current_pos = it->second;
    }

    double new_pos = current_pos;
    if (order.side == OrderSide::Buy) {
        new_pos += order.volume;
    } else {
        new_pos -= order.volume;
    }

    if (std::abs(new_pos) > config_.max_position_size) {
        std::ostringstream oss;
        oss << "Position limit exceeded for " << order.pair
            << ": current=" << current_pos
            << ", order_vol=" << order.volume
            << ", would_be=" << new_pos
            << ", max=" << config_.max_position_size;
        reason = oss.str();
        return false;
    }

    return true;
}

bool RiskManager::check_loss_limit(const std::string& script_name, std::string& reason) {
    // Check total loss limit
    if (total_pnl_ < -config_.max_total_loss) {
        std::ostringstream oss;
        oss << "Total loss limit breached: pnl=" << total_pnl_
            << ", max_loss=" << config_.max_total_loss;
        reason = oss.str();
        return false;
    }

    // Check per-script loss limit (if script name provided)
    if (!script_name.empty()) {
        auto it = script_pnl_.find(script_name);
        if (it != script_pnl_.end() && it->second < -config_.max_script_loss) {
            std::ostringstream oss;
            oss << "Script '" << script_name << "' loss limit breached: pnl=" << it->second
                << ", max_loss=" << config_.max_script_loss;
            reason = oss.str();
            return false;
        }
    }

    return true;
}

bool RiskManager::check_order_count_limit(std::string& reason) {
    if (open_order_count_ >= config_.max_open_orders) {
        std::ostringstream oss;
        oss << "Open order limit reached: current=" << open_order_count_
            << ", max=" << config_.max_open_orders;
        reason = oss.str();
        return false;
    }
    return true;
}

void RiskManager::check_and_trigger_emergency() {
    auto logger = get_logger("risk");

    // Check total loss
    if (config_.enforce_loss_limits && total_pnl_ < -config_.max_total_loss) {
        limits_breached_ = true;
        breach_reason_ = "Total loss limit breached: " + std::to_string(total_pnl_);
        logger->critical("RISK BREACH: {}", breach_reason_);
        if (emergency_callback_) {
            emergency_callback_();
        }
        return;
    }

    // Check per-script loss limits
    if (config_.enforce_loss_limits) {
        for (const auto& [name, pnl] : script_pnl_) {
            if (pnl < -config_.max_script_loss) {
                limits_breached_ = true;
                breach_reason_ = "Script '" + name + "' loss limit breached: " + std::to_string(pnl);
                logger->critical("RISK BREACH: {}", breach_reason_);
                if (emergency_callback_) {
                    emergency_callback_();
                }
                return;
            }
        }
    }

    // Check equity floor
    if (config_.equity_floor > 0) {
        // In production, this would compare against actual account equity
        // For now, we use total_pnl_ as a proxy
        // (Real implementation would query balance periodically)
    }
}

}  // namespace trader

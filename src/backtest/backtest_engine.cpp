#include "backtest/backtest_engine.hpp"
#include "scripting/lua_engine.hpp"
#include "core/logger.hpp"

#include <iostream>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace trader {

BacktestEngine::BacktestEngine(const BacktestConfig& config)
    : config_(config) {
}

bool BacktestEngine::run() {
    // Initialize logging if not already done
    auto logger = get_logger("backtest");

    // Load data feed
    feed_ = std::make_unique<DataFeed>();
    if (!feed_->load(config_.data_file)) {
        last_error_ = "Failed to load data: " + feed_->last_error();
        return false;
    }

    logger->info("Loaded {} candles from {}", feed_->size(), config_.data_file.string());

    // Create broker
    broker_ = std::make_unique<BacktestBroker>(config_.initial_balance, config_.commission_pct);

    // Create backtest REST client
    rest_client_ = std::make_shared<BacktestRestClient>(*feed_, *broker_, config_.pair);

    // Create LuaEngine with our backtest REST client (ws_client = nullptr)
    auto engine = std::make_unique<LuaEngine>("backtest", rest_client_, nullptr);

    // Set up parameters that scripts typically expect
    std::vector<std::pair<std::string, std::string>> params = {
        {"pair", config_.pair},
        {"threshold", "0.5"},
        {"quantity", "0.01"}
    };
    engine->set_parameters(params);

    // Load the script
    if (!engine->load_script(config_.script_file)) {
        last_error_ = "Failed to load script: " + engine->last_error();
        return false;
    }

    logger->info("Script loaded: {}", config_.script_file.string());

    // Call on_init()
    if (!engine->call_init()) {
        last_error_ = "Script on_init() failed: " + engine->last_error();
        return false;
    }

    // Main backtest loop: iterate over all candles
    for (size_t i = 0; i < feed_->size(); i++) {
        // Update the REST client's current candle index
        rest_client_->set_current_index(i);

        // Call on_tick() for each candle
        if (!engine->call_tick()) {
            logger->warn("on_tick() failed at candle {}: {}", i, engine->last_error());
            // Continue processing remaining candles even if one tick fails
        }

        // Record equity for drawdown tracking
        const auto& candle = feed_->at(i);
        std::unordered_map<std::string, double> prices;
        prices[config_.pair] = candle.close;
        // Also add pair without slash for position lookups
        std::string no_slash = config_.pair;
        no_slash.erase(std::remove(no_slash.begin(), no_slash.end(), '/'), no_slash.end());
        prices[no_slash] = candle.close;
        // Also add Kraken internal format (e.g., "XXBTZUSD") for scripts using that format
        if (no_slash.size() == 6) {
            std::string kraken_fmt = "X" + no_slash.substr(0, 3) + "Z" + no_slash.substr(3);
            prices[kraken_fmt] = candle.close;
        }
        double equity = broker_->get_equity(prices);
        broker_->record_equity(equity);
    }

    // Call on_stop()
    engine->call_stop();

    logger->info("Backtest complete: {} candles processed, {} trades executed",
                 feed_->size(), broker_->num_trades());

    // Compute results
    result_ = compute_results();
    return true;
}

BacktestResult BacktestEngine::compute_results() {
    BacktestResult r;
    r.initial_balance = config_.initial_balance;
    r.candles_processed = static_cast<int>(feed_->size());

    if (feed_->size() > 0) {
        r.start_timestamp = feed_->at(0).timestamp;
        r.end_timestamp = feed_->at(feed_->size() - 1).timestamp;
    }

    // Final equity
    const auto& equity_curve = broker_->equity_curve();
    if (!equity_curve.empty()) {
        r.final_equity = equity_curve.back();
    } else {
        r.final_equity = config_.initial_balance;
    }

    r.total_pnl = r.final_equity - r.initial_balance;
    r.return_pct = (r.total_pnl / r.initial_balance) * 100.0;

    // Trade statistics
    const auto& trades = broker_->trade_history();
    r.num_trades = static_cast<int>(trades.size());

    double gross_profit = 0.0;
    double gross_loss = 0.0;
    r.winning_trades = 0;
    r.losing_trades = 0;

    for (const auto& trade : trades) {
        if (trade.realized_pnl > 0) {
            r.winning_trades++;
            gross_profit += trade.realized_pnl;
        } else if (trade.realized_pnl < 0) {
            r.losing_trades++;
            gross_loss += std::abs(trade.realized_pnl);
        }
    }

    // Win rate
    int trades_with_pnl = r.winning_trades + r.losing_trades;
    r.win_rate = trades_with_pnl > 0 ? (static_cast<double>(r.winning_trades) / trades_with_pnl) * 100.0 : 0.0;

    // Profit factor
    r.profit_factor = gross_loss > 0 ? gross_profit / gross_loss : (gross_profit > 0 ? 999.99 : 0.0);

    // Max drawdown
    r.max_drawdown = 0.0;
    r.max_drawdown_pct = 0.0;
    double peak = 0.0;

    for (double eq : equity_curve) {
        if (eq > peak) peak = eq;
        double drawdown = peak - eq;
        if (drawdown > r.max_drawdown) {
            r.max_drawdown = drawdown;
            r.max_drawdown_pct = (drawdown / peak) * 100.0;
        }
    }

    // Sharpe ratio (annualized, assuming hourly data, 0% risk-free rate)
    if (equity_curve.size() > 1) {
        std::vector<double> returns;
        for (size_t i = 1; i < equity_curve.size(); i++) {
            if (equity_curve[i - 1] > 0) {
                returns.push_back((equity_curve[i] - equity_curve[i - 1]) / equity_curve[i - 1]);
            }
        }

        if (!returns.empty()) {
            double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
            double sq_sum = 0.0;
            for (double ret : returns) {
                sq_sum += (ret - mean_return) * (ret - mean_return);
            }
            double std_dev = std::sqrt(sq_sum / returns.size());

            // Annualize: assume hourly candles -> 8760 periods per year
            if (std_dev > 0) {
                r.sharpe_ratio = (mean_return / std_dev) * std::sqrt(8760.0);
            }
        }
    }

    return r;
}

}  // namespace trader

#pragma once

#include <string>
#include <memory>
#include <filesystem>
#include <vector>

#include "backtest/data_feed.hpp"
#include "backtest/backtest_broker.hpp"
#include "backtest/backtest_rest_client.hpp"

namespace trader {

/// Configuration for a backtest run
struct BacktestConfig {
    std::filesystem::path data_file;    // CSV file path
    std::filesystem::path script_file;  // Lua script path
    std::string pair;                   // Trading pair (e.g., "XBT/USD")
    double initial_balance = 10000.0;   // Starting cash
    double commission_pct = 0.1;        // Commission as percentage
};

/// Results of a backtest run
struct BacktestResult {
    double initial_balance = 0.0;
    double final_equity = 0.0;
    double total_pnl = 0.0;
    double return_pct = 0.0;
    int num_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    double win_rate = 0.0;
    double profit_factor = 0.0;
    double max_drawdown = 0.0;
    double max_drawdown_pct = 0.0;
    double sharpe_ratio = 0.0;
    int64_t start_timestamp = 0;
    int64_t end_timestamp = 0;
    int candles_processed = 0;
};

/// Orchestrates a backtest: loads data, runs script, computes results
class BacktestEngine {
public:
    BacktestEngine(const BacktestConfig& config);

    /// Run the backtest
    /// @return true if completed successfully
    bool run();

    /// Get the backtest results (valid after run() returns true)
    const BacktestResult& result() const { return result_; }

    /// Get the last error message
    const std::string& last_error() const { return last_error_; }

    /// Get the broker for detailed trade access
    const BacktestBroker& broker() const { return *broker_; }

private:
    /// Compute performance metrics from broker data
    BacktestResult compute_results();

    BacktestConfig config_;
    BacktestResult result_;
    std::string last_error_;

    std::unique_ptr<DataFeed> feed_;
    std::unique_ptr<BacktestBroker> broker_;
    std::shared_ptr<BacktestRestClient> rest_client_;
};

}  // namespace trader

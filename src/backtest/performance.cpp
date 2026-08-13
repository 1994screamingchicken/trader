#include "backtest/performance.hpp"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <ctime>

namespace trader {

namespace {
    std::string format_timestamp(int64_t ts) {
        std::time_t t = static_cast<std::time_t>(ts);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&t));
        return std::string(buf);
    }
}

void print_performance_report(const BacktestResult& result, const BacktestBroker& broker) {
    std::cout << "\n";
    std::cout << "======================================================" << std::endl;
    std::cout << "              BACKTEST PERFORMANCE REPORT              " << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << std::endl;

    // Period
    std::cout << "  Period:           " << format_timestamp(result.start_timestamp)
              << " to " << format_timestamp(result.end_timestamp) << std::endl;
    std::cout << "  Candles:          " << result.candles_processed << std::endl;
    std::cout << std::endl;

    // P&L
    std::cout << "  --- Profit & Loss ---" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Initial Balance:  $" << result.initial_balance << std::endl;
    std::cout << "  Final Equity:     $" << result.final_equity << std::endl;
    std::cout << "  Total P&L:        $" << result.total_pnl
              << " (" << (result.total_pnl >= 0 ? "+" : "") << result.return_pct << "%)" << std::endl;
    std::cout << std::endl;

    // Trade statistics
    std::cout << "  --- Trade Statistics ---" << std::endl;
    std::cout << "  Total Trades:     " << result.num_trades << std::endl;
    std::cout << "  Winning Trades:   " << result.winning_trades << std::endl;
    std::cout << "  Losing Trades:    " << result.losing_trades << std::endl;
    std::cout << "  Win Rate:         " << result.win_rate << "%" << std::endl;
    std::cout << "  Profit Factor:    " << result.profit_factor << std::endl;
    std::cout << std::endl;

    // Risk metrics
    std::cout << "  --- Risk Metrics ---" << std::endl;
    std::cout << "  Max Drawdown:     $" << result.max_drawdown
              << " (" << result.max_drawdown_pct << "%)" << std::endl;
    std::cout << "  Sharpe Ratio:     " << std::setprecision(3) << result.sharpe_ratio << std::endl;
    std::cout << std::endl;

    // Trade log (last 10 trades)
    const auto& trades = broker.trade_history();
    if (!trades.empty()) {
        std::cout << "  --- Recent Trades (last " << std::min(trades.size(), size_t(10)) << ") ---" << std::endl;
        std::cout << "  " << std::left << std::setw(20) << "Time"
                  << std::setw(6) << "Side"
                  << std::setw(12) << "Volume"
                  << std::setw(12) << "Price"
                  << std::setw(12) << "P&L" << std::endl;
        std::cout << "  " << std::string(62, '-') << std::endl;

        size_t start_idx = trades.size() > 10 ? trades.size() - 10 : 0;
        for (size_t i = start_idx; i < trades.size(); i++) {
            const auto& t = trades[i];
            std::cout << "  " << std::left << std::setw(20) << format_timestamp(t.timestamp)
                      << std::setw(6) << (t.side == OrderSide::Buy ? "BUY" : "SELL")
                      << std::fixed << std::setprecision(6) << std::setw(12) << t.volume
                      << std::setprecision(2) << std::setw(12) << t.price
                      << std::setw(12) << t.realized_pnl << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << std::endl;
}

}  // namespace trader

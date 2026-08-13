#pragma once

#include "backtest/backtest_engine.hpp"
#include "backtest/backtest_broker.hpp"

namespace trader {

/// Print a formatted performance report to stdout
void print_performance_report(const BacktestResult& result, const BacktestBroker& broker);

}  // namespace trader

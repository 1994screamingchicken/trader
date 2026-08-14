#pragma once

#include <string>
#include <vector>
#include <span>
#include <cstdint>
#include <filesystem>

namespace trader {

/// A single OHLC candle from historical data
struct OhlcCandle {
    int64_t timestamp;
    double open;
    double high;
    double low;
    double close;
    double volume;
};

/// Reads a CSV file of OHLC candles and provides iteration
class DataFeed {
public:
    DataFeed() = default;

    /// Load candles from a CSV file
    /// Expected columns: timestamp,open,high,low,close,volume
    /// @return true if loaded successfully
    bool load(const std::filesystem::path& csv_path);

    /// Get number of loaded candles
    size_t size() const { return candles_.size(); }

    /// Check if feed is empty
    bool empty() const { return candles_.empty(); }

    /// Get candle at index
    const OhlcCandle& at(size_t index) const { return candles_.at(index); }

    /// Get all candles
    const std::vector<OhlcCandle>& candles() const { return candles_; }

    /// Get a view of candles up to (and including) a given index (no copy)
    std::span<const OhlcCandle> candles_up_to(size_t index) const;

    /// Get the last error message
    const std::string& last_error() const { return last_error_; }

private:
    std::vector<OhlcCandle> candles_;
    std::string last_error_;
};

}  // namespace trader

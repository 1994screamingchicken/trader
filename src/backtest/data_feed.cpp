#include "backtest/data_feed.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <span>

namespace trader {

bool DataFeed::load(const std::filesystem::path& csv_path) {
    candles_.clear();
    last_error_.clear();

    std::ifstream file(csv_path);
    if (!file.is_open()) {
        last_error_ = "Cannot open file: " + csv_path.string();
        return false;
    }

    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;

        // Skip empty lines
        if (line.empty()) continue;

        // Skip header line (contains non-numeric first field)
        if (line_num == 1) {
            // Check if first char is a letter (header)
            size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos && !std::isdigit(line[start]) && line[start] != '-') {
                continue;
            }
        }

        // Parse CSV line
        std::istringstream ss(line);
        std::string token;
        std::vector<std::string> fields;

        while (std::getline(ss, token, ',')) {
            // Trim whitespace
            size_t s = token.find_first_not_of(" \t");
            size_t e = token.find_last_not_of(" \t");
            if (s != std::string::npos) {
                fields.push_back(token.substr(s, e - s + 1));
            } else {
                fields.push_back("");
            }
        }

        if (fields.size() < 6) {
            last_error_ = "Line " + std::to_string(line_num) + ": expected at least 6 fields, got " +
                          std::to_string(fields.size());
            return false;
        }

        try {
            OhlcCandle candle;
            candle.timestamp = std::stoll(fields[0]);
            candle.open = std::stod(fields[1]);
            candle.high = std::stod(fields[2]);
            candle.low = std::stod(fields[3]);
            candle.close = std::stod(fields[4]);
            candle.volume = std::stod(fields[5]);
            candles_.push_back(candle);
        } catch (const std::exception& e) {
            last_error_ = "Line " + std::to_string(line_num) + ": parse error: " + e.what();
            return false;
        }
    }

    if (candles_.empty()) {
        last_error_ = "No candles loaded from file";
        return false;
    }

    // Sort by timestamp
    std::sort(candles_.begin(), candles_.end(),
              [](const OhlcCandle& a, const OhlcCandle& b) {
                  return a.timestamp < b.timestamp;
              });

    return true;
}

std::span<const OhlcCandle> DataFeed::candles_up_to(size_t index) const {
    if (index >= candles_.size()) {
        return std::span<const OhlcCandle>(candles_);
    }
    return std::span<const OhlcCandle>(candles_.data(), index + 1);
}

}  // namespace trader

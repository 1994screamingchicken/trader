#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace trader {

/// Kraken API credentials and endpoint configuration
struct KrakenConfig {
    std::string api_key;
    std::string api_secret;
    std::string rest_url = "https://api.kraken.com";
    std::string ws_url = "wss://ws.kraken.com";
    std::string ws_auth_url = "wss://ws-auth.kraken.com";
};

/// Risk management parameters
struct RiskConfig {
    /// Maximum position size per asset (in base currency units)
    double max_position_size = 1.0;
    /// Maximum total loss before halting all scripts (in quote currency)
    double max_total_loss = 1000.0;
    /// Maximum loss per individual script before stopping it
    double max_script_loss = 200.0;
    /// Maximum number of open orders across all scripts
    int max_open_orders = 20;
    /// Emergency stop - halt everything if equity drops below this
    double equity_floor = 0.0;
    /// Whether to enforce position limits
    bool enforce_position_limits = true;
    /// Whether to enforce loss limits
    bool enforce_loss_limits = true;
};

/// Rate limiting configuration
struct RateLimitConfig {
    /// Maximum API calls per second (Kraken allows ~1/sec for private, ~1/sec for public)
    int max_calls_per_second = 1;
    /// Maximum API calls per minute
    int max_calls_per_minute = 15;
    /// Cooldown period in seconds after hitting rate limit
    int cooldown_seconds = 30;
    /// Per-script rate limit (calls per minute)
    int per_script_calls_per_minute = 5;
};

/// Script configuration entry
struct ScriptEntry {
    std::string name;
    std::filesystem::path path;
    bool auto_start = false;
    /// Script-specific parameters passed to the Lua environment
    std::vector<std::pair<std::string, std::string>> parameters;
};

/// Logging configuration
struct LogConfig {
    std::string level = "info";  // trace, debug, info, warn, error, critical
    std::filesystem::path log_file = "trader.log";
    bool console_output = true;
    bool file_output = true;
    size_t max_file_size_mb = 50;
    int max_files = 5;
};

/// Top-level application configuration
struct AppConfig {
    KrakenConfig kraken;
    RiskConfig risk;
    RateLimitConfig rate_limit;
    LogConfig logging;
    std::vector<ScriptEntry> scripts;
    std::filesystem::path scripts_directory = "scripts";
    /// Interval in milliseconds for the main event loop
    int tick_interval_ms = 100;
    /// Whether to run in paper trading mode (no real orders)
    bool paper_trading = true;
};

/// Load configuration from a TOML file
/// @param path Path to the TOML configuration file
/// @return Parsed AppConfig, or throws std::runtime_error on failure
AppConfig load_config(const std::filesystem::path& path);

/// Validate configuration values for sanity
/// @param config The configuration to validate
/// @return Empty optional if valid, error message otherwise
std::optional<std::string> validate_config(const AppConfig& config);

}  // namespace trader

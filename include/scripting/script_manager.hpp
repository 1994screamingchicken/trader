#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "core/config.hpp"
#include "scripting/lua_engine.hpp"
#include "kraken/rest_client.hpp"
#include "kraken/websocket_client.hpp"

namespace trader {

/// State of an individual managed script
enum class ScriptState {
    Loaded,     // Script file loaded but not running
    Running,    // Script is actively executing
    Paused,     // Script is paused (tick not called)
    Stopped,    // Script has been stopped (cleanup done)
    Error       // Script encountered a fatal error
};

/// Convert ScriptState to string for display
std::string script_state_to_string(ScriptState state);

/// Information about a managed script instance
struct ScriptInfo {
    std::string name;
    std::filesystem::path path;
    ScriptState state = ScriptState::Loaded;
    std::string last_error;
    int tick_count = 0;
    int orders_placed = 0;
    int orders_cancelled = 0;
    int api_calls = 0;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point last_tick_at;
};

/// Forward declarations for safety components
class RateLimiter;
class RiskManager;

/// Manages loading, running, and monitoring multiple Lua trading scripts
class ScriptManager {
public:
    ScriptManager(std::shared_ptr<KrakenRestClient> rest_client,
                  std::shared_ptr<KrakenWebSocketClient> ws_client,
                  const AppConfig& config);
    ~ScriptManager();

    // --- Script lifecycle ---

    /// Load a script from configuration
    bool load_script(const ScriptEntry& entry);

    /// Load all scripts from configuration
    int load_all_scripts();

    /// Start a specific script by name
    bool start_script(const std::string& name);

    /// Stop a specific script by name
    bool stop_script(const std::string& name);

    /// Pause a script (stops ticking but keeps state)
    bool pause_script(const std::string& name);

    /// Resume a paused script
    bool resume_script(const std::string& name);

    /// Restart a script (stop + reload + start)
    bool restart_script(const std::string& name);

    /// Start all scripts that have auto_start enabled
    int start_auto_scripts();

    /// Stop all running scripts
    void stop_all();

    // --- Monitoring ---

    /// Get information about all managed scripts
    std::vector<ScriptInfo> get_all_script_info() const;

    /// Get information about a specific script
    std::optional<ScriptInfo> get_script_info(const std::string& name) const;

    /// Get names of all loaded scripts
    std::vector<std::string> get_script_names() const;

    /// Check if a script is running
    bool is_running(const std::string& name) const;

    // --- Event dispatch ---

    /// Tick all running scripts (called by main loop)
    void tick_all();

    /// Dispatch WebSocket data to interested scripts
    void dispatch_data(const std::string& channel, const std::string& pair, const json& data);

    // --- Safety integration ---

    /// Set the rate limiter (shared across all scripts)
    void set_rate_limiter(std::shared_ptr<RateLimiter> limiter);

    /// Set the risk manager (shared across all scripts)
    void set_risk_manager(std::shared_ptr<RiskManager> manager);

    // --- Emergency ---

    /// Emergency stop all scripts and cancel all orders
    void emergency_stop();

    /// Check if emergency stop has been triggered
    bool is_emergency_stopped() const { return emergency_stopped_; }

private:
    /// Internal script instance
    struct ManagedScript {
        ScriptEntry config;
        std::unique_ptr<LuaEngine> engine;
        ScriptState state = ScriptState::Loaded;
        int tick_count = 0;
        std::chrono::steady_clock::time_point started_at;
        std::chrono::steady_clock::time_point last_tick_at;
        int consecutive_errors = 0;
        static constexpr int MAX_CONSECUTIVE_ERRORS = 5;
    };

    /// Get a managed script by name (internal, must hold lock)
    ManagedScript* find_script(const std::string& name);
    const ManagedScript* find_script(const std::string& name) const;

    /// Create a LuaEngine for a script entry and wire up callbacks
    std::unique_ptr<LuaEngine> create_engine(const ScriptEntry& entry);

    std::shared_ptr<KrakenRestClient> rest_client_;
    std::shared_ptr<KrakenWebSocketClient> ws_client_;
    AppConfig config_;

    mutable std::mutex scripts_mutex_;
    std::unordered_map<std::string, ManagedScript> scripts_;

    std::shared_ptr<RateLimiter> rate_limiter_;
    std::shared_ptr<RiskManager> risk_manager_;

    std::atomic<bool> emergency_stopped_{false};

    std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace trader

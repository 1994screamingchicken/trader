#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "core/config.hpp"
#include "kraken/rest_client.hpp"
#include "kraken/websocket_client.hpp"

namespace trader {

using json = nlohmann::json;

/// Callback for risk checks before order placement
using OrderCheckCallback = std::function<bool(const OrderRequest&, std::string&)>;

/// Callback for rate limit checks
using ScriptRateLimitCallback = std::function<bool(const std::string& script_name)>;

/// Represents the runtime state of a single Lua script
class LuaEngine {
public:
    LuaEngine(const std::string& script_name,
              std::shared_ptr<KrakenRestClient> rest_client,
              std::shared_ptr<KrakenWebSocketClient> ws_client);
    ~LuaEngine();

    /// Load a Lua script from file
    /// @return true if loaded successfully
    bool load_script(const std::filesystem::path& path);

    /// Set script parameters (accessible in Lua as `params` table)
    void set_parameters(const std::vector<std::pair<std::string, std::string>>& params);

    /// Call the script's on_init() function (if it exists)
    bool call_init();

    /// Call the script's on_tick() function (main loop callback)
    bool call_tick();

    /// Call the script's on_data(channel, pair, data) function
    bool call_on_data(const std::string& channel, const std::string& pair, const json& data);

    /// Call the script's on_stop() function for cleanup
    bool call_stop();

    /// Check if the script has encountered a fatal error
    bool has_error() const { return has_error_; }

    /// Get the last error message
    const std::string& last_error() const { return last_error_; }

    /// Get the script name
    const std::string& name() const { return script_name_; }

    // --- Callbacks for safety integration ---

    /// Set callback for order validation (risk manager)
    void set_order_check(OrderCheckCallback callback) { order_check_ = std::move(callback); }

    /// Set callback for rate limiting
    void set_rate_limit_check(ScriptRateLimitCallback callback) { rate_limit_check_ = std::move(callback); }

    /// Get statistics
    int orders_placed() const { return orders_placed_; }
    int orders_cancelled() const { return orders_cancelled_; }
    int api_calls() const { return api_calls_; }

private:
    /// Register all trading API functions into the Lua state
    void register_trading_api();

    /// Apply the sandbox (remove dangerous functions)
    void apply_sandbox();

    /// Lua-exposed: place a market order
    sol::table lua_market_order(const std::string& pair, const std::string& side, double volume);

    /// Lua-exposed: place a limit order
    sol::table lua_limit_order(const std::string& pair, const std::string& side,
                               double volume, double price);

    /// Lua-exposed: cancel an order by txid
    sol::table lua_cancel_order(const std::string& txid);

    /// Lua-exposed: cancel all orders
    sol::table lua_cancel_all();

    /// Lua-exposed: get ticker price for a pair
    sol::table lua_get_ticker(const std::string& pair);

    /// Lua-exposed: get OHLC data
    sol::table lua_get_ohlc(const std::string& pair, int interval);

    /// Lua-exposed: get order book
    sol::table lua_get_order_book(const std::string& pair, int depth);

    /// Lua-exposed: get account balance
    sol::table lua_get_balance();

    /// Lua-exposed: get open orders
    sol::table lua_get_open_orders();

    /// Lua-exposed: get open positions
    sol::table lua_get_positions();

    /// Lua-exposed: subscribe to WebSocket channel
    void lua_subscribe(const std::string& channel, sol::table pairs);

    /// Lua-exposed: unsubscribe from WebSocket channel
    void lua_unsubscribe(const std::string& channel, sol::table pairs);

    /// Lua-exposed: log functions
    void lua_log_info(const std::string& msg);
    void lua_log_warn(const std::string& msg);
    void lua_log_error(const std::string& msg);
    void lua_log_debug(const std::string& msg);

    /// Lua-exposed: get current timestamp (milliseconds)
    int64_t lua_timestamp();

    /// Lua-exposed: sleep for milliseconds (capped)
    void lua_sleep(int ms);

    /// Convert ApiResult to a Lua table
    sol::table api_result_to_lua(const ApiResult& result);

    /// Convert JSON to a Lua-friendly sol::object
    sol::object json_to_lua(const json& j);

    sol::state lua_;
    std::string script_name_;
    std::shared_ptr<KrakenRestClient> rest_client_;
    std::shared_ptr<KrakenWebSocketClient> ws_client_;

    OrderCheckCallback order_check_;
    ScriptRateLimitCallback rate_limit_check_;

    bool has_error_ = false;
    std::string last_error_;

    // Statistics
    int orders_placed_ = 0;
    int orders_cancelled_ = 0;
    int api_calls_ = 0;

    // Script-specific logger
    std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace trader

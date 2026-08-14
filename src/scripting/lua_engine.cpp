#include "scripting/lua_engine.hpp"
#include "scripting/sandbox.hpp"
#include "core/logger.hpp"

#include <chrono>
#include <thread>
#include <algorithm>

namespace trader {

LuaEngine::LuaEngine(const std::string& script_name,
                     std::shared_ptr<KrakenRestClient> rest_client,
                     std::shared_ptr<KrakenWebSocketClient> ws_client)
    : script_name_(script_name),
      rest_client_(std::move(rest_client)),
      ws_client_(std::move(ws_client)) {

    logger_ = get_logger("script:" + script_name);

    // Open standard Lua libraries
    lua_.open_libraries(
        sol::lib::base,
        sol::lib::string,
        sol::lib::table,
        sol::lib::math,
        sol::lib::coroutine
    );

    // Apply sandbox before loading any user code
    apply_sandbox();

    // Register the trading API
    register_trading_api();
}

LuaEngine::~LuaEngine() = default;

bool LuaEngine::load_script(const std::filesystem::path& path) {
    logger_->info("Loading script: {}", path.string());

    try {
        auto result = lua_.safe_script_file(path.string(), sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error err = result;
            last_error_ = err.what();
            has_error_ = true;
            logger_->error("Failed to load script: {}", last_error_);
            return false;
        }
        logger_->info("Script loaded successfully");
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        has_error_ = true;
        logger_->error("Exception loading script: {}", last_error_);
        return false;
    }
}

void LuaEngine::set_parameters(const std::vector<std::pair<std::string, std::string>>& params) {
    sol::table params_table = lua_.create_table();
    for (const auto& [key, value] : params) {
        // Try to convert to number, otherwise keep as string
        try {
            double num = std::stod(value);
            params_table[key] = num;
        } catch (...) {
            params_table[key] = value;
        }
    }
    lua_["params"] = params_table;
}

bool LuaEngine::call_init() {
    sol::function fn = lua_["on_init"];
    if (!fn.valid()) {
        logger_->debug("No on_init() function defined");
        return true;
    }

    try {
        auto result = fn();
        if (!result.valid()) {
            sol::error err = result;
            last_error_ = err.what();
            logger_->error("on_init() error: {}", last_error_);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        logger_->error("on_init() exception: {}", last_error_);
        return false;
    }
}

bool LuaEngine::call_tick() {
    sol::function fn = lua_["on_tick"];
    if (!fn.valid()) {
        return true;  // No tick handler is fine
    }

    try {
        auto result = fn();
        if (!result.valid()) {
            sol::error err = result;
            last_error_ = err.what();
            logger_->error("on_tick() error: {}", last_error_);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        logger_->error("on_tick() exception: {}", last_error_);
        return false;
    }
}

bool LuaEngine::call_on_data(const std::string& channel, const std::string& pair, const json& data) {
    sol::function fn = lua_["on_data"];
    if (!fn.valid()) {
        return true;
    }

    try {
        sol::object lua_data = json_to_lua(data);
        auto result = fn(channel, pair, lua_data);
        if (!result.valid()) {
            sol::error err = result;
            last_error_ = err.what();
            logger_->error("on_data() error: {}", last_error_);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        logger_->error("on_data() exception: {}", last_error_);
        return false;
    }
}

bool LuaEngine::call_stop() {
    sol::function fn = lua_["on_stop"];
    if (!fn.valid()) {
        logger_->debug("No on_stop() function defined");
        return true;
    }

    try {
        auto result = fn();
        if (!result.valid()) {
            sol::error err = result;
            last_error_ = err.what();
            logger_->warn("on_stop() error: {}", last_error_);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        logger_->warn("on_stop() exception: {}", last_error_);
        return false;
    }
}

// ==================== Private Methods ====================

void LuaEngine::register_trading_api() {
    // Create the main 'trader' namespace table
    sol::table api = lua_.create_named_table("trader");

    // --- Order functions ---
    api["market_order"] = [this](const std::string& pair, const std::string& side, double volume) {
        return lua_market_order(pair, side, volume);
    };

    api["limit_order"] = [this](const std::string& pair, const std::string& side,
                                double volume, double price) {
        return lua_limit_order(pair, side, volume, price);
    };

    api["cancel_order"] = [this](const std::string& txid) {
        return lua_cancel_order(txid);
    };

    api["cancel_all"] = [this]() {
        return lua_cancel_all();
    };

    // --- Market data functions ---
    api["get_ticker"] = [this](const std::string& pair) {
        return lua_get_ticker(pair);
    };

    api["get_ohlc"] = [this](const std::string& pair, sol::optional<int> interval) {
        return lua_get_ohlc(pair, interval.value_or(1));
    };

    api["get_order_book"] = [this](const std::string& pair, sol::optional<int> depth) {
        return lua_get_order_book(pair, depth.value_or(25));
    };

    // --- Account functions ---
    api["get_balance"] = [this]() {
        return lua_get_balance();
    };

    api["get_open_orders"] = [this]() {
        return lua_get_open_orders();
    };

    api["get_positions"] = [this]() {
        return lua_get_positions();
    };

    // --- WebSocket subscription ---
    api["subscribe"] = [this](const std::string& channel, sol::table pairs) {
        lua_subscribe(channel, pairs);
    };

    api["unsubscribe"] = [this](const std::string& channel, sol::table pairs) {
        lua_unsubscribe(channel, pairs);
    };

    // --- Utility functions ---
    api["timestamp"] = [this]() { return lua_timestamp(); };
    api["sleep"] = [this](int ms) { lua_sleep(ms); };
    api["name"] = script_name_;

    // --- Logging (also override global print) ---
    sol::table log = lua_.create_named_table("log");
    log["info"] = [this](const std::string& msg) { lua_log_info(msg); };
    log["warn"] = [this](const std::string& msg) { lua_log_warn(msg); };
    log["error"] = [this](const std::string& msg) { lua_log_error(msg); };
    log["debug"] = [this](const std::string& msg) { lua_log_debug(msg); };

    // Override print to route through logging
    lua_["print"] = [this](sol::variadic_args va) {
        std::string msg;
        for (auto v : va) {
            if (!msg.empty()) msg += "\t";
            msg += lua_["tostring"](v.get<sol::object>()).get<std::string>();
        }
        lua_log_info(msg);
    };
}

void LuaEngine::apply_sandbox() {
    apply_lua_sandbox(lua_);
    // Set instruction limit to prevent infinite loops (10M instructions per call)
    set_lua_instruction_limit(lua_, 10000000);
}

// ==================== Lua API Implementations ====================

sol::table LuaEngine::lua_market_order(const std::string& pair, const std::string& side, double volume) {
    // Rate limit check
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    OrderRequest req;
    req.pair = pair;
    req.side = (side == "buy") ? OrderSide::Buy : OrderSide::Sell;
    req.type = OrderType::Market;
    req.volume = volume;

    // Risk check
    if (order_check_) {
        std::string reason;
        if (!order_check_(req, reason)) {
            sol::table result = lua_.create_table();
            result["success"] = false;
            result["error"] = "Risk check failed: " + reason;
            logger_->warn("Order blocked by risk manager: {}", reason);
            return result;
        }
    }

    api_calls_++;
    auto api_result = rest_client_->place_order(req);
    orders_placed_++;
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_limit_order(const std::string& pair, const std::string& side,
                                       double volume, double price) {
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    OrderRequest req;
    req.pair = pair;
    req.side = (side == "buy") ? OrderSide::Buy : OrderSide::Sell;
    req.type = OrderType::Limit;
    req.volume = volume;
    req.price = price;

    if (order_check_) {
        std::string reason;
        if (!order_check_(req, reason)) {
            sol::table result = lua_.create_table();
            result["success"] = false;
            result["error"] = "Risk check failed: " + reason;
            logger_->warn("Order blocked by risk manager: {}", reason);
            return result;
        }
    }

    api_calls_++;
    auto api_result = rest_client_->place_order(req);
    orders_placed_++;
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_cancel_order(const std::string& txid) {
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->cancel_order(txid);
    orders_cancelled_++;
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_cancel_all() {
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->cancel_all_orders();
    orders_cancelled_++;
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_get_ticker(const std::string& pair) {
    // First try the WebSocket cache for real-time data
    if (ws_client_ && ws_client_->is_connected()) {
        auto cached = ws_client_->get_ticker(pair);
        if (!cached.empty()) {
            sol::table result = lua_.create_table();
            result["success"] = true;
            result["data"] = json_to_lua(cached);
            return result;
        }
    }

    // Fall back to REST API
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->get_ticker(pair);
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_get_ohlc(const std::string& pair, int interval) {
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->get_ohlc(pair, interval);
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_get_order_book(const std::string& pair, int depth) {
    // Try WebSocket cache first
    if (ws_client_ && ws_client_->is_connected()) {
        auto cached = ws_client_->get_order_book(pair);
        if (!cached.empty()) {
            sol::table result = lua_.create_table();
            result["success"] = true;
            result["data"] = json_to_lua(cached);
            return result;
        }
    }

    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->get_order_book(pair, depth);
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_get_balance() {
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->get_balance();
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_get_open_orders() {
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->get_open_orders();
    return api_result_to_lua(api_result);
}

sol::table LuaEngine::lua_get_positions() {
    if (rate_limit_check_ && !rate_limit_check_(script_name_)) {
        sol::table result = lua_.create_table();
        result["success"] = false;
        result["error"] = "Rate limit exceeded";
        return result;
    }

    api_calls_++;
    auto api_result = rest_client_->get_open_positions();
    return api_result_to_lua(api_result);
}

void LuaEngine::lua_subscribe(const std::string& channel, sol::table pairs) {
    if (!ws_client_) {
        logger_->warn("WebSocket client not available");
        return;
    }

    WsSubscription sub;
    sub.channel = channel;
    for (auto& [key, val] : pairs) {
        if (val.is<std::string>()) {
            sub.pairs.push_back(val.as<std::string>());
        }
    }

    ws_client_->subscribe(sub);
}

void LuaEngine::lua_unsubscribe(const std::string& channel, sol::table pairs) {
    if (!ws_client_) return;

    WsSubscription sub;
    sub.channel = channel;
    for (auto& [key, val] : pairs) {
        if (val.is<std::string>()) {
            sub.pairs.push_back(val.as<std::string>());
        }
    }

    ws_client_->unsubscribe(sub);
}

void LuaEngine::lua_log_info(const std::string& msg) {
    logger_->info("{}", msg);
}

void LuaEngine::lua_log_warn(const std::string& msg) {
    logger_->warn("{}", msg);
}

void LuaEngine::lua_log_error(const std::string& msg) {
    logger_->error("{}", msg);
}

void LuaEngine::lua_log_debug(const std::string& msg) {
    logger_->debug("{}", msg);
}

int64_t LuaEngine::lua_timestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void LuaEngine::lua_sleep(int ms) {
    // Cap sleep to prevent scripts from blocking too long
    int capped = std::min(ms, 5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(capped));
}

sol::table LuaEngine::api_result_to_lua(const ApiResult& result) {
    sol::table tbl = lua_.create_table();
    tbl["success"] = result.success;

    if (result.success) {
        tbl["data"] = json_to_lua(result.data);
    } else {
        tbl["error"] = result.error;
    }

    return tbl;
}

sol::object LuaEngine::json_to_lua(const json& j) {
    if (j.is_null()) {
        return sol::make_object(lua_, sol::nil);
    } else if (j.is_boolean()) {
        return sol::make_object(lua_, j.get<bool>());
    } else if (j.is_number_integer()) {
        return sol::make_object(lua_, j.get<int64_t>());
    } else if (j.is_number_float()) {
        return sol::make_object(lua_, j.get<double>());
    } else if (j.is_string()) {
        return sol::make_object(lua_, j.get<std::string>());
    } else if (j.is_array()) {
        sol::table arr = lua_.create_table();
        int idx = 1;
        for (const auto& elem : j) {
            arr[idx++] = json_to_lua(elem);
        }
        return arr;
    } else if (j.is_object()) {
        sol::table obj = lua_.create_table();
        for (auto& [key, val] : j.items()) {
            obj[key] = json_to_lua(val);
        }
        return obj;
    }
    return sol::make_object(lua_, sol::nil);
}

}  // namespace trader

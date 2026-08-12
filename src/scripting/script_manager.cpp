#include "scripting/script_manager.hpp"
#include "safety/rate_limiter.hpp"
#include "safety/risk_manager.hpp"
#include "core/logger.hpp"

#include <algorithm>

namespace trader {

std::string script_state_to_string(ScriptState state) {
    switch (state) {
        case ScriptState::Loaded:  return "loaded";
        case ScriptState::Running: return "running";
        case ScriptState::Paused:  return "paused";
        case ScriptState::Stopped: return "stopped";
        case ScriptState::Error:   return "error";
    }
    return "unknown";
}

ScriptManager::ScriptManager(std::shared_ptr<KrakenRestClient> rest_client,
                             std::shared_ptr<KrakenWebSocketClient> ws_client,
                             const AppConfig& config)
    : rest_client_(std::move(rest_client)),
      ws_client_(std::move(ws_client)),
      config_(config) {
    logger_ = get_logger("script_manager");
}

ScriptManager::~ScriptManager() {
    stop_all();
}

// ==================== Script Lifecycle ====================

bool ScriptManager::load_script(const ScriptEntry& entry) {
    std::lock_guard<std::mutex> lock(scripts_mutex_);

    if (scripts_.contains(entry.name)) {
        logger_->warn("Script '{}' is already loaded", entry.name);
        return false;
    }

    auto engine = create_engine(entry);
    if (!engine) {
        return false;
    }

    // Resolve script path relative to scripts directory
    std::filesystem::path script_path = entry.path;
    if (script_path.is_relative()) {
        script_path = config_.scripts_directory / script_path;
    }

    if (!engine->load_script(script_path)) {
        logger_->error("Failed to load script '{}': {}", entry.name, engine->last_error());
        return false;
    }

    // Set parameters
    engine->set_parameters(entry.parameters);

    ManagedScript managed;
    managed.config = entry;
    managed.engine = std::move(engine);
    managed.state = ScriptState::Loaded;

    scripts_.emplace(entry.name, std::move(managed));
    logger_->info("Script '{}' loaded from {}", entry.name, script_path.string());
    return true;
}

int ScriptManager::load_all_scripts() {
    int count = 0;
    for (const auto& entry : config_.scripts) {
        if (load_script(entry)) {
            count++;
        }
    }
    logger_->info("Loaded {}/{} scripts from configuration", count, config_.scripts.size());
    return count;
}

bool ScriptManager::start_script(const std::string& name) {
    std::lock_guard<std::mutex> lock(scripts_mutex_);

    auto* script = find_script(name);
    if (!script) {
        logger_->error("Script '{}' not found", name);
        return false;
    }

    if (script->state == ScriptState::Running) {
        logger_->warn("Script '{}' is already running", name);
        return false;
    }

    if (emergency_stopped_) {
        logger_->error("Cannot start script '{}' - emergency stop active", name);
        return false;
    }

    // Call on_init if transitioning from Loaded or Stopped state
    if (script->state == ScriptState::Loaded || script->state == ScriptState::Stopped) {
        if (!script->engine->call_init()) {
            script->state = ScriptState::Error;
            logger_->error("Script '{}' on_init() failed: {}", name, script->engine->last_error());
            return false;
        }
    }

    script->state = ScriptState::Running;
    script->started_at = std::chrono::steady_clock::now();
    script->consecutive_errors = 0;
    logger_->info("Script '{}' started", name);
    return true;
}

bool ScriptManager::stop_script(const std::string& name) {
    std::lock_guard<std::mutex> lock(scripts_mutex_);

    auto* script = find_script(name);
    if (!script) {
        logger_->error("Script '{}' not found", name);
        return false;
    }

    if (script->state == ScriptState::Stopped) {
        return true;
    }

    // Call on_stop for cleanup
    script->engine->call_stop();
    script->state = ScriptState::Stopped;
    logger_->info("Script '{}' stopped (ticked {} times)", name, script->tick_count);
    return true;
}

bool ScriptManager::pause_script(const std::string& name) {
    std::lock_guard<std::mutex> lock(scripts_mutex_);

    auto* script = find_script(name);
    if (!script) {
        logger_->error("Script '{}' not found", name);
        return false;
    }

    if (script->state != ScriptState::Running) {
        logger_->warn("Script '{}' is not running (state: {})", name,
                      script_state_to_string(script->state));
        return false;
    }

    script->state = ScriptState::Paused;
    logger_->info("Script '{}' paused", name);
    return true;
}

bool ScriptManager::resume_script(const std::string& name) {
    std::lock_guard<std::mutex> lock(scripts_mutex_);

    auto* script = find_script(name);
    if (!script) {
        logger_->error("Script '{}' not found", name);
        return false;
    }

    if (script->state != ScriptState::Paused) {
        logger_->warn("Script '{}' is not paused (state: {})", name,
                      script_state_to_string(script->state));
        return false;
    }

    if (emergency_stopped_) {
        logger_->error("Cannot resume script '{}' - emergency stop active", name);
        return false;
    }

    script->state = ScriptState::Running;
    script->consecutive_errors = 0;
    logger_->info("Script '{}' resumed", name);
    return true;
}

bool ScriptManager::restart_script(const std::string& name) {
    std::lock_guard<std::mutex> lock(scripts_mutex_);

    auto* script = find_script(name);
    if (!script) {
        logger_->error("Script '{}' not found", name);
        return false;
    }

    // Stop
    script->engine->call_stop();

    // Recreate engine
    auto new_engine = create_engine(script->config);
    if (!new_engine) {
        script->state = ScriptState::Error;
        return false;
    }

    std::filesystem::path script_path = script->config.path;
    if (script_path.is_relative()) {
        script_path = config_.scripts_directory / script_path;
    }

    if (!new_engine->load_script(script_path)) {
        script->state = ScriptState::Error;
        logger_->error("Failed to reload script '{}': {}", name, new_engine->last_error());
        return false;
    }

    new_engine->set_parameters(script->config.parameters);
    script->engine = std::move(new_engine);
    script->tick_count = 0;
    script->consecutive_errors = 0;

    // Re-init and start
    if (!script->engine->call_init()) {
        script->state = ScriptState::Error;
        return false;
    }

    script->state = ScriptState::Running;
    script->started_at = std::chrono::steady_clock::now();
    logger_->info("Script '{}' restarted", name);
    return true;
}

int ScriptManager::start_auto_scripts() {
    int count = 0;
    // Need to collect names first to avoid recursive lock
    std::vector<std::string> auto_start_names;
    {
        std::lock_guard<std::mutex> lock(scripts_mutex_);
        for (const auto& [name, script] : scripts_) {
            if (script.config.auto_start) {
                auto_start_names.push_back(name);
            }
        }
    }

    for (const auto& name : auto_start_names) {
        if (start_script(name)) {
            count++;
        }
    }

    logger_->info("Auto-started {} scripts", count);
    return count;
}

void ScriptManager::stop_all() {
    std::lock_guard<std::mutex> lock(scripts_mutex_);

    for (auto& [name, script] : scripts_) {
        if (script.state == ScriptState::Running || script.state == ScriptState::Paused) {
            script.engine->call_stop();
            script.state = ScriptState::Stopped;
            logger_->info("Stopped script '{}'", name);
        }
    }
}

// ==================== Monitoring ====================

std::vector<ScriptInfo> ScriptManager::get_all_script_info() const {
    std::lock_guard<std::mutex> lock(scripts_mutex_);
    std::vector<ScriptInfo> infos;

    for (const auto& [name, script] : scripts_) {
        ScriptInfo info;
        info.name = name;
        info.path = script.config.path;
        info.state = script.state;
        info.last_error = script.engine ? script.engine->last_error() : "";
        info.tick_count = script.tick_count;
        info.orders_placed = script.engine ? script.engine->orders_placed() : 0;
        info.orders_cancelled = script.engine ? script.engine->orders_cancelled() : 0;
        info.api_calls = script.engine ? script.engine->api_calls() : 0;
        info.started_at = script.started_at;
        info.last_tick_at = script.last_tick_at;
        infos.push_back(info);
    }

    return infos;
}

std::optional<ScriptInfo> ScriptManager::get_script_info(const std::string& name) const {
    std::lock_guard<std::mutex> lock(scripts_mutex_);
    const auto* script = find_script(name);
    if (!script) return std::nullopt;

    ScriptInfo info;
    info.name = name;
    info.path = script->config.path;
    info.state = script->state;
    info.last_error = script->engine ? script->engine->last_error() : "";
    info.tick_count = script->tick_count;
    info.orders_placed = script->engine ? script->engine->orders_placed() : 0;
    info.orders_cancelled = script->engine ? script->engine->orders_cancelled() : 0;
    info.api_calls = script->engine ? script->engine->api_calls() : 0;
    info.started_at = script->started_at;
    info.last_tick_at = script->last_tick_at;
    return info;
}

std::vector<std::string> ScriptManager::get_script_names() const {
    std::lock_guard<std::mutex> lock(scripts_mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : scripts_) {
        names.push_back(name);
    }
    return names;
}

bool ScriptManager::is_running(const std::string& name) const {
    std::lock_guard<std::mutex> lock(scripts_mutex_);
    const auto* script = find_script(name);
    return script && script->state == ScriptState::Running;
}

// ==================== Event Dispatch ====================

void ScriptManager::tick_all() {
    if (emergency_stopped_) return;

    std::lock_guard<std::mutex> lock(scripts_mutex_);

    for (auto& [name, script] : scripts_) {
        if (script.state != ScriptState::Running) continue;

        bool success = script.engine->call_tick();
        script.last_tick_at = std::chrono::steady_clock::now();
        script.tick_count++;

        if (!success) {
            script.consecutive_errors++;
            logger_->warn("Script '{}' tick #{} failed ({}/{} consecutive errors): {}",
                          name, script.tick_count,
                          script.consecutive_errors, ManagedScript::MAX_CONSECUTIVE_ERRORS,
                          script.engine->last_error());

            if (script.consecutive_errors >= ManagedScript::MAX_CONSECUTIVE_ERRORS) {
                script.state = ScriptState::Error;
                logger_->error("Script '{}' stopped due to {} consecutive errors",
                               name, script.consecutive_errors);
            }
        } else {
            script.consecutive_errors = 0;
        }
    }
}

void ScriptManager::dispatch_data(const std::string& channel, const std::string& pair,
                                   const json& data) {
    if (emergency_stopped_) return;

    std::lock_guard<std::mutex> lock(scripts_mutex_);

    for (auto& [name, script] : scripts_) {
        if (script.state != ScriptState::Running) continue;
        script.engine->call_on_data(channel, pair, data);
    }
}

// ==================== Safety Integration ====================

void ScriptManager::set_rate_limiter(std::shared_ptr<RateLimiter> limiter) {
    rate_limiter_ = std::move(limiter);
}

void ScriptManager::set_risk_manager(std::shared_ptr<RiskManager> manager) {
    risk_manager_ = std::move(manager);
}

// ==================== Emergency ====================

void ScriptManager::emergency_stop() {
    logger_->critical("EMERGENCY STOP triggered - halting all scripts");
    emergency_stopped_ = true;

    // Stop all scripts
    stop_all();

    // Cancel all outstanding orders
    if (rest_client_) {
        rest_client_->cancel_all_orders();
    }
}

// ==================== Private ====================

ScriptManager::ManagedScript* ScriptManager::find_script(const std::string& name) {
    auto it = scripts_.find(name);
    if (it == scripts_.end()) return nullptr;
    return &it->second;
}

const ScriptManager::ManagedScript* ScriptManager::find_script(const std::string& name) const {
    auto it = scripts_.find(name);
    if (it == scripts_.end()) return nullptr;
    return &it->second;
}

std::unique_ptr<LuaEngine> ScriptManager::create_engine(const ScriptEntry& entry) {
    auto engine = std::make_unique<LuaEngine>(entry.name, rest_client_, ws_client_);

    // Wire up rate limiter
    if (rate_limiter_) {
        engine->set_rate_limit_check([this](const std::string& script_name) -> bool {
            return rate_limiter_->allow_request(script_name);
        });
    }

    // Wire up risk manager
    if (risk_manager_) {
        engine->set_order_check([this](const OrderRequest& order, std::string& reason) -> bool {
            return risk_manager_->check_order(order, reason);
        });
    }

    return engine;
}

}  // namespace trader

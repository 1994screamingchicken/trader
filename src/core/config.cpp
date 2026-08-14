#include "core/config.hpp"

#include <toml++/toml.hpp>
#include <stdexcept>
#include <fstream>

namespace trader {

AppConfig load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Configuration file not found: " + path.string());
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(std::string("Failed to parse config: ") + e.what());
    }

    AppConfig config;

    // [kraken] section
    if (auto kraken = tbl["kraken"].as_table()) {
        config.kraken.api_key = kraken->get("api_key")->value_or(std::string(""));
        config.kraken.api_secret = kraken->get("api_secret")->value_or(std::string(""));
        if (auto url = kraken->get("rest_url")) {
            config.kraken.rest_url = url->value_or(config.kraken.rest_url);
        }
        if (auto url = kraken->get("ws_url")) {
            config.kraken.ws_url = url->value_or(config.kraken.ws_url);
        }
        if (auto url = kraken->get("ws_auth_url")) {
            config.kraken.ws_auth_url = url->value_or(config.kraken.ws_auth_url);
        }
    }

    // [risk] section
    if (auto risk = tbl["risk"].as_table()) {
        if (auto v = risk->get("max_position_size"))
            config.risk.max_position_size = v->value_or(config.risk.max_position_size);
        if (auto v = risk->get("max_total_loss"))
            config.risk.max_total_loss = v->value_or(config.risk.max_total_loss);
        if (auto v = risk->get("max_script_loss"))
            config.risk.max_script_loss = v->value_or(config.risk.max_script_loss);
        if (auto v = risk->get("max_open_orders"))
            config.risk.max_open_orders = static_cast<int>(v->value_or(static_cast<int64_t>(config.risk.max_open_orders)));
        if (auto v = risk->get("equity_floor"))
            config.risk.equity_floor = v->value_or(config.risk.equity_floor);
        if (auto v = risk->get("enforce_position_limits"))
            config.risk.enforce_position_limits = v->value_or(config.risk.enforce_position_limits);
        if (auto v = risk->get("enforce_loss_limits"))
            config.risk.enforce_loss_limits = v->value_or(config.risk.enforce_loss_limits);
    }

    // [rate_limit] section
    if (auto rl = tbl["rate_limit"].as_table()) {
        if (auto v = rl->get("max_calls_per_second"))
            config.rate_limit.max_calls_per_second = static_cast<int>(v->value_or(static_cast<int64_t>(config.rate_limit.max_calls_per_second)));
        if (auto v = rl->get("max_calls_per_minute"))
            config.rate_limit.max_calls_per_minute = static_cast<int>(v->value_or(static_cast<int64_t>(config.rate_limit.max_calls_per_minute)));
        if (auto v = rl->get("cooldown_seconds"))
            config.rate_limit.cooldown_seconds = static_cast<int>(v->value_or(static_cast<int64_t>(config.rate_limit.cooldown_seconds)));
        if (auto v = rl->get("per_script_calls_per_minute"))
            config.rate_limit.per_script_calls_per_minute = static_cast<int>(v->value_or(static_cast<int64_t>(config.rate_limit.per_script_calls_per_minute)));
    }

    // [logging] section
    if (auto log = tbl["logging"].as_table()) {
        if (auto v = log->get("level"))
            config.logging.level = v->value_or(config.logging.level);
        if (auto v = log->get("log_file"))
            config.logging.log_file = v->value_or(std::string(config.logging.log_file.string()));
        if (auto v = log->get("console_output"))
            config.logging.console_output = v->value_or(config.logging.console_output);
        if (auto v = log->get("file_output"))
            config.logging.file_output = v->value_or(config.logging.file_output);
        if (auto v = log->get("max_file_size_mb"))
            config.logging.max_file_size_mb = static_cast<size_t>(v->value_or(static_cast<int64_t>(config.logging.max_file_size_mb)));
        if (auto v = log->get("max_files"))
            config.logging.max_files = static_cast<int>(v->value_or(static_cast<int64_t>(config.logging.max_files)));
    }

    // [general] section
    if (auto gen = tbl["general"].as_table()) {
        if (auto v = gen->get("scripts_directory"))
            config.scripts_directory = v->value_or(std::string(config.scripts_directory.string()));
        if (auto v = gen->get("tick_interval_ms"))
            config.tick_interval_ms = static_cast<int>(v->value_or(static_cast<int64_t>(config.tick_interval_ms)));
        if (auto v = gen->get("paper_trading"))
            config.paper_trading = v->value_or(config.paper_trading);
    }

    // [[scripts]] array
    if (auto scripts_arr = tbl["scripts"].as_array()) {
        for (auto& elem : *scripts_arr) {
            if (auto script_tbl = elem.as_table()) {
                ScriptEntry entry;
                entry.name = script_tbl->get("name")->value_or(std::string("unnamed"));
                entry.path = script_tbl->get("path")->value_or(std::string(""));
                if (auto v = script_tbl->get("auto_start"))
                    entry.auto_start = v->value_or(false);

                // Script parameters
                if (auto params = script_tbl->get("parameters")->as_table()) {
                    for (auto& [key, val] : *params) {
                        entry.parameters.emplace_back(
                            std::string(key.str()),
                            val.value_or(std::string(""))
                        );
                    }
                }

                config.scripts.push_back(std::move(entry));
            }
        }
    }

    return config;
}

std::optional<std::string> validate_config(const AppConfig& config) {
    if (config.kraken.api_key.empty() && !config.paper_trading) {
        return "Kraken API key is required for live trading";
    }
    if (config.kraken.api_secret.empty() && !config.paper_trading) {
        return "Kraken API secret is required for live trading";
    }
    if (config.risk.max_position_size <= 0) {
        return "max_position_size must be positive";
    }
    if (config.risk.max_total_loss <= 0) {
        return "max_total_loss must be positive";
    }
    if (config.risk.max_script_loss <= 0) {
        return "max_script_loss must be positive";
    }
    if (config.risk.max_open_orders <= 0) {
        return "max_open_orders must be positive";
    }
    if (config.rate_limit.max_calls_per_second <= 0) {
        return "max_calls_per_second must be positive";
    }
    if (config.rate_limit.max_calls_per_minute <= 0) {
        return "max_calls_per_minute must be positive";
    }
    if (config.tick_interval_ms <= 0) {
        return "tick_interval_ms must be positive";
    }

    // Validate script paths
    for (const auto& script : config.scripts) {
        if (script.path.empty()) {
            return "Script '" + script.name + "' has no path specified";
        }
    }

    return std::nullopt;
}

}  // namespace trader

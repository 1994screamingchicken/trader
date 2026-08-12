#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>
#include "core/config.hpp"

namespace trader {

/// Initialize the global logging system
void init_logging(const LogConfig& config);

/// Get the main application logger
std::shared_ptr<spdlog::logger> get_logger();

/// Get or create a named logger (for scripts, components, etc.)
std::shared_ptr<spdlog::logger> get_logger(const std::string& name);

}  // namespace trader

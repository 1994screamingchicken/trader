#include "core/logger.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <vector>

namespace trader {

static std::shared_ptr<spdlog::logger> s_main_logger;
static std::vector<spdlog::sink_ptr> s_sinks;

void init_logging(const LogConfig& config) {
    s_sinks.clear();

    if (config.console_output) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        s_sinks.push_back(console_sink);
    }

    if (config.file_output) {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            config.log_file.string(),
            config.max_file_size_mb * 1024 * 1024,
            config.max_files
        );
        s_sinks.push_back(file_sink);
    }

    s_main_logger = std::make_shared<spdlog::logger>("trader", s_sinks.begin(), s_sinks.end());

    // Set log level
    if (config.level == "trace") s_main_logger->set_level(spdlog::level::trace);
    else if (config.level == "debug") s_main_logger->set_level(spdlog::level::debug);
    else if (config.level == "info") s_main_logger->set_level(spdlog::level::info);
    else if (config.level == "warn") s_main_logger->set_level(spdlog::level::warn);
    else if (config.level == "error") s_main_logger->set_level(spdlog::level::err);
    else if (config.level == "critical") s_main_logger->set_level(spdlog::level::critical);
    else s_main_logger->set_level(spdlog::level::info);

    s_main_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    spdlog::register_logger(s_main_logger);
    spdlog::set_default_logger(s_main_logger);
}

std::shared_ptr<spdlog::logger> get_logger() {
    if (!s_main_logger) {
        // Fallback: create a simple console logger
        s_main_logger = spdlog::stdout_color_mt("trader");
    }
    return s_main_logger;
}

std::shared_ptr<spdlog::logger> get_logger(const std::string& name) {
    auto existing = spdlog::get(name);
    if (existing) return existing;

    auto logger = std::make_shared<spdlog::logger>(name, s_sinks.begin(), s_sinks.end());
    logger->set_level(s_main_logger ? s_main_logger->level() : spdlog::level::info);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    spdlog::register_logger(logger);
    return logger;
}

}  // namespace trader

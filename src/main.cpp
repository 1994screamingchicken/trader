#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

#include "core/config.hpp"
#include "core/logger.hpp"
#include "kraken/rest_client.hpp"
#include "kraken/websocket_client.hpp"
#include "scripting/script_manager.hpp"
#include "safety/rate_limiter.hpp"
#include "safety/risk_manager.hpp"
#include "tui/tui_app.hpp"
#include "tui/log_sink.hpp"

// g_running has external linkage for TUI module coordinated shutdown
std::atomic<bool> g_running{true};

// g_emergency has external linkage so the TUI can detect risk-limit breaches
std::atomic<bool> g_emergency{false};

namespace {
    void signal_handler(int signum) {
        if (signum == SIGINT || signum == SIGTERM) {
            if (g_running) {
                g_running = false;
            } else {
                // Second signal = force exit
                std::exit(1);
            }
        }
    }
}

void print_banner() {
    std::cout << R"(
  _  __          _               _                _____              _
 | |/ /         | |             | |              |_   _|            | |
 | ' / _ __ __ _| | _____ _ __ | |    _   _  __ _ | |_ __ __ _  __| | ___ _ __
 |  < | '__/ _` | |/ / _ \ '_ \| |   | | | |/ _` || | '__/ _` |/ _` |/ _ \ '__|
 | . \| | | (_| |   <  __/ | | | |___| |_| | (_| || | | | (_| | (_| |  __/ |
 |_|\_\_|  \__,_|_|\_\___|_| |_|______\__,_|\__,_|_/_|  \__,_|\__,_|\___|_|
)" << std::endl;
    std::cout << "  Automated Crypto Trading Platform v1.0.0" << std::endl;
    std::cout << "  Kraken Exchange | Lua Scripting Engine" << std::endl;
    std::cout << "  =========================================" << std::endl;
    std::cout << std::endl;
}

void print_status(const trader::ScriptManager& manager) {
    auto infos = manager.get_all_script_info();
    std::cout << "\n--- Script Status ---" << std::endl;
    for (const auto& info : infos) {
        std::cout << "  [" << trader::script_state_to_string(info.state) << "] "
                  << info.name
                  << " | ticks: " << info.tick_count
                  << " | orders: " << info.orders_placed
                  << " | api_calls: " << info.api_calls;
        if (!info.last_error.empty()) {
            std::cout << " | error: " << info.last_error;
        }
        std::cout << std::endl;
    }
    std::cout << "---------------------\n" << std::endl;
}

int main(int argc, char* argv[]) {
    print_banner();

    // --- Parse command line arguments ---
    std::filesystem::path config_path = "config/trader.toml";
    bool verbose = false;
    bool use_tui = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--tui") {
            use_tui = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: kraken_trader [OPTIONS]\n"
                      << "Options:\n"
                      << "  -c, --config <path>  Path to configuration file (default: config/trader.toml)\n"
                      << "  -v, --verbose        Enable verbose (debug) logging\n"
                      << "      --tui            Launch interactive terminal UI dashboard\n"
                      << "  -h, --help           Show this help message\n"
                      << std::endl;
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    // --- Load configuration ---
    trader::AppConfig config;
    try {
        config = trader::load_config(config_path);
        std::cout << "[OK] Configuration loaded from: " << config_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to load configuration: " << e.what() << std::endl;
        return 1;
    }

    // Apply verbose override
    if (verbose) {
        config.logging.level = "debug";
    }

    // Validate configuration
    auto validation_error = trader::validate_config(config);
    if (validation_error.has_value()) {
        std::cerr << "[ERROR] Configuration validation failed: " << *validation_error << std::endl;
        return 1;
    }

    // --- Initialize logging ---
    trader::init_logging(config.logging);
    auto logger = trader::get_logger();
    logger->info("Kraken Lua Trader starting up");

    if (config.paper_trading) {
        logger->warn("=== PAPER TRADING MODE === No real orders will be placed");
        std::cout << "[!] PAPER TRADING MODE - No real orders will be placed" << std::endl;
    } else {
        logger->info("LIVE TRADING MODE - Real orders will be placed on Kraken");
        std::cout << "[!] LIVE TRADING MODE - Real orders on Kraken exchange" << std::endl;
    }

    // --- Set up signal handlers ---
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // --- Create core components ---
    auto rest_client = std::make_shared<trader::KrakenRestClient>(config.kraken, config.paper_trading);
    auto ws_client = std::make_shared<trader::KrakenWebSocketClient>(config.kraken);
    auto rate_limiter = std::make_shared<trader::RateLimiter>(config.rate_limit);
    auto risk_manager = std::make_shared<trader::RiskManager>(config.risk);

    // Wire up rate limiter to REST client
    rest_client->set_rate_limit_callback([&rate_limiter]() -> bool {
        return rate_limiter->allow_global_request();
    });

    // Create script manager
    trader::ScriptManager script_manager(rest_client, ws_client, config);
    script_manager.set_rate_limiter(rate_limiter);
    script_manager.set_risk_manager(risk_manager);

    // Wire up risk manager emergency stop
    risk_manager->set_emergency_callback([&script_manager]() {
        script_manager.emergency_stop();
        g_emergency = true;
    });

    // --- Verify connectivity (non-paper mode) ---
    if (!config.paper_trading) {
        logger->info("Testing API connectivity...");
        auto time_result = rest_client->get_server_time();
        if (!time_result.success) {
            logger->error("Failed to connect to Kraken API: {}", time_result.error);
            std::cerr << "[ERROR] Cannot reach Kraken API: " << time_result.error << std::endl;
            return 1;
        }
        logger->info("Kraken API connection verified");
        std::cout << "[OK] Kraken API connection verified" << std::endl;
    }

    // --- Load scripts ---
    int loaded = script_manager.load_all_scripts();
    if (loaded == 0) {
        logger->warn("No scripts loaded - nothing to run");
        std::cout << "[WARN] No scripts loaded. Check your configuration." << std::endl;
    } else {
        std::cout << "[OK] Loaded " << loaded << " script(s)" << std::endl;
    }

    // --- Connect WebSocket (optional, for real-time data) ---
    bool ws_connected = false;
    if (!config.paper_trading && !config.kraken.api_key.empty()) {
        try {
            ws_client->connect();
            ws_connected = true;
            logger->info("WebSocket connected for real-time data");
            std::cout << "[OK] WebSocket connected" << std::endl;
        } catch (const std::exception& e) {
            logger->warn("WebSocket connection failed (will use REST polling): {}", e.what());
            std::cout << "[WARN] WebSocket unavailable, using REST polling" << std::endl;
        }
    }

    // --- Start auto-start scripts ---
    int started = script_manager.start_auto_scripts();
    if (started > 0) {
        std::cout << "[OK] Auto-started " << started << " script(s)" << std::endl;
    }

    logger->info("Entering main loop (tick interval: {}ms)", config.tick_interval_ms);

    if (use_tui) {
        // --- TUI Mode ---
        // Register the ring-buffer log sink so output feeds into the TUI panel
        auto tui_sink = std::make_shared<trader::RingBufferSink_mt>(100);
        tui_sink->set_pattern("[%H:%M:%S] [%l] %v");
        logger->sinks().push_back(tui_sink);

        trader::TuiApp tui_app(script_manager, rate_limiter, risk_manager,
                               rest_client, ws_client, config, tui_sink);

        logger->info("TUI mode active");
        tui_app.run();
    } else {
        // --- Classic text-based main loop ---
        std::cout << "\n[RUNNING] Press Ctrl+C to stop gracefully (twice to force quit)\n" << std::endl;

        auto tick_duration = std::chrono::milliseconds(config.tick_interval_ms);
        int status_counter = 0;
        constexpr int STATUS_INTERVAL = 100;  // Print status every N ticks

        while (g_running) {
            auto tick_start = std::chrono::steady_clock::now();

            // Process WebSocket messages
            if (ws_connected && ws_client->is_connected()) {
                ws_client->poll();
            }

            // Tick all running scripts
            script_manager.tick_all();

            // Periodic status logging
            status_counter++;
            if (status_counter >= STATUS_INTERVAL) {
                status_counter = 0;

                // Log rate limiter stats
                logger->debug("Rate limiter: allowed={}, rejected={}",
                              rate_limiter->total_allowed(), rate_limiter->total_rejected());

                // Check risk status
                if (risk_manager->limits_breached()) {
                    logger->critical("Risk limits breached: {}", risk_manager->breach_description());
                }
            }

            // Check for emergency stop
            if (g_emergency) {
                logger->critical("Emergency stop triggered - shutting down");
                std::cerr << "\n[EMERGENCY] Risk limits breached - all scripts halted!" << std::endl;
                break;
            }

            // Sleep for remainder of tick interval
            auto elapsed = std::chrono::steady_clock::now() - tick_start;
            if (elapsed < tick_duration) {
                std::this_thread::sleep_for(tick_duration - elapsed);
            }
        }
    }

    // --- Shutdown ---
    std::cout << "\n[SHUTDOWN] Stopping all scripts..." << std::endl;
    logger->info("Shutdown initiated");

    script_manager.stop_all();

    // Print final status
    print_status(script_manager);

    // Disconnect WebSocket
    if (ws_connected) {
        ws_client->disconnect();
    }

    logger->info("Kraken Lua Trader shutdown complete");
    std::cout << "[DONE] Shutdown complete. Goodbye!" << std::endl;

    return g_emergency ? 2 : 0;
}

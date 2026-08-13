#include "tui/tui_app.hpp"

#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/color.hpp>

#include "scripting/script_manager.hpp"
#include "safety/rate_limiter.hpp"
#include "safety/risk_manager.hpp"
#include "kraken/rest_client.hpp"
#include "kraken/websocket_client.hpp"
#include "core/logger.hpp"

// External signal flag (defined in main.cpp with external linkage)
extern std::atomic<bool> g_running;

namespace trader {

TuiApp::TuiApp(ScriptManager& script_manager,
               std::shared_ptr<RateLimiter> rate_limiter,
               std::shared_ptr<RiskManager> risk_manager,
               std::shared_ptr<KrakenRestClient> rest_client,
               std::shared_ptr<KrakenWebSocketClient> ws_client,
               const AppConfig& config,
               std::shared_ptr<RingBufferSink_mt> log_sink)
    : script_manager_(script_manager)
    , rate_limiter_(std::move(rate_limiter))
    , risk_manager_(std::move(risk_manager))
    , rest_client_(std::move(rest_client))
    , ws_client_(std::move(ws_client))
    , config_(config)
    , log_sink_(std::move(log_sink))
    , start_time_(std::chrono::steady_clock::now())
{}

TuiApp::~TuiApp() {
    stop();
}

void TuiApp::stop() {
    running_ = false;
    g_running = false;
}

std::string TuiApp::format_uptime() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    int hours = static_cast<int>(elapsed.count() / 3600);
    int minutes = static_cast<int>((elapsed.count() % 3600) / 60);
    int seconds = static_cast<int>(elapsed.count() % 60);
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds;
    return oss.str();
}

std::string TuiApp::ws_state_string() const {
    switch (ws_client_->state()) {
        case WsState::Disconnected: return "Disconnected";
        case WsState::Connecting:   return "Connecting";
        case WsState::Connected:    return "Connected";
        case WsState::Closing:      return "Closing";
        case WsState::Error:        return "Error";
        default:                    return "Unknown";
    }
}

void TuiApp::run() {
    using namespace ftxui;

    running_ = true;
    auto screen = ScreenInteractive::Fullscreen();

    auto tick_duration = std::chrono::milliseconds(config_.tick_interval_ms);

    // Build the renderer component
    auto renderer = Renderer([&] {
        // --- Header bar ---
        std::string mode_str = rest_client_->is_paper_trading() ? "PAPER" : "LIVE";
        auto mode_color = rest_client_->is_paper_trading() ? Color::Yellow : Color::Red;
        std::string ws_str = ws_state_string();
        auto ws_color = ws_client_->is_connected() ? Color::Green : Color::Red;

        auto header = hbox({
            text(" Kraken Lua Trader ") | bold | color(Color::Cyan),
            separator(),
            text(" Mode: ") | dim,
            text(mode_str) | bold | color(mode_color),
            separator(),
            text(" Uptime: ") | dim,
            text(format_uptime()) | bold,
            separator(),
            text(" WS: ") | dim,
            text(ws_str) | bold | color(ws_color),
            filler(),
            text(" v1.0.0 ") | dim,
        }) | border;

        // --- Live Prices panel ---
        Elements price_rows;
        for (const auto& script_entry : config_.scripts) {
            // Try to get ticker data for pairs mentioned in scripts
            // We use the script parameters or default pair info
            // For now, iterate subscribed pairs from config
        }
        // Show ticker data for unique pairs from scripts
        std::vector<std::string> pairs_seen;
        for (const auto& s : config_.scripts) {
            for (const auto& param : s.parameters) {
                if (param.first == "pair" || param.first == "symbol") {
                    if (std::find(pairs_seen.begin(), pairs_seen.end(), param.second) == pairs_seen.end()) {
                        pairs_seen.push_back(param.second);
                    }
                }
            }
        }
        if (pairs_seen.empty()) {
            pairs_seen.push_back("XBT/USD");
            pairs_seen.push_back("ETH/USD");
        }

        for (const auto& pair : pairs_seen) {
            auto ticker = ws_client_->get_ticker(pair);
            std::string price_str = "---";
            if (!ticker.is_null() && ticker.contains("c") && ticker["c"].is_array() && !ticker["c"].empty()) {
                price_str = ticker["c"][0].get<std::string>();
            } else if (!ticker.is_null() && ticker.contains("last")) {
                price_str = ticker["last"].get<std::string>();
            }
            price_rows.push_back(hbox({
                text(" " + pair + ": ") | dim,
                text(price_str) | bold | color(Color::White),
            }));
        }
        if (price_rows.empty()) {
            price_rows.push_back(text(" No price data") | dim);
        }
        auto prices_panel = vbox(std::move(price_rows)) | border | flex;

        // --- Account / P&L panel ---
        double total_pnl = risk_manager_->total_pnl();
        auto pnl_color = total_pnl >= 0 ? Color::Green : Color::Red;
        std::ostringstream pnl_str;
        pnl_str << std::fixed << std::setprecision(4) << total_pnl;

        Elements pnl_rows;
        pnl_rows.push_back(hbox({
            text(" Total P&L: ") | dim,
            text(pnl_str.str()) | bold | color(pnl_color),
        }));

        // Per-script P&L
        auto script_infos = script_manager_.get_all_script_info();
        for (const auto& info : script_infos) {
            double spnl = risk_manager_->script_pnl(info.name);
            auto sc = spnl >= 0 ? Color::Green : Color::Red;
            std::ostringstream sp;
            sp << std::fixed << std::setprecision(4) << spnl;
            pnl_rows.push_back(hbox({
                text("   " + info.name + ": ") | dim,
                text(sp.str()) | color(sc),
            }));
        }

        pnl_rows.push_back(separator());
        pnl_rows.push_back(hbox({
            text(" Open Orders: ") | dim,
            text(std::to_string(risk_manager_->open_order_count())) | bold,
        }));

        auto pnl_panel = window(text(" Account / P&L "), vbox(std::move(pnl_rows)) | flex);

        // --- Scripts Table panel ---
        std::vector<std::vector<std::string>> table_data;
        table_data.push_back({"Name", "State", "Ticks", "Orders", "Cancelled", "API Calls", "Last Error"});
        for (const auto& info : script_infos) {
            table_data.push_back({
                info.name,
                script_state_to_string(info.state),
                std::to_string(info.tick_count),
                std::to_string(info.orders_placed),
                std::to_string(info.orders_cancelled),
                std::to_string(info.api_calls),
                info.last_error.empty() ? "-" : info.last_error.substr(0, 40),
            });
        }

        auto table = Table(table_data);
        table.SelectAll().Border(LIGHT);
        // Header row styling
        table.SelectRow(0).Decorate(bold);
        table.SelectRow(0).SeparatorVertical(LIGHT);

        auto scripts_panel = window(text(" Scripts "), table.Render() | flex);

        // --- Rate Limiter panel ---
        int allowed = rate_limiter_->total_allowed();
        int rejected = rate_limiter_->total_rejected();
        bool cooldown = rate_limiter_->in_cooldown();
        int cd_remaining = rate_limiter_->cooldown_remaining();

        Elements rl_rows;
        rl_rows.push_back(hbox({
            text(" Allowed: ") | dim,
            text(std::to_string(allowed)) | bold | color(Color::Green),
            text("  Rejected: ") | dim,
            text(std::to_string(rejected)) | bold | color(rejected > 0 ? Color::Red : Color::White),
        }));
        rl_rows.push_back(hbox({
            text(" Cooldown: ") | dim,
            text(cooldown ? "ACTIVE (" + std::to_string(cd_remaining) + "s)" : "None")
                | bold | color(cooldown ? Color::Yellow : Color::Green),
        }));

        auto rl_panel = window(text(" Rate Limiter "), vbox(std::move(rl_rows)));

        // --- Risk Manager panel ---
        bool breached = risk_manager_->limits_breached();
        Elements rm_rows;
        rm_rows.push_back(hbox({
            text(" Status: ") | dim,
            text(breached ? "LIMITS BREACHED" : "OK")
                | bold | color(breached ? Color::Red : Color::Green),
        }));
        if (breached) {
            rm_rows.push_back(hbox({
                text(" Reason: ") | dim,
                text(risk_manager_->breach_description()) | color(Color::Red),
            }));
        }
        rm_rows.push_back(hbox({
            text(" Max Position: ") | dim,
            text(std::to_string(static_cast<int>(config_.risk.max_position_size))) | bold,
            text("  Max Loss: ") | dim,
            text(std::to_string(static_cast<int>(config_.risk.max_total_loss))) | bold,
            text("  Max Orders: ") | dim,
            text(std::to_string(config_.risk.max_open_orders)) | bold,
        }));

        auto rm_panel = window(text(" Risk Manager "), vbox(std::move(rm_rows)));

        // --- Log panel ---
        auto messages = log_sink_->get_messages();
        Elements log_lines;
        // Show last 15 messages that fit
        size_t start_idx = messages.size() > 15 ? messages.size() - 15 : 0;
        for (size_t i = start_idx; i < messages.size(); ++i) {
            auto& msg = messages[i];
            auto line_color = Color::White;
            if (msg.find("[error]") != std::string::npos || msg.find("[critical]") != std::string::npos) {
                line_color = Color::Red;
            } else if (msg.find("[warning]") != std::string::npos || msg.find("[warn]") != std::string::npos) {
                line_color = Color::Yellow;
            } else if (msg.find("[debug]") != std::string::npos) {
                line_color = Color::GrayDark;
            }
            // Truncate long lines
            std::string display_msg = msg.size() > 120 ? msg.substr(0, 120) + "..." : msg;
            // Remove trailing newline if present
            if (!display_msg.empty() && display_msg.back() == '\n') {
                display_msg.pop_back();
            }
            log_lines.push_back(text(display_msg) | color(line_color));
        }
        if (log_lines.empty()) {
            log_lines.push_back(text(" No log messages yet") | dim);
        }

        auto log_panel = window(text(" Logs "), vbox(std::move(log_lines)) | flex);

        // --- Status bar (bottom) ---
        auto status_bar = hbox({
            text(" [q] Quit") | bold,
            text("  ") | dim,
            text("[p] Pause/Resume") | bold,
            text("  ") | dim,
            text("[r] Restart Scripts") | bold,
            filler(),
            text(paused_ ? " PAUSED " : "") | bold | color(Color::Yellow),
        }) | border;

        // --- Compose layout ---
        // Top: header
        // Middle: left column (prices + P&L + rate limiter + risk manager) | right column (scripts)
        // Bottom rows: logs, status bar

        auto left_column = vbox({
            window(text(" Live Prices "), vbox(price_rows)) | flex_shrink,
            pnl_panel | flex_shrink,
            rl_panel | flex_shrink,
            rm_panel | flex_shrink,
        });

        auto middle = hbox({
            left_column | size(WIDTH, EQUAL, 45),
            scripts_panel | flex,
        }) | flex;

        return vbox({
            header,
            middle,
            log_panel | size(HEIGHT, EQUAL, 18),
            status_bar,
        });
    });

    // Handle keyboard input
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Escape) {
            stop();
            screen.Exit();
            return true;
        }
        if (event == Event::Character('p')) {
            paused_ = !paused_;
            return true;
        }
        if (event == Event::Character('r')) {
            // Restart all scripts
            auto names = script_manager_.get_script_names();
            for (const auto& name : names) {
                script_manager_.restart_script(name);
            }
            return true;
        }
        // Ctrl+C is handled by signal handler setting g_running = false
        return false;
    });

    // Background thread for ticking scripts and refreshing UI
    std::atomic<bool> bg_running{true};
    std::thread bg_thread([&]() {
        while (bg_running && running_ && g_running) {
            auto tick_start = std::chrono::steady_clock::now();

            // Poll WebSocket
            if (ws_client_->is_connected()) {
                ws_client_->poll();
            }

            // Tick scripts (unless paused)
            if (!paused_) {
                script_manager_.tick_all();
            }

            // Post a custom event to trigger UI refresh
            screen.Post(Event::Custom);

            // Sleep for the tick interval
            auto elapsed = std::chrono::steady_clock::now() - tick_start;
            if (elapsed < tick_duration) {
                std::this_thread::sleep_for(tick_duration - elapsed);
            }
        }
    });

    // Also run a separate refresh thread at ~4Hz for smoother UI updates
    std::thread refresh_thread([&]() {
        while (bg_running && running_ && g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            screen.Post(Event::Custom);
        }
    });

    // Run the FTXUI loop (blocking)
    screen.Loop(component);

    // Cleanup
    bg_running = false;
    running_ = false;
    g_running = false;

    if (bg_thread.joinable()) {
        bg_thread.join();
    }
    if (refresh_thread.joinable()) {
        refresh_thread.join();
    }
}

}  // namespace trader

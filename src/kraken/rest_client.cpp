#include "kraken/rest_client.hpp"
#include "core/logger.hpp"

#include <curl/curl.h>
#include <sstream>
#include <stdexcept>

namespace trader {

namespace {

// CURL write callback
size_t write_callback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace

std::string order_type_to_string(OrderType type) {
    switch (type) {
        case OrderType::Market: return "market";
        case OrderType::Limit: return "limit";
        case OrderType::StopLoss: return "stop-loss";
        case OrderType::TakeProfit: return "take-profit";
        case OrderType::StopLossLimit: return "stop-loss-limit";
        case OrderType::TakeProfitLimit: return "take-profit-limit";
    }
    return "market";
}

std::string order_side_to_string(OrderSide side) {
    switch (side) {
        case OrderSide::Buy: return "buy";
        case OrderSide::Sell: return "sell";
    }
    return "buy";
}

KrakenRestClient::KrakenRestClient(const KrakenConfig& config, bool paper_trading)
    : config_(config),
      auth_(std::make_unique<KrakenAuth>(config.api_key, config.api_secret)),
      paper_trading_(paper_trading) {
    curl_global_init(CURL_GLOBAL_ALL);
}

KrakenRestClient::KrakenRestClient(const KrakenConfig& config, bool paper_trading, SkipCurlInit)
    : config_(config),
      auth_(std::make_unique<KrakenAuth>(config.api_key, config.api_secret)),
      paper_trading_(paper_trading),
      skip_curl_cleanup_(true) {
}

KrakenRestClient::~KrakenRestClient() {
    if (!skip_curl_cleanup_) {
        curl_global_cleanup();
    }
}

// ==================== Public API ====================

ApiResult KrakenRestClient::get_server_time() {
    return public_request("/0/public/Time");
}

ApiResult KrakenRestClient::get_asset_pairs(const std::string& pair) {
    std::string params;
    if (!pair.empty()) {
        params = "pair=" + pair;
    }
    return public_request("/0/public/AssetPairs", params);
}

ApiResult KrakenRestClient::get_ticker(const std::string& pair) {
    return public_request("/0/public/Ticker", "pair=" + pair);
}

ApiResult KrakenRestClient::get_ohlc(const std::string& pair, int interval, int64_t since) {
    std::string params = "pair=" + pair + "&interval=" + std::to_string(interval);
    if (since > 0) {
        params += "&since=" + std::to_string(since);
    }
    return public_request("/0/public/OHLC", params);
}

ApiResult KrakenRestClient::get_order_book(const std::string& pair, int count) {
    std::string params = "pair=" + pair + "&count=" + std::to_string(count);
    return public_request("/0/public/Depth", params);
}

ApiResult KrakenRestClient::get_recent_trades(const std::string& pair, int64_t since) {
    std::string params = "pair=" + pair;
    if (since > 0) {
        params += "&since=" + std::to_string(since);
    }
    return public_request("/0/public/Trades", params);
}

// ==================== Private API ====================

ApiResult KrakenRestClient::get_balance() {
    return private_request("/0/private/Balance");
}

ApiResult KrakenRestClient::get_trade_balance(const std::string& asset) {
    return private_request("/0/private/TradeBalance", "asset=" + asset);
}

ApiResult KrakenRestClient::get_open_orders() {
    return private_request("/0/private/OpenOrders");
}

ApiResult KrakenRestClient::get_closed_orders(int64_t start, int64_t end) {
    std::string params;
    if (start > 0) params += "start=" + std::to_string(start);
    if (end > 0) {
        if (!params.empty()) params += "&";
        params += "end=" + std::to_string(end);
    }
    return private_request("/0/private/ClosedOrders", params);
}

ApiResult KrakenRestClient::query_orders(const std::string& txid) {
    return private_request("/0/private/QueryOrders", "txid=" + txid);
}

ApiResult KrakenRestClient::place_order(const OrderRequest& order) {
    if (paper_trading_) {
        auto logger = get_logger();
        logger->info("[PAPER] Would place {} {} order: {} {} @ {}",
                     order_side_to_string(order.side),
                     order_type_to_string(order.type),
                     order.volume, order.pair,
                     order.type == OrderType::Market ? "market" : std::to_string(order.price));

        // Return a simulated successful response
        json simulated;
        simulated["descr"]["order"] = order_side_to_string(order.side) + " " +
                                       std::to_string(order.volume) + " " +
                                       order.pair + " @ " +
                                       order_type_to_string(order.type);
        simulated["txid"] = json::array({"PAPER-" + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count())});
        return ApiResult::ok(simulated);
    }

    std::ostringstream params;
    params << "pair=" << order.pair
           << "&type=" << order_side_to_string(order.side)
           << "&ordertype=" << order_type_to_string(order.type)
           << "&volume=" << order.volume;

    if (order.type != OrderType::Market && order.price > 0) {
        params << "&price=" << order.price;
    }
    if (order.price2 > 0) {
        params << "&price2=" << order.price2;
    }
    if (!order.leverage.empty()) {
        params << "&leverage=" << order.leverage;
    }
    if (order.validate_only) {
        params << "&validate=true";
    }
    if (!order.close_type.empty()) {
        params << "&close[ordertype]=" << order.close_type;
        if (order.close_price > 0) {
            params << "&close[price]=" << order.close_price;
        }
    }

    return private_request("/0/private/AddOrder", params.str());
}

ApiResult KrakenRestClient::cancel_order(const std::string& txid) {
    if (paper_trading_) {
        auto logger = get_logger();
        logger->info("[PAPER] Would cancel order: {}", txid);
        json simulated;
        simulated["count"] = 1;
        return ApiResult::ok(simulated);
    }
    return private_request("/0/private/CancelOrder", "txid=" + txid);
}

ApiResult KrakenRestClient::cancel_all_orders() {
    if (paper_trading_) {
        auto logger = get_logger();
        logger->info("[PAPER] Would cancel all orders");
        json simulated;
        simulated["count"] = 0;
        return ApiResult::ok(simulated);
    }
    return private_request("/0/private/CancelAll");
}

ApiResult KrakenRestClient::get_open_positions() {
    return private_request("/0/private/OpenPositions");
}

ApiResult KrakenRestClient::get_trades_history(int64_t start, int64_t end) {
    std::string params;
    if (start > 0) params += "start=" + std::to_string(start);
    if (end > 0) {
        if (!params.empty()) params += "&";
        params += "end=" + std::to_string(end);
    }
    return private_request("/0/private/TradesHistory", params);
}

ApiResult KrakenRestClient::get_ws_token() {
    return private_request("/0/private/GetWebSocketsToken");
}

// ==================== Configuration ====================

void KrakenRestClient::set_rate_limit_callback(RateLimitCallback callback) {
    rate_limit_callback_ = std::move(callback);
}

// ==================== Internal Methods ====================

ApiResult KrakenRestClient::public_request(const std::string& endpoint, const std::string& params) {
    if (!check_rate_limit()) {
        return ApiResult::fail("Rate limit exceeded");
    }

    std::string url = config_.rest_url + endpoint;
    std::string post_data = params;

    auto logger = get_logger();
    logger->debug("Public API request: {} params={}", endpoint, params);

    std::string response = http_post(url, post_data);
    return parse_response(response);
}

ApiResult KrakenRestClient::private_request(const std::string& endpoint, const std::string& extra_params) {
    if (!auth_->has_credentials()) {
        if (paper_trading_) {
            // In paper mode, return simulated empty data for private endpoints
            auto logger = get_logger();
            logger->debug("[PAPER] Simulating private request: {}", endpoint);
            return ApiResult::ok(json::object());
        }
        return ApiResult::fail("API credentials not configured");
    }

    if (!check_rate_limit()) {
        return ApiResult::fail("Rate limit exceeded");
    }

    std::string nonce = auth_->generate_nonce();
    std::string post_data = "nonce=" + nonce;
    if (!extra_params.empty()) {
        post_data += "&" + extra_params;
    }

    std::string signature = auth_->compute_signature(endpoint, nonce, post_data);

    std::string url = config_.rest_url + endpoint;

    std::vector<std::string> headers = {
        "API-Key: " + auth_->api_key(),
        "API-Sign: " + signature,
        "Content-Type: application/x-www-form-urlencoded"
    };

    auto logger = get_logger();
    logger->debug("Private API request: {}", endpoint);

    std::string response = http_post(url, post_data, headers);
    return parse_response(response);
}

std::string KrakenRestClient::http_post(const std::string& url,
                                         const std::string& post_data,
                                         const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response_body;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "KrakenLuaTrader/1.0");

    struct curl_slist* header_list = nullptr;
    for (const auto& header : headers) {
        header_list = curl_slist_append(header_list, header.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);

    if (header_list) {
        curl_slist_free_all(header_list);
    }

    if (res != CURLE_OK) {
        std::string error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw std::runtime_error("HTTP request failed: " + error);
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        throw std::runtime_error("HTTP error: " + std::to_string(http_code));
    }

    return response_body;
}

ApiResult KrakenRestClient::parse_response(const std::string& response_body) {
    auto logger = get_logger();

    try {
        json response = json::parse(response_body);

        // Kraken responses have {"error": [...], "result": {...}}
        auto errors = response.value("error", json::array());
        if (!errors.empty()) {
            std::string error_msg;
            for (const auto& err : errors) {
                if (!error_msg.empty()) error_msg += "; ";
                error_msg += err.get<std::string>();
            }
            logger->warn("Kraken API error: {}", error_msg);
            return ApiResult::fail(error_msg);
        }

        auto result = response.value("result", json::object());
        return ApiResult::ok(result);

    } catch (const json::exception& e) {
        logger->error("Failed to parse API response: {}", e.what());
        return ApiResult::fail(std::string("JSON parse error: ") + e.what());
    }
}

bool KrakenRestClient::check_rate_limit() {
    if (rate_limit_callback_) {
        return rate_limit_callback_();
    }
    return true;  // No rate limiter set = always allow
}

}  // namespace trader

-- Top Crypto Trader - Simple Percentage Change Strategy
-- ======================================================
-- Trades a single pair based on price momentum.
-- Buys when price rises > threshold% since last check.
-- Sells when price drops > threshold% since last check.
-- Logs activity on EVERY check so user can see the system is alive.
--
-- Parameters:
--   pair      - Trading pair (e.g., "XBT/USD")
--   threshold - Percentage change to trigger trade (e.g., 0.3 = 0.3%)
--   quantity  - Order size in base currency units

-- Strategy state
local state = {
    last_price = nil,
    position = "flat",  -- "flat", "long", "short"
    tick_count = 0,
    check_interval = 30,  -- Check price every 30 ticks (30 * 200ms = 6s)
    total_trades = 0,
}

function on_init()
    log.info(string.format("[%s] Top Crypto Trader initializing", params.pair))
    log.info(string.format("[%s]   Threshold: %.2f%%", params.pair, params.threshold))
    log.info(string.format("[%s]   Quantity: %s", params.pair, tostring(params.quantity)))
    log.info(string.format("[%s]   Check interval: every %d ticks", params.pair, state.check_interval))

    -- Subscribe to real-time ticker data
    trader.subscribe("ticker", { params.pair })

    -- Get initial price
    local result = trader.get_ticker(params.pair)
    if result.success and result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                state.last_price = tonumber(v.c[1])
                break
            end
        end
    end

    if state.last_price then
        log.info(string.format("[%s] Initial price: %.4f", params.pair, state.last_price))
    else
        log.warn(string.format("[%s] Could not get initial price - will retry on first tick", params.pair))
    end
end

function on_tick()
    state.tick_count = state.tick_count + 1

    -- Only check price at the configured interval
    if state.tick_count % state.check_interval ~= 0 then
        return
    end

    -- Fetch current price
    local result = trader.get_ticker(params.pair)
    if not result.success then
        log.warn(string.format("[%s] Tick #%d: Rate limited or API error: %s",
                               params.pair, state.tick_count, result.error or "unknown"))
        return
    end

    -- Extract current price from ticker data
    local current_price = nil
    if result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                current_price = tonumber(v.c[1])
                break
            end
        end
    end

    if not current_price then
        log.warn(string.format("[%s] Tick #%d: No price data available in response",
                               params.pair, state.tick_count))
        return
    end

    -- First price - just record it and log
    if not state.last_price then
        state.last_price = current_price
        log.info(string.format("[%s] First price recorded: %.4f", params.pair, current_price))
        return
    end

    -- Calculate percentage change
    local pct_change = ((current_price - state.last_price) / state.last_price) * 100

    -- ALWAYS log current status so user can see activity
    log.info(string.format("[%s] Price: %.4f | Change: %+.4f%% | Position: %s | Trades: %d",
                           params.pair, current_price, pct_change, state.position, state.total_trades))

    -- Trading logic
    if pct_change >= params.threshold and state.position ~= "long" then
        -- Price went up - buy signal
        local order = trader.market_order(params.pair, "buy", params.quantity)
        if order.success then
            state.position = "long"
            state.total_trades = state.total_trades + 1
            log.info(string.format("[TRADE] BUY %s @ %.4f (up %.4f%%)",
                                   params.pair, current_price, pct_change))
        else
            log.error(string.format("[%s] BUY order failed: %s",
                                    params.pair, order.error or "unknown"))
        end

    elseif pct_change <= -params.threshold and state.position ~= "short" then
        -- Price went down - sell signal
        local order = trader.market_order(params.pair, "sell", params.quantity)
        if order.success then
            state.position = "short"
            state.total_trades = state.total_trades + 1
            log.info(string.format("[TRADE] SELL %s @ %.4f (down %.4f%%)",
                                   params.pair, current_price, pct_change))
        else
            log.error(string.format("[%s] SELL order failed: %s",
                                    params.pair, order.error or "unknown"))
        end
    end

    -- Update last price reference for next comparison
    state.last_price = current_price
end

function on_data(channel, pair, data)
    -- Handle real-time ticker updates from WebSocket
    if channel == "ticker" and pair == params.pair then
        if data and data.c then
            local ws_price = tonumber(data.c[1])
            if ws_price then
                state.last_price = ws_price
            end
        end
    end
end

function on_stop()
    log.info(string.format("[%s] Top Crypto Trader stopping (ticks: %d, trades: %d, position: %s)",
                           params.pair, state.tick_count, state.total_trades, state.position))

    -- Close position on stop
    if state.position ~= "flat" then
        log.info(string.format("[%s] Closing position...", params.pair))
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
    end

    trader.unsubscribe("ticker", { params.pair })
end

-- Simple Momentum Trading Strategy
-- =================================
-- Buys when price increases by a threshold percentage within a short window.
-- Sells when price decreases by the same threshold.
--
-- Parameters:
--   pair      - Trading pair (e.g., "XBTUSD")
--   threshold - Percentage change to trigger trade (e.g., 0.5 = 0.5%)
--   quantity  - Order size in base currency units

-- Strategy state
local state = {
    last_price = nil,
    position = "flat",  -- "flat", "long", "short"
    tick_count = 0,
    check_interval = 50,  -- Check price every N ticks (50 * 200ms = 10s)
}

function on_init()
    log.info("Simple Momentum strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Threshold: %.2f%%", params.threshold))
    log.info(string.format("  Quantity: %.6f", params.quantity))

    -- Subscribe to real-time ticker data
    trader.subscribe("ticker", { params.pair })

    -- Get initial price
    local result = trader.get_ticker(params.pair)
    if result.success and result.data then
        -- Extract last trade price from ticker data
        -- Kraken ticker format varies, try to extract price
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                state.last_price = tonumber(v.c[1])
                break
            end
        end
    end

    if state.last_price then
        log.info(string.format("  Initial price: %.2f", state.last_price))
    else
        log.warn("  Could not get initial price, will wait for first tick")
    end
end

function on_tick()
    state.tick_count = state.tick_count + 1

    -- Only check price periodically to avoid rate limiting
    if state.tick_count % state.check_interval ~= 0 then
        return
    end

    -- Fetch current price
    local result = trader.get_ticker(params.pair)
    if not result.success then
        log.debug("Failed to get ticker: " .. (result.error or "unknown"))
        return
    end

    -- Extract current price
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
        return
    end

    -- First tick with price - just record it
    if not state.last_price then
        state.last_price = current_price
        log.info(string.format("First price recorded: %.2f", current_price))
        return
    end

    -- Calculate percentage change
    local pct_change = ((current_price - state.last_price) / state.last_price) * 100

    -- Log price periodically
    if state.tick_count % (state.check_interval * 10) == 0 then
        log.info(string.format("Price: %.2f (change: %.3f%%)", current_price, pct_change))
    end

    -- Trading logic
    if pct_change >= params.threshold and state.position ~= "long" then
        -- Momentum up - buy signal
        log.info(string.format("BUY signal: price up %.3f%% (%.2f -> %.2f)",
                               pct_change, state.last_price, current_price))

        local order = trader.market_order(params.pair, "buy", params.quantity)
        if order.success then
            log.info("BUY order placed successfully")
            state.position = "long"
        else
            log.error("BUY order failed: " .. (order.error or "unknown"))
        end

    elseif pct_change <= -params.threshold and state.position ~= "short" then
        -- Momentum down - sell signal
        log.info(string.format("SELL signal: price down %.3f%% (%.2f -> %.2f)",
                               pct_change, state.last_price, current_price))

        local order = trader.market_order(params.pair, "sell", params.quantity)
        if order.success then
            log.info("SELL order placed successfully")
            state.position = "short"
        else
            log.error("SELL order failed: " .. (order.error or "unknown"))
        end
    end

    -- Update last price reference
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
    log.info(string.format("Simple Momentum stopping (ticked %d times, position: %s)",
                           state.tick_count, state.position))

    -- Cancel any open orders on stop
    if state.position ~= "flat" then
        log.info("Closing position via market order...")
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
    end

    trader.unsubscribe("ticker", { params.pair })
end

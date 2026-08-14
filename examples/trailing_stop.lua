-- Trailing Stop Strategy
-- ======================
-- Trend-following strategy that enters on momentum and uses a trailing
-- stop to lock in profits. The stop dynamically adjusts as price moves
-- in the favorable direction, never moving backward.
--
-- Parameters:
--   pair           - Trading pair (e.g., "XBTUSD")
--   quantity       - Order size in base currency units
--   entry_pct      - Percentage move from recent low/high to enter (e.g., 1.0)
--   trail_pct      - Trailing stop percentage distance (e.g., 1.5)
--   lookback_ticks - Ticks to determine recent extremes (e.g., 100)

-- Strategy state
local state = {
    tick_count = 0,
    position = "flat",
    entry_price = 0,
    stop_price = 0,
    peak_price = 0,       -- Highest price since long entry
    trough_price = 0,     -- Lowest price since short entry
    recent_prices = {},
    total_trades = 0,
    winning_trades = 0,
    total_pnl = 0,
    sample_interval = 5,
}

-- Find min/max in recent prices
local function find_extremes(prices)
    if #prices == 0 then return nil, nil end
    local min_val = prices[1]
    local max_val = prices[1]
    for i = 2, #prices do
        if prices[i] < min_val then min_val = prices[i] end
        if prices[i] > max_val then max_val = prices[i] end
    end
    return min_val, max_val
end

function on_init()
    log.info("Trailing Stop strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Quantity: %.6f", params.quantity))
    log.info(string.format("  Entry threshold: %.2f%%", params.entry_pct))
    log.info(string.format("  Trailing stop: %.2f%%", params.trail_pct))
    log.info(string.format("  Lookback: %d ticks", params.lookback_ticks))

    trader.subscribe("ticker", { params.pair })

    -- Pre-fill some price history
    local result = trader.get_ohlc(params.pair, 1)
    if result.success and result.data then
        for _, candle_data in pairs(result.data) do
            if type(candle_data) == "table" then
                for _, candle in ipairs(candle_data) do
                    if type(candle) == "table" and #candle >= 5 then
                        local close = tonumber(candle[5])
                        if close then
                            table.insert(state.recent_prices, close)
                        end
                    end
                end
            end
        end

        while #state.recent_prices > params.lookback_ticks do
            table.remove(state.recent_prices, 1)
        end

        log.info(string.format("  Pre-loaded %d price points", #state.recent_prices))
    end
end

function on_tick()
    state.tick_count = state.tick_count + 1

    if state.tick_count % state.sample_interval ~= 0 then
        return
    end

    -- Get current price
    local result = trader.get_ticker(params.pair)
    if not result.success then
        return
    end

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

    -- Update recent prices
    table.insert(state.recent_prices, current_price)
    while #state.recent_prices > params.lookback_ticks do
        table.remove(state.recent_prices, 1)
    end

    -- Need full lookback before trading
    if #state.recent_prices < params.lookback_ticks then
        return
    end

    local recent_low, recent_high = find_extremes(state.recent_prices)

    -- Manage open position with trailing stop
    if state.position == "long" then
        -- Update peak and trailing stop
        if current_price > state.peak_price then
            state.peak_price = current_price
            state.stop_price = current_price * (1 - params.trail_pct / 100)
        end

        -- Check if stop was hit
        if current_price <= state.stop_price then
            local pnl = current_price - state.entry_price
            local order = trader.market_order(params.pair, "sell", params.quantity)
            if order.success then
                state.total_pnl = state.total_pnl + pnl
                state.total_trades = state.total_trades + 1
                if pnl > 0 then state.winning_trades = state.winning_trades + 1 end
                log.info(string.format("TRAILING STOP hit (long): exit %.2f | entry %.2f | peak %.2f | PnL: %.4f",
                                       current_price, state.entry_price, state.peak_price, pnl))
                state.position = "flat"
            end
        end

        return

    elseif state.position == "short" then
        -- Update trough and trailing stop
        if current_price < state.trough_price then
            state.trough_price = current_price
            state.stop_price = current_price * (1 + params.trail_pct / 100)
        end

        -- Check if stop was hit
        if current_price >= state.stop_price then
            local pnl = state.entry_price - current_price
            local order = trader.market_order(params.pair, "buy", params.quantity)
            if order.success then
                state.total_pnl = state.total_pnl + pnl
                state.total_trades = state.total_trades + 1
                if pnl > 0 then state.winning_trades = state.winning_trades + 1 end
                log.info(string.format("TRAILING STOP hit (short): exit %.2f | entry %.2f | trough %.2f | PnL: %.4f",
                                       current_price, state.entry_price, state.trough_price, pnl))
                state.position = "flat"
            end
        end

        return
    end

    -- Entry logic: look for breakout from recent range
    if not recent_low or not recent_high then
        return
    end

    local range = recent_high - recent_low
    if range <= 0 then
        return
    end

    -- Long entry: price breaks above recent high by entry_pct
    local long_trigger = recent_high * (1 + params.entry_pct / 100)
    if current_price >= long_trigger then
        local order = trader.market_order(params.pair, "buy", params.quantity)
        if order.success then
            state.position = "long"
            state.entry_price = current_price
            state.peak_price = current_price
            state.stop_price = current_price * (1 - params.trail_pct / 100)
            log.info(string.format("LONG entry at %.2f (broke above %.2f) | stop: %.2f",
                                   current_price, recent_high, state.stop_price))
        end
        return
    end

    -- Short entry: price breaks below recent low by entry_pct
    local short_trigger = recent_low * (1 - params.entry_pct / 100)
    if current_price <= short_trigger then
        local order = trader.market_order(params.pair, "sell", params.quantity)
        if order.success then
            state.position = "short"
            state.entry_price = current_price
            state.trough_price = current_price
            state.stop_price = current_price * (1 + params.trail_pct / 100)
            log.info(string.format("SHORT entry at %.2f (broke below %.2f) | stop: %.2f",
                                   current_price, recent_low, state.stop_price))
        end
        return
    end

    -- Periodic status
    if state.tick_count % (state.sample_interval * 100) == 0 then
        local win_rate = 0
        if state.total_trades > 0 then
            win_rate = (state.winning_trades / state.total_trades) * 100
        end
        log.info(string.format("Price: %.2f | Range: [%.2f-%.2f] | Trades: %d (%.0f%% win) | PnL: %.4f",
                               current_price, recent_low, recent_high,
                               state.total_trades, win_rate, state.total_pnl))
    end
end

function on_data(channel, pair, data)
    if channel == "ticker" and pair == params.pair then
        if data and data.c then
            local price = tonumber(data.c[1])
            if price then
                -- Update trailing stop in real-time for faster reaction
                if state.position == "long" and price > state.peak_price then
                    state.peak_price = price
                    state.stop_price = price * (1 - params.trail_pct / 100)
                elseif state.position == "short" and price < state.trough_price then
                    state.trough_price = price
                    state.stop_price = price * (1 + params.trail_pct / 100)
                end
            end
        end
    end
end

function on_stop()
    local win_rate = 0
    if state.total_trades > 0 then
        win_rate = (state.winning_trades / state.total_trades) * 100
    end

    log.info("=== Trailing Stop Summary ===")
    log.info(string.format("  Total trades: %d", state.total_trades))
    log.info(string.format("  Win rate: %.1f%%", win_rate))
    log.info(string.format("  Total PnL: %.4f", state.total_pnl))

    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info("Flattened position on stop")
    end

    trader.unsubscribe("ticker", { params.pair })
end

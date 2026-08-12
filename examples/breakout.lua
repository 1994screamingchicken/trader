-- Breakout Trading Strategy
-- =========================
-- Monitors price within a consolidation range and enters a trade when
-- price breaks above resistance or below support. Uses recent highs
-- and lows to define the range dynamically.
--
-- Parameters:
--   pair          - Trading pair (e.g., "XBTUSD")
--   lookback      - Number of candles to define the range (e.g., 30)
--   breakout_pct  - Percentage beyond range to confirm breakout (e.g., 0.2)
--   quantity      - Order size in base currency units
--   stop_loss_pct - Stop loss percentage from entry (e.g., 1.0)

-- Strategy state
local state = {
    highs = {},
    lows = {},
    position = "flat",
    entry_price = 0,
    tick_count = 0,
    sample_interval = 10,
    total_trades = 0,
    wins = 0,
    losses = 0,
}

-- Find the highest value in a table
local function find_max(values)
    if #values == 0 then return nil end
    local max_val = values[1]
    for i = 2, #values do
        if values[i] > max_val then
            max_val = values[i]
        end
    end
    return max_val
end

-- Find the lowest value in a table
local function find_min(values)
    if #values == 0 then return nil end
    local min_val = values[1]
    for i = 2, #values do
        if values[i] < min_val then
            min_val = values[i]
        end
    end
    return min_val
end

function on_init()
    log.info("Breakout strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Lookback: %d candles", params.lookback))
    log.info(string.format("  Breakout threshold: %.2f%%", params.breakout_pct))
    log.info(string.format("  Quantity: %.6f", params.quantity))
    log.info(string.format("  Stop loss: %.2f%%", params.stop_loss_pct))

    trader.subscribe("ticker", { params.pair })

    -- Pre-load range from OHLC data
    local result = trader.get_ohlc(params.pair, 5)  -- 5-minute candles
    if result.success and result.data then
        for _, candle_data in pairs(result.data) do
            if type(candle_data) == "table" then
                for _, candle in ipairs(candle_data) do
                    if type(candle) == "table" and #candle >= 5 then
                        local high = tonumber(candle[3])
                        local low = tonumber(candle[4])
                        if high and low then
                            table.insert(state.highs, high)
                            table.insert(state.lows, low)
                        end
                    end
                end
            end
        end

        -- Keep only lookback period
        while #state.highs > params.lookback do
            table.remove(state.highs, 1)
            table.remove(state.lows, 1)
        end

        log.info(string.format("  Pre-loaded %d candles for range calculation", #state.highs))
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
    local current_high = nil
    local current_low = nil

    if result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                current_price = tonumber(v.c[1])
                if v.h then current_high = tonumber(v.h[1]) end
                if v.l then current_low = tonumber(v.l[1]) end
                break
            end
        end
    end

    if not current_price then
        return
    end

    -- Update highs and lows
    if current_high and current_low then
        table.insert(state.highs, current_high)
        table.insert(state.lows, current_low)

        while #state.highs > params.lookback do
            table.remove(state.highs, 1)
            table.remove(state.lows, 1)
        end
    end

    -- Need enough data to define a range
    if #state.highs < params.lookback then
        return
    end

    -- Calculate range boundaries (excluding most recent candle)
    local resistance = find_max(state.highs)
    local support = find_min(state.lows)

    if not resistance or not support or resistance == support then
        return
    end

    local range_size = resistance - support
    local breakout_threshold = range_size * (params.breakout_pct / 100)

    -- Check stop loss on open positions
    if state.position == "long" then
        local stop_price = state.entry_price * (1 - params.stop_loss_pct / 100)
        if current_price <= stop_price then
            log.warn(string.format("STOP LOSS hit: price %.2f <= stop %.2f", current_price, stop_price))
            local order = trader.market_order(params.pair, "sell", params.quantity)
            if order.success then
                state.position = "flat"
                state.losses = state.losses + 1
                log.info(string.format("Closed LONG at stop (loss: %.2f)", current_price - state.entry_price))
            end
            return
        end
    elseif state.position == "short" then
        local stop_price = state.entry_price * (1 + params.stop_loss_pct / 100)
        if current_price >= stop_price then
            log.warn(string.format("STOP LOSS hit: price %.2f >= stop %.2f", current_price, stop_price))
            local order = trader.market_order(params.pair, "buy", params.quantity)
            if order.success then
                state.position = "flat"
                state.losses = state.losses + 1
                log.info(string.format("Closed SHORT at stop (loss: %.2f)", state.entry_price - current_price))
            end
            return
        end
    end

    -- Breakout detection
    if current_price > resistance + breakout_threshold and state.position ~= "long" then
        log.info(string.format("BREAKOUT UP: price %.2f > resistance %.2f + %.2f",
                               current_price, resistance, breakout_threshold))

        -- Close short if open
        if state.position == "short" then
            trader.market_order(params.pair, "buy", params.quantity)
            local pnl = state.entry_price - current_price
            if pnl > 0 then state.wins = state.wins + 1 else state.losses = state.losses + 1 end
        end

        local order = trader.market_order(params.pair, "buy", params.quantity)
        if order.success then
            state.position = "long"
            state.entry_price = current_price
            state.total_trades = state.total_trades + 1
            log.info(string.format("Opened LONG at %.2f (trade #%d)", current_price, state.total_trades))
        end

    elseif current_price < support - breakout_threshold and state.position ~= "short" then
        log.info(string.format("BREAKOUT DOWN: price %.2f < support %.2f - %.2f",
                               current_price, support, breakout_threshold))

        -- Close long if open
        if state.position == "long" then
            trader.market_order(params.pair, "sell", params.quantity)
            local pnl = current_price - state.entry_price
            if pnl > 0 then state.wins = state.wins + 1 else state.losses = state.losses + 1 end
        end

        local order = trader.market_order(params.pair, "sell", params.quantity)
        if order.success then
            state.position = "short"
            state.entry_price = current_price
            state.total_trades = state.total_trades + 1
            log.info(string.format("Opened SHORT at %.2f (trade #%d)", current_price, state.total_trades))
        end
    end

    -- Periodic status
    if state.tick_count % (state.sample_interval * 50) == 0 then
        log.info(string.format("Range: [%.2f - %.2f] | Price: %.2f | Pos: %s | W/L: %d/%d",
                               support, resistance, current_price, state.position, state.wins, state.losses))
    end
end

function on_data(channel, pair, data)
    if channel == "ticker" and pair == params.pair then
        if data and data.h and data.l then
            local h = tonumber(data.h[1])
            local l = tonumber(data.l[1])
            if h and l and #state.highs > 0 then
                state.highs[#state.highs] = math.max(state.highs[#state.highs], h)
                state.lows[#state.lows] = math.min(state.lows[#state.lows], l)
            end
        end
    end
end

function on_stop()
    log.info(string.format("Breakout strategy stopping (trades: %d, W/L: %d/%d)",
                           state.total_trades, state.wins, state.losses))

    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info("Flattened position on stop")
    end

    trader.unsubscribe("ticker", { params.pair })
end

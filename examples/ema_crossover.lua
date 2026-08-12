-- EMA Crossover Strategy
-- =======================
-- Uses two Exponential Moving Averages (fast and slow). Buys when the
-- fast EMA crosses above the slow EMA (golden cross) and sells when
-- the fast EMA crosses below (death cross). A classic trend-following
-- signal used across all timeframes.
--
-- Parameters:
--   pair        - Trading pair (e.g., "XBTUSD")
--   fast_period - Fast EMA period (e.g., 9)
--   slow_period - Slow EMA period (e.g., 21)
--   quantity    - Order size in base currency units

-- Strategy state
local state = {
    fast_ema = nil,
    slow_ema = nil,
    fast_multiplier = 0,
    slow_multiplier = 0,
    position = "flat",
    entry_price = 0,
    tick_count = 0,
    sample_interval = 10,
    price_count = 0,       -- Number of prices seen
    total_trades = 0,
    total_pnl = 0,
    last_signal = "none",  -- "bullish" or "bearish"
}

function on_init()
    log.info("EMA Crossover strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Fast EMA: %d periods", params.fast_period))
    log.info(string.format("  Slow EMA: %d periods", params.slow_period))
    log.info(string.format("  Quantity: %.6f", params.quantity))

    -- EMA multipliers: 2 / (period + 1)
    state.fast_multiplier = 2 / (params.fast_period + 1)
    state.slow_multiplier = 2 / (params.slow_period + 1)

    trader.subscribe("ticker", { params.pair })

    -- Initialize EMAs from OHLC data
    local result = trader.get_ohlc(params.pair, 1)  -- 1-minute candles
    if result.success and result.data then
        local prices = {}
        for _, candle_data in pairs(result.data) do
            if type(candle_data) == "table" then
                for _, candle in ipairs(candle_data) do
                    if type(candle) == "table" and #candle >= 5 then
                        local close = tonumber(candle[5])
                        if close then
                            table.insert(prices, close)
                        end
                    end
                end
            end
        end

        -- Seed EMAs using SMA of first N prices, then apply EMA formula
        if #prices >= params.slow_period then
            -- Calculate initial SMA for slow period
            local sum_fast = 0
            local sum_slow = 0
            for i = 1, params.slow_period do
                sum_slow = sum_slow + prices[i]
                if i <= params.fast_period then
                    sum_fast = sum_fast + prices[i]
                end
            end

            state.slow_ema = sum_slow / params.slow_period
            state.fast_ema = sum_fast / params.fast_period

            -- Apply EMA formula for remaining prices
            for i = params.slow_period + 1, #prices do
                state.fast_ema = (prices[i] - state.fast_ema) * state.fast_multiplier + state.fast_ema
                state.slow_ema = (prices[i] - state.slow_ema) * state.slow_multiplier + state.slow_ema
            end

            state.price_count = #prices

            -- Determine current signal state
            if state.fast_ema > state.slow_ema then
                state.last_signal = "bullish"
            else
                state.last_signal = "bearish"
            end

            log.info(string.format("  Initialized from %d candles", #prices))
            log.info(string.format("  Fast EMA: %.2f | Slow EMA: %.2f | Signal: %s",
                                   state.fast_ema, state.slow_ema, state.last_signal))
        else
            log.warn(string.format("  Only %d candles available, need %d. Building from live data.",
                                   #prices, params.slow_period))
        end
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

    state.price_count = state.price_count + 1

    -- Initialize EMAs if not yet set (use first price as seed)
    if not state.fast_ema then
        state.fast_ema = current_price
        state.slow_ema = current_price
        return
    end

    -- Update EMAs
    state.fast_ema = (current_price - state.fast_ema) * state.fast_multiplier + state.fast_ema
    state.slow_ema = (current_price - state.slow_ema) * state.slow_multiplier + state.slow_ema

    -- Need enough data before trading
    if state.price_count < params.slow_period then
        if state.price_count % 50 == 0 then
            log.debug(string.format("Building EMA history: %d/%d", state.price_count, params.slow_period))
        end
        return
    end

    -- Detect crossover
    local current_signal = "none"
    if state.fast_ema > state.slow_ema then
        current_signal = "bullish"
    elseif state.fast_ema < state.slow_ema then
        current_signal = "bearish"
    end

    -- Periodic logging
    if state.tick_count % (state.sample_interval * 30) == 0 then
        log.info(string.format("Price: %.2f | Fast EMA: %.2f | Slow EMA: %.2f | Signal: %s | Pos: %s",
                               current_price, state.fast_ema, state.slow_ema,
                               current_signal, state.position))
    end

    -- Check for crossover (signal change)
    if current_signal == state.last_signal or current_signal == "none" then
        state.last_signal = current_signal
        return
    end

    -- Golden cross: fast crosses above slow
    if current_signal == "bullish" and state.last_signal == "bearish" then
        log.info(string.format("GOLDEN CROSS: fast EMA (%.2f) crossed above slow EMA (%.2f) at price %.2f",
                               state.fast_ema, state.slow_ema, current_price))

        -- Close short if open
        if state.position == "short" then
            local close = trader.market_order(params.pair, "buy", params.quantity)
            if close.success then
                local pnl = state.entry_price - current_price
                state.total_pnl = state.total_pnl + pnl
                log.info(string.format("Closed SHORT: PnL per unit = %.4f", pnl))
            end
        end

        -- Open long
        local order = trader.market_order(params.pair, "buy", params.quantity)
        if order.success then
            state.position = "long"
            state.entry_price = current_price
            state.total_trades = state.total_trades + 1
            log.info(string.format("Opened LONG at %.2f (trade #%d)", current_price, state.total_trades))
        else
            log.error("Buy order failed: " .. (order.error or "unknown"))
        end

    -- Death cross: fast crosses below slow
    elseif current_signal == "bearish" and state.last_signal == "bullish" then
        log.info(string.format("DEATH CROSS: fast EMA (%.2f) crossed below slow EMA (%.2f) at price %.2f",
                               state.fast_ema, state.slow_ema, current_price))

        -- Close long if open
        if state.position == "long" then
            local close = trader.market_order(params.pair, "sell", params.quantity)
            if close.success then
                local pnl = current_price - state.entry_price
                state.total_pnl = state.total_pnl + pnl
                log.info(string.format("Closed LONG: PnL per unit = %.4f", pnl))
            end
        end

        -- Open short
        local order = trader.market_order(params.pair, "sell", params.quantity)
        if order.success then
            state.position = "short"
            state.entry_price = current_price
            state.total_trades = state.total_trades + 1
            log.info(string.format("Opened SHORT at %.2f (trade #%d)", current_price, state.total_trades))
        else
            log.error("Sell order failed: " .. (order.error or "unknown"))
        end
    end

    state.last_signal = current_signal
end

function on_data(channel, pair, data)
    if channel == "ticker" and pair == params.pair then
        if data and data.c then
            local price = tonumber(data.c[1])
            if price and state.fast_ema then
                -- Update EMAs with real-time data
                state.fast_ema = (price - state.fast_ema) * state.fast_multiplier + state.fast_ema
                state.slow_ema = (price - state.slow_ema) * state.slow_multiplier + state.slow_ema
            end
        end
    end
end

function on_stop()
    log.info("=== EMA Crossover Summary ===")
    log.info(string.format("  Total trades: %d", state.total_trades))
    log.info(string.format("  Total PnL (per unit): %.4f", state.total_pnl))
    if state.fast_ema and state.slow_ema then
        log.info(string.format("  Final Fast EMA: %.2f | Slow EMA: %.2f", state.fast_ema, state.slow_ema))
    end

    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info("Flattened position on stop")
    end

    trader.unsubscribe("ticker", { params.pair })
end

-- Mean Reversion Trading Strategy
-- ================================
-- Uses a simple moving average (SMA) and buys when price deviates below
-- the mean by N standard deviations, sells when it deviates above.
--
-- Parameters:
--   pair      - Trading pair (e.g., "ETHUSD")
--   lookback  - Number of price samples for SMA (e.g., 20)
--   deviation - Standard deviations for signal (e.g., 2.0)
--   quantity  - Order size in base currency units

-- Strategy state
local state = {
    prices = {},           -- Rolling price history
    position = "flat",     -- "flat", "long", "short"
    tick_count = 0,
    sample_interval = 100,  -- Sample price every N ticks (100 * 200ms = 20s)
    entry_price = 0,
    total_trades = 0,
}

-- Calculate mean of a table of numbers
local function mean(values)
    if #values == 0 then return 0 end
    local sum = 0
    for _, v in ipairs(values) do
        sum = sum + v
    end
    return sum / #values
end

-- Calculate standard deviation
local function stddev(values, avg)
    if #values < 2 then return 0 end
    local sum_sq = 0
    for _, v in ipairs(values) do
        local diff = v - avg
        sum_sq = sum_sq + diff * diff
    end
    return math.sqrt(sum_sq / (#values - 1))
end

function on_init()
    log.info("Mean Reversion strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Lookback: %d samples", params.lookback))
    log.info(string.format("  Deviation: %.1f sigma", params.deviation))
    log.info(string.format("  Quantity: %.6f", params.quantity))

    -- Subscribe to ticker for real-time data
    trader.subscribe("ticker", { params.pair })

    -- Pre-load price history from OHLC data
    local result = trader.get_ohlc(params.pair, 1)  -- 1-minute candles
    if result.success and result.data then
        for _, candle_data in pairs(result.data) do
            if type(candle_data) == "table" then
                -- OHLC data: each candle is [time, open, high, low, close, vwap, volume, count]
                for _, candle in ipairs(candle_data) do
                    if type(candle) == "table" and #candle >= 5 then
                        local close = tonumber(candle[5])
                        if close then
                            table.insert(state.prices, close)
                        end
                    end
                end
            end
        end

        -- Keep only the lookback period
        while #state.prices > params.lookback do
            table.remove(state.prices, 1)
        end

        log.info(string.format("  Pre-loaded %d price samples from OHLC", #state.prices))
    else
        log.warn("  Could not pre-load OHLC data, building history from ticks")
    end
end

function on_tick()
    state.tick_count = state.tick_count + 1

    -- Sample price at intervals
    if state.tick_count % state.sample_interval ~= 0 then
        return
    end

    -- Get current price
    local result = trader.get_ticker(params.pair)
    if not result.success then
        log.debug("Failed to get ticker for " .. params.pair .. ": " .. (result.error or "unknown"))
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

    -- Add to price history
    table.insert(state.prices, current_price)

    -- Trim to lookback window
    while #state.prices > params.lookback do
        table.remove(state.prices, 1)
    end

    -- Need full lookback window before trading
    if #state.prices < params.lookback then
        if state.tick_count % (state.sample_interval * 20) == 0 then
            log.debug(string.format("Building history: %d/%d samples",
                                    #state.prices, params.lookback))
        end
        return
    end

    -- Calculate statistics
    local sma = mean(state.prices)
    local sd = stddev(state.prices, sma)

    if sd == 0 then
        return  -- No variance, skip
    end

    -- Calculate z-score (how many SDs away from mean)
    local z_score = (current_price - sma) / sd

    -- Periodic logging
    if state.tick_count % (state.sample_interval * 10) == 0 then
        log.info(string.format("Price: %.2f | SMA: %.2f | SD: %.2f | Z: %.2f | Pos: %s",
                               current_price, sma, sd, z_score, state.position))
    end

    -- Trading signals
    if z_score <= -params.deviation and state.position ~= "long" then
        -- Price is significantly below mean - expect reversion up
        log.info(string.format("BUY signal: Z=%.2f (price %.2f below SMA %.2f)",
                               z_score, current_price, sma))

        -- Close short position first if needed
        if state.position == "short" then
            local close = trader.market_order(params.pair, "buy", params.quantity)
            if close.success then
                local pnl = state.entry_price - current_price
                log.info(string.format("Closed SHORT at %.2f (entry: %.2f, P&L: %.2f)",
                                       current_price, state.entry_price, pnl * params.quantity))
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

    elseif z_score >= params.deviation and state.position ~= "short" then
        -- Price is significantly above mean - expect reversion down
        log.info(string.format("SELL signal: Z=%.2f (price %.2f above SMA %.2f)",
                               z_score, current_price, sma))

        -- Close long position first if needed
        if state.position == "long" then
            local close = trader.market_order(params.pair, "sell", params.quantity)
            if close.success then
                local pnl = current_price - state.entry_price
                log.info(string.format("Closed LONG at %.2f (entry: %.2f, P&L: %.2f)",
                                       current_price, state.entry_price, pnl * params.quantity))
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

    elseif math.abs(z_score) < 0.5 and state.position ~= "flat" then
        -- Price returned near mean - close position
        log.info(string.format("CLOSE signal: Z=%.2f (price near mean)", z_score))

        local side = state.position == "long" and "sell" or "buy"
        local order = trader.market_order(params.pair, side, params.quantity)
        if order.success then
            local pnl
            if state.position == "long" then
                pnl = (current_price - state.entry_price) * params.quantity
            else
                pnl = (state.entry_price - current_price) * params.quantity
            end
            log.info(string.format("Position closed at %.2f (P&L: %.4f)", current_price, pnl))
            state.position = "flat"
            state.entry_price = 0
        end
    end
end

function on_data(channel, pair, data)
    -- Use WebSocket ticker updates to supplement price data
    if channel == "ticker" and pair == params.pair then
        if data and data.c then
            local ws_price = tonumber(data.c[1])
            if ws_price and #state.prices > 0 then
                -- Update the most recent price entry
                state.prices[#state.prices] = ws_price
            end
        end
    end
end

function on_stop()
    log.info(string.format("Mean Reversion stopping (trades: %d, position: %s)",
                           state.total_trades, state.position))

    -- Flatten position on stop
    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        log.info("Flattening position: " .. side)
        trader.market_order(params.pair, side, params.quantity)
    end

    trader.unsubscribe("ticker", { params.pair })
end

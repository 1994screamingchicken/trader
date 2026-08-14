-- RSI (Relative Strength Index) Strategy
-- =======================================
-- Computes RSI over a configurable period. Buys when RSI drops below
-- the oversold threshold and sells when RSI rises above the overbought
-- threshold. Classic mean-reversion indicator.
--
-- Parameters:
--   pair       - Trading pair (e.g., "ETHUSD")
--   period     - RSI calculation period (e.g., 14)
--   oversold   - RSI level to trigger buy (e.g., 30)
--   overbought - RSI level to trigger sell (e.g., 70)
--   quantity   - Order size in base currency units

-- Strategy state
local state = {
    prices = {},
    position = "flat",
    tick_count = 0,
    sample_interval = 10,
    entry_price = 0,
    total_trades = 0,
    last_rsi = 50,
}

-- Calculate RSI from a list of prices
-- Needs at least (period + 1) prices
local function calculate_rsi(prices, period)
    if #prices < period + 1 then
        return nil
    end

    local gains = 0
    local losses = 0

    -- Calculate initial average gain/loss over the period
    for i = #prices - period + 1, #prices do
        local change = prices[i] - prices[i - 1]
        if change > 0 then
            gains = gains + change
        else
            losses = losses + math.abs(change)
        end
    end

    local avg_gain = gains / period
    local avg_loss = losses / period

    if avg_loss == 0 then
        return 100  -- No losses means RSI is maxed
    end

    local rs = avg_gain / avg_loss
    local rsi = 100 - (100 / (1 + rs))

    return rsi
end

function on_init()
    log.info("RSI strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Period: %d", params.period))
    log.info(string.format("  Oversold: %d", params.oversold))
    log.info(string.format("  Overbought: %d", params.overbought))
    log.info(string.format("  Quantity: %.6f", params.quantity))

    trader.subscribe("ticker", { params.pair })

    -- Pre-load price history from OHLC
    local result = trader.get_ohlc(params.pair, 1)  -- 1-minute candles
    if result.success and result.data then
        for _, candle_data in pairs(result.data) do
            if type(candle_data) == "table" then
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

        -- Keep a reasonable amount
        local max_history = params.period * 3
        while #state.prices > max_history do
            table.remove(state.prices, 1)
        end

        log.info(string.format("  Pre-loaded %d price samples", #state.prices))

        -- Calculate initial RSI
        local rsi = calculate_rsi(state.prices, params.period)
        if rsi then
            state.last_rsi = rsi
            log.info(string.format("  Initial RSI: %.1f", rsi))
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

    -- Update price history
    table.insert(state.prices, current_price)

    -- Trim to prevent unbounded growth
    local max_history = params.period * 3
    while #state.prices > max_history do
        table.remove(state.prices, 1)
    end

    -- Calculate RSI
    local rsi = calculate_rsi(state.prices, params.period)
    if not rsi then
        return
    end

    state.last_rsi = rsi

    -- Periodic status logging
    if state.tick_count % (state.sample_interval * 20) == 0 then
        log.info(string.format("Price: %.2f | RSI: %.1f | Position: %s",
                               current_price, rsi, state.position))
    end

    -- Trading signals
    if rsi <= params.oversold and state.position ~= "long" then
        -- RSI oversold - buy signal
        log.info(string.format("BUY signal: RSI=%.1f (oversold at %d), price=%.2f",
                               rsi, params.oversold, current_price))

        -- Close short if open
        if state.position == "short" then
            local close = trader.market_order(params.pair, "buy", params.quantity)
            if close.success then
                local pnl = state.entry_price - current_price
                log.info(string.format("Closed SHORT (P&L per unit: %.2f)", pnl))
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

    elseif rsi >= params.overbought and state.position ~= "short" then
        -- RSI overbought - sell signal
        log.info(string.format("SELL signal: RSI=%.1f (overbought at %d), price=%.2f",
                               rsi, params.overbought, current_price))

        -- Close long if open
        if state.position == "long" then
            local close = trader.market_order(params.pair, "sell", params.quantity)
            if close.success then
                local pnl = current_price - state.entry_price
                log.info(string.format("Closed LONG (P&L per unit: %.2f)", pnl))
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

    elseif state.position == "long" and rsi >= 50 and rsi < params.overbought then
        -- Take profit when RSI returns to neutral from oversold
        if state.entry_price > 0 and current_price > state.entry_price then
            local profit_pct = ((current_price - state.entry_price) / state.entry_price) * 100
            if profit_pct >= 0.5 then
                log.info(string.format("Taking profit: RSI=%.1f, profit=%.2f%%", rsi, profit_pct))
                local order = trader.market_order(params.pair, "sell", params.quantity)
                if order.success then
                    state.position = "flat"
                    state.entry_price = 0
                end
            end
        end

    elseif state.position == "short" and rsi <= 50 and rsi > params.oversold then
        -- Take profit when RSI returns to neutral from overbought
        if state.entry_price > 0 and current_price < state.entry_price then
            local profit_pct = ((state.entry_price - current_price) / state.entry_price) * 100
            if profit_pct >= 0.5 then
                log.info(string.format("Taking profit: RSI=%.1f, profit=%.2f%%", rsi, profit_pct))
                local order = trader.market_order(params.pair, "buy", params.quantity)
                if order.success then
                    state.position = "flat"
                    state.entry_price = 0
                end
            end
        end
    end
end

function on_data(channel, pair, data)
    if channel == "ticker" and pair == params.pair then
        if data and data.c then
            local ws_price = tonumber(data.c[1])
            if ws_price and #state.prices > 0 then
                state.prices[#state.prices] = ws_price
            end
        end
    end
end

function on_stop()
    log.info(string.format("RSI strategy stopping (trades: %d, last RSI: %.1f, position: %s)",
                           state.total_trades, state.last_rsi, state.position))

    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info("Flattened position on stop")
    end

    trader.unsubscribe("ticker", { params.pair })
end

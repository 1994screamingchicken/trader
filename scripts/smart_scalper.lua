-- Smart Scalper - Mean Reversion with Momentum Filter
-- ====================================================
-- On 5-minute charts, price tends to revert after sharp moves.
-- This strategy:
--   1. Tracks a short moving average (5 candles = 25 min)
--   2. Buys when price dips BELOW the average by a threshold (oversold)
--   3. Sells when price rises ABOVE the average by a threshold (overbought)
--   4. Takes profit when price returns to the mean
--   5. Uses a stop-loss to limit downside
--
-- Key insight: DON'T chase momentum on 5-min charts. Fade the moves.
--
-- Parameters (set by backtest engine or config):
--   pair      - Trading pair
--   threshold - How far from mean to enter (default 0.15%)
--   quantity  - Order size

local state = {
    prices = {},
    ma_length = 5,        -- 5-candle moving average (25 min on 5m chart)
    position = "flat",
    entry_price = 0,
    tick_count = 0,
    total_trades = 0,
    wins = 0,
    losses = 0,
    total_pnl = 0,
    -- Risk management
    take_profit_pct = 0.12,  -- Take profit at 0.12% gain
    stop_loss_pct = 0.20,    -- Stop loss at 0.20% loss
}

local function get_ma()
    if #state.prices < state.ma_length then
        return nil
    end
    local sum = 0
    for i = #state.prices - state.ma_length + 1, #state.prices do
        sum = sum + state.prices[i]
    end
    return sum / state.ma_length
end

function on_init()
    log.info(string.format("[%s] Smart Scalper initializing (mean reversion)", params.pair))
    log.info(string.format("[%s]   MA length: %d candles", params.pair, state.ma_length))
    log.info(string.format("[%s]   Entry threshold: %.2f%%", params.pair, params.threshold or 0.15))
    log.info(string.format("[%s]   Take profit: %.2f%%, Stop loss: %.2f%%",
                           params.pair, state.take_profit_pct, state.stop_loss_pct))

    -- Get initial price to seed the MA
    local result = trader.get_ticker(params.pair)
    if result.success and result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                local price = tonumber(v.c[1])
                if price then
                    table.insert(state.prices, price)
                    log.info(string.format("[%s] Initial price: %.4f", params.pair, price))
                end
                break
            end
        end
    end
end

function on_tick()
    state.tick_count = state.tick_count + 1

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

    -- Add to price history
    table.insert(state.prices, current_price)
    -- Keep only what we need for MA
    while #state.prices > state.ma_length * 2 do
        table.remove(state.prices, 1)
    end

    -- Need enough data for MA
    local ma = get_ma()
    if not ma then
        return
    end

    -- Calculate deviation from mean
    local deviation_pct = ((current_price - ma) / ma) * 100
    local threshold = params.threshold or 0.15

    -- Log every 5 ticks
    if state.tick_count % 5 == 0 then
        log.info(string.format("[%s] Price: %.2f | MA: %.2f | Dev: %+.3f%% | Pos: %s | W/L: %d/%d | PnL: %.2f",
                               params.pair, current_price, ma, deviation_pct,
                               state.position, state.wins, state.losses, state.total_pnl))
    end

    -- === POSITION MANAGEMENT (check exits first) ===
    if state.position == "long" then
        local pnl_pct = ((current_price - state.entry_price) / state.entry_price) * 100

        -- Take profit: price returned to or above mean
        if pnl_pct >= state.take_profit_pct then
            local order = trader.market_order(params.pair, "sell", params.quantity)
            if order.success then
                local pnl = (current_price - state.entry_price) * params.quantity
                state.total_pnl = state.total_pnl + pnl
                state.wins = state.wins + 1
                state.total_trades = state.total_trades + 1
                state.position = "flat"
                log.info(string.format("[WIN] SELL %s @ %.2f (profit: +%.3f%%  $%.4f)",
                                       params.pair, current_price, pnl_pct, pnl))
            end
            return
        end

        -- Stop loss
        if pnl_pct <= -state.stop_loss_pct then
            local order = trader.market_order(params.pair, "sell", params.quantity)
            if order.success then
                local pnl = (current_price - state.entry_price) * params.quantity
                state.total_pnl = state.total_pnl + pnl
                state.losses = state.losses + 1
                state.total_trades = state.total_trades + 1
                state.position = "flat"
                log.info(string.format("[LOSS] SELL %s @ %.2f (loss: %.3f%%  $%.4f)",
                                       params.pair, current_price, pnl_pct, pnl))
            end
            return
        end

    elseif state.position == "short" then
        local pnl_pct = ((state.entry_price - current_price) / state.entry_price) * 100

        -- Take profit: price returned to or below mean
        if pnl_pct >= state.take_profit_pct then
            local order = trader.market_order(params.pair, "buy", params.quantity)
            if order.success then
                local pnl = (state.entry_price - current_price) * params.quantity
                state.total_pnl = state.total_pnl + pnl
                state.wins = state.wins + 1
                state.total_trades = state.total_trades + 1
                state.position = "flat"
                log.info(string.format("[WIN] BUY %s @ %.2f (profit: +%.3f%%  $%.4f)",
                                       params.pair, current_price, pnl_pct, pnl))
            end
            return
        end

        -- Stop loss
        if pnl_pct <= -state.stop_loss_pct then
            local order = trader.market_order(params.pair, "buy", params.quantity)
            if order.success then
                local pnl = (state.entry_price - current_price) * params.quantity
                state.total_pnl = state.total_pnl + pnl
                state.losses = state.losses + 1
                state.total_trades = state.total_trades + 1
                state.position = "flat"
                log.info(string.format("[LOSS] BUY %s @ %.2f (loss: %.3f%%  $%.4f)",
                                       params.pair, current_price, pnl_pct, pnl))
            end
            return
        end
    end

    -- === ENTRY SIGNALS (only when flat) ===
    if state.position == "flat" then
        -- Price is BELOW mean by threshold -> BUY (expect reversion up)
        if deviation_pct <= -threshold then
            local order = trader.market_order(params.pair, "buy", params.quantity)
            if order.success then
                state.entry_price = current_price
                state.position = "long"
                log.info(string.format("[ENTRY] BUY %s @ %.2f (%.3f%% below MA)",
                                       params.pair, current_price, -deviation_pct))
            end

        -- Price is ABOVE mean by threshold -> SELL (expect reversion down)
        elseif deviation_pct >= threshold then
            local order = trader.market_order(params.pair, "sell", params.quantity)
            if order.success then
                state.entry_price = current_price
                state.position = "short"
                log.info(string.format("[ENTRY] SELL %s @ %.2f (%.3f%% above MA)",
                                       params.pair, current_price, deviation_pct))
            end
        end
    end
end

function on_stop()
    log.info(string.format("[%s] Smart Scalper results: %d trades | W: %d L: %d | P&L: $%.4f",
                           params.pair, state.total_trades, state.wins, state.losses, state.total_pnl))

    -- Close any open position
    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info(string.format("[%s] Closed remaining %s position", params.pair, state.position))
    end
end

-- Scalping Strategy
-- =================
-- High-frequency strategy that aims for many small profits by placing
-- tight limit orders around the current spread. Enters and exits
-- quickly, targeting a small profit per trade.
--
-- Parameters:
--   pair         - Trading pair (e.g., "XBTUSD")
--   quantity     - Order size in base currency units
--   profit_ticks - Number of price ticks for profit target (e.g., 5)
--   max_hold     - Maximum hold time in ticks before exiting (e.g., 100)
--   spread_mult  - Multiplier for entry offset from mid (e.g., 0.5)

-- Strategy state
local state = {
    position = "flat",
    entry_price = 0,
    entry_tick = 0,
    tick_count = 0,
    total_trades = 0,
    winning_trades = 0,
    losing_trades = 0,
    total_pnl = 0,
    active_order_id = nil,
    cooldown = 0,
}

function on_init()
    log.info("Scalper strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Quantity: %.6f", params.quantity))
    log.info(string.format("  Profit target: %d ticks", params.profit_ticks))
    log.info(string.format("  Max hold: %d ticks", params.max_hold))
    log.info(string.format("  Spread multiplier: %.2f", params.spread_mult))

    trader.subscribe("ticker", { params.pair })
end

function on_tick()
    state.tick_count = state.tick_count + 1

    -- Cooldown after a trade
    if state.cooldown > 0 then
        state.cooldown = state.cooldown - 1
        return
    end

    -- Get current market data (bid/ask)
    local result = trader.get_ticker(params.pair)
    if not result.success then
        return
    end

    local bid = nil
    local ask = nil
    local last_price = nil

    if result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" then
                if v.b then bid = tonumber(v.b[1]) end
                if v.a then ask = tonumber(v.a[1]) end
                if v.c then last_price = tonumber(v.c[1]) end
                break
            end
        end
    end

    if not bid or not ask or not last_price then
        return
    end

    local spread = ask - bid
    local mid_price = (bid + ask) / 2

    -- Manage open position
    if state.position ~= "flat" then
        local hold_time = state.tick_count - state.entry_tick
        local current_pnl = 0

        if state.position == "long" then
            current_pnl = last_price - state.entry_price
        else
            current_pnl = state.entry_price - last_price
        end

        -- Check profit target
        local target = spread * params.profit_ticks
        if current_pnl >= target then
            -- Take profit
            local side = state.position == "long" and "sell" or "buy"
            local order = trader.market_order(params.pair, side, params.quantity)
            if order.success then
                state.total_pnl = state.total_pnl + current_pnl
                state.winning_trades = state.winning_trades + 1
                state.total_trades = state.total_trades + 1
                log.info(string.format("PROFIT: %.4f (held %d ticks, trade #%d)",
                                       current_pnl, hold_time, state.total_trades))
                state.position = "flat"
                state.cooldown = 5  -- Brief cooldown after profit
            end
            return
        end

        -- Check max hold time (exit regardless)
        if hold_time >= params.max_hold then
            local side = state.position == "long" and "sell" or "buy"
            local order = trader.market_order(params.pair, side, params.quantity)
            if order.success then
                state.total_pnl = state.total_pnl + current_pnl
                if current_pnl >= 0 then
                    state.winning_trades = state.winning_trades + 1
                else
                    state.losing_trades = state.losing_trades + 1
                end
                state.total_trades = state.total_trades + 1
                log.info(string.format("MAX HOLD exit: %.4f (held %d ticks, trade #%d)",
                                       current_pnl, hold_time, state.total_trades))
                state.position = "flat"
                state.cooldown = 10  -- Longer cooldown after forced exit
            end
            return
        end

        -- Check stop loss (2x the profit target as loss limit)
        local max_loss = target * 2
        if current_pnl <= -max_loss then
            local side = state.position == "long" and "sell" or "buy"
            local order = trader.market_order(params.pair, side, params.quantity)
            if order.success then
                state.total_pnl = state.total_pnl + current_pnl
                state.losing_trades = state.losing_trades + 1
                state.total_trades = state.total_trades + 1
                log.warn(string.format("STOP LOSS: %.4f (held %d ticks, trade #%d)",
                                       current_pnl, hold_time, state.total_trades))
                state.position = "flat"
                state.cooldown = 20  -- Longest cooldown after loss
            end
            return
        end

        return  -- Already in a position, wait
    end

    -- Entry logic: only enter when spread is reasonable
    if spread <= 0 then
        return
    end

    -- Determine entry direction based on order book depth
    local book = trader.get_order_book(params.pair, 5)
    local buy_pressure = 0
    local sell_pressure = 0

    if book.success and book.data then
        for _, pair_data in pairs(book.data) do
            if type(pair_data) == "table" then
                if pair_data.bids then
                    for _, bid_level in ipairs(pair_data.bids) do
                        if type(bid_level) == "table" and #bid_level >= 2 then
                            buy_pressure = buy_pressure + tonumber(bid_level[2] or 0)
                        end
                    end
                end
                if pair_data.asks then
                    for _, ask_level in ipairs(pair_data.asks) do
                        if type(ask_level) == "table" and #ask_level >= 2 then
                            sell_pressure = sell_pressure + tonumber(ask_level[2] or 0)
                        end
                    end
                end
            end
        end
    end

    -- Enter in the direction of order book imbalance
    local offset = spread * params.spread_mult

    if buy_pressure > sell_pressure * 1.2 then
        -- More buy pressure - go long
        local entry = bid + offset
        local order = trader.limit_order(params.pair, "buy", params.quantity, entry)
        if order.success then
            state.position = "long"
            state.entry_price = entry
            state.entry_tick = state.tick_count
            log.debug(string.format("Scalp LONG entry at %.2f (spread: %.4f)", entry, spread))
        end

    elseif sell_pressure > buy_pressure * 1.2 then
        -- More sell pressure - go short
        local entry = ask - offset
        local order = trader.limit_order(params.pair, "sell", params.quantity, entry)
        if order.success then
            state.position = "short"
            state.entry_price = entry
            state.entry_tick = state.tick_count
            log.debug(string.format("Scalp SHORT entry at %.2f (spread: %.4f)", entry, spread))
        end
    end

    -- Periodic stats
    if state.tick_count % 500 == 0 then
        local win_rate = 0
        if state.total_trades > 0 then
            win_rate = (state.winning_trades / state.total_trades) * 100
        end
        log.info(string.format("Scalper stats: trades=%d, W/L=%d/%d (%.1f%%), PnL=%.4f",
                               state.total_trades, state.winning_trades, state.losing_trades,
                               win_rate, state.total_pnl))
    end
end

function on_data(channel, pair, data)
    -- Real-time updates can improve scalper timing
    if channel == "ticker" and pair == params.pair then
        -- Fast price updates handled in on_tick via polling
    end
end

function on_stop()
    local win_rate = 0
    if state.total_trades > 0 then
        win_rate = (state.winning_trades / state.total_trades) * 100
    end

    log.info(string.format("Scalper stopping - Trades: %d | Win rate: %.1f%% | Total PnL: %.4f",
                           state.total_trades, win_rate, state.total_pnl))

    -- Close any open position
    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info("Closed open scalp position")
    end

    -- Cancel any pending orders
    trader.cancel_all()
    trader.unsubscribe("ticker", { params.pair })
end

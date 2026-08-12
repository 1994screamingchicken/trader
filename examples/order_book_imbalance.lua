-- Order Book Imbalance Strategy
-- ==============================
-- Analyzes the order book to detect imbalances between bid and ask
-- volume at top levels. When significant imbalance exists, trades in
-- the direction of heavier volume, anticipating short-term price
-- pressure from the larger side.
--
-- Parameters:
--   pair            - Trading pair (e.g., "XBTUSD")
--   quantity        - Order size in base currency units
--   depth           - Order book depth to analyze (e.g., 10)
--   imbalance_ratio - Min bid/ask ratio to trigger trade (e.g., 2.0)
--   cooldown_ticks  - Ticks between trades to avoid overtrading (e.g., 50)
--   take_profit_pct - Profit target percentage (e.g., 0.3)
--   stop_loss_pct   - Stop loss percentage (e.g., 0.5)

-- Strategy state
local state = {
    tick_count = 0,
    position = "flat",
    entry_price = 0,
    last_trade_tick = 0,
    total_trades = 0,
    winning_trades = 0,
    losing_trades = 0,
    total_pnl = 0,
    sample_interval = 3,
    imbalance_history = {},  -- Track recent imbalance readings
    max_history = 10,
}

-- Calculate order book imbalance ratio
local function analyze_book(book_data, depth)
    local bid_volume = 0
    local ask_volume = 0
    local best_bid = 0
    local best_ask = 0

    for _, pair_data in pairs(book_data) do
        if type(pair_data) == "table" then
            if pair_data.bids then
                for i, level in ipairs(pair_data.bids) do
                    if i > depth then break end
                    if type(level) == "table" and #level >= 2 then
                        local price = tonumber(level[1])
                        local vol = tonumber(level[2])
                        if price and vol then
                            bid_volume = bid_volume + vol
                            if i == 1 then best_bid = price end
                        end
                    end
                end
            end
            if pair_data.asks then
                for i, level in ipairs(pair_data.asks) do
                    if i > depth then break end
                    if type(level) == "table" and #level >= 2 then
                        local price = tonumber(level[1])
                        local vol = tonumber(level[2])
                        if price and vol then
                            ask_volume = ask_volume + vol
                            if i == 1 then best_ask = price end
                        end
                    end
                end
            end
        end
    end

    return {
        bid_volume = bid_volume,
        ask_volume = ask_volume,
        best_bid = best_bid,
        best_ask = best_ask,
        mid_price = (best_bid + best_ask) / 2,
    }
end

function on_init()
    log.info("Order Book Imbalance strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Quantity: %.6f", params.quantity))
    log.info(string.format("  Depth: %d levels", params.depth))
    log.info(string.format("  Imbalance ratio: %.1fx", params.imbalance_ratio))
    log.info(string.format("  Cooldown: %d ticks", params.cooldown_ticks))
    log.info(string.format("  Take profit: %.2f%%", params.take_profit_pct))
    log.info(string.format("  Stop loss: %.2f%%", params.stop_loss_pct))

    trader.subscribe("ticker", { params.pair })
end

function on_tick()
    state.tick_count = state.tick_count + 1

    if state.tick_count % state.sample_interval ~= 0 then
        return
    end

    -- Get order book
    local book = trader.get_order_book(params.pair, params.depth)
    if not book.success or not book.data then
        return
    end

    local analysis = analyze_book(book.data, params.depth)

    if analysis.bid_volume == 0 or analysis.ask_volume == 0 then
        return
    end

    if analysis.mid_price == 0 then
        return
    end

    -- Calculate imbalance
    local bid_ask_ratio = analysis.bid_volume / analysis.ask_volume
    local ask_bid_ratio = analysis.ask_volume / analysis.bid_volume

    -- Track imbalance history for confirmation
    table.insert(state.imbalance_history, bid_ask_ratio)
    while #state.imbalance_history > state.max_history do
        table.remove(state.imbalance_history, 1)
    end

    -- Manage open position
    if state.position ~= "flat" then
        local current_price = analysis.mid_price
        local pnl_pct = 0

        if state.position == "long" then
            pnl_pct = ((current_price - state.entry_price) / state.entry_price) * 100
        else
            pnl_pct = ((state.entry_price - current_price) / state.entry_price) * 100
        end

        -- Take profit
        if pnl_pct >= params.take_profit_pct then
            local side = state.position == "long" and "sell" or "buy"
            local order = trader.market_order(params.pair, side, params.quantity)
            if order.success then
                local pnl = pnl_pct * state.entry_price * params.quantity / 100
                state.total_pnl = state.total_pnl + pnl
                state.winning_trades = state.winning_trades + 1
                state.total_trades = state.total_trades + 1
                state.last_trade_tick = state.tick_count
                log.info(string.format("TAKE PROFIT (%s): +%.2f%% at %.2f (trade #%d)",
                                       state.position, pnl_pct, current_price, state.total_trades))
                state.position = "flat"
            end
            return
        end

        -- Stop loss
        if pnl_pct <= -params.stop_loss_pct then
            local side = state.position == "long" and "sell" or "buy"
            local order = trader.market_order(params.pair, side, params.quantity)
            if order.success then
                local pnl = pnl_pct * state.entry_price * params.quantity / 100
                state.total_pnl = state.total_pnl + pnl
                state.losing_trades = state.losing_trades + 1
                state.total_trades = state.total_trades + 1
                state.last_trade_tick = state.tick_count
                log.warn(string.format("STOP LOSS (%s): %.2f%% at %.2f (trade #%d)",
                                       state.position, pnl_pct, current_price, state.total_trades))
                state.position = "flat"
            end
            return
        end

        return  -- Hold position
    end

    -- Check cooldown
    if state.tick_count - state.last_trade_tick < params.cooldown_ticks then
        return
    end

    -- Need enough history for confirmation
    if #state.imbalance_history < 3 then
        return
    end

    -- Check for confirmed imbalance (recent readings should agree)
    local recent_bullish = 0
    local recent_bearish = 0
    for i = math.max(1, #state.imbalance_history - 2), #state.imbalance_history do
        if state.imbalance_history[i] >= params.imbalance_ratio then
            recent_bullish = recent_bullish + 1
        elseif (1 / state.imbalance_history[i]) >= params.imbalance_ratio then
            recent_bearish = recent_bearish + 1
        end
    end

    -- Entry signals (require confirmation from multiple readings)
    if recent_bullish >= 2 and bid_ask_ratio >= params.imbalance_ratio then
        -- Strong bid-side pressure - go long
        log.info(string.format("LONG signal: bid/ask ratio=%.2f (threshold: %.2f) | bids=%.2f asks=%.2f",
                               bid_ask_ratio, params.imbalance_ratio,
                               analysis.bid_volume, analysis.ask_volume))

        local order = trader.market_order(params.pair, "buy", params.quantity)
        if order.success then
            state.position = "long"
            state.entry_price = analysis.mid_price
            log.info(string.format("Opened LONG at %.2f", analysis.mid_price))
        end

    elseif recent_bearish >= 2 and ask_bid_ratio >= params.imbalance_ratio then
        -- Strong ask-side pressure - go short
        log.info(string.format("SHORT signal: ask/bid ratio=%.2f (threshold: %.2f) | asks=%.2f bids=%.2f",
                               ask_bid_ratio, params.imbalance_ratio,
                               analysis.ask_volume, analysis.bid_volume))

        local order = trader.market_order(params.pair, "sell", params.quantity)
        if order.success then
            state.position = "short"
            state.entry_price = analysis.mid_price
            log.info(string.format("Opened SHORT at %.2f", analysis.mid_price))
        end
    end

    -- Periodic stats
    if state.tick_count % 300 == 0 then
        local win_rate = 0
        if state.total_trades > 0 then
            win_rate = (state.winning_trades / state.total_trades) * 100
        end
        log.info(string.format("Book: bid_vol=%.2f ask_vol=%.2f ratio=%.2f | Trades: %d (%.0f%% win) | PnL: %.4f",
                               analysis.bid_volume, analysis.ask_volume, bid_ask_ratio,
                               state.total_trades, win_rate, state.total_pnl))
    end
end

function on_data(channel, pair, data)
    -- WebSocket book updates could be used for faster signals
    if channel == "ticker" and pair == params.pair then
        -- Price updates from ticker stream
    end
end

function on_stop()
    local win_rate = 0
    if state.total_trades > 0 then
        win_rate = (state.winning_trades / state.total_trades) * 100
    end

    log.info("=== Order Book Imbalance Summary ===")
    log.info(string.format("  Total trades: %d", state.total_trades))
    log.info(string.format("  Win/Loss: %d/%d (%.1f%%)", state.winning_trades, state.losing_trades, win_rate))
    log.info(string.format("  Total PnL: %.4f", state.total_pnl))

    if state.position ~= "flat" then
        local side = state.position == "long" and "sell" or "buy"
        trader.market_order(params.pair, side, params.quantity)
        log.info("Flattened position on stop")
    end

    trader.cancel_all()
    trader.unsubscribe("ticker", { params.pair })
end

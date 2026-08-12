-- DCA (Dollar Cost Averaging) Accumulator
-- =========================================
-- Systematically buys a fixed amount at regular intervals, regardless
-- of price. Optionally buys extra during dips (enhanced DCA). Simple
-- long-term accumulation strategy that reduces timing risk.
--
-- Parameters:
--   pair            - Trading pair (e.g., "XBTUSD")
--   base_quantity   - Base order size per interval (e.g., 0.001)
--   buy_interval    - Number of ticks between buys (e.g., 6000 = ~10min at 100ms)
--   dip_threshold   - Percentage drop to trigger extra buy (e.g., 3.0)
--   dip_multiplier  - Multiplier for dip buys (e.g., 2.0 = double size on dips)
--   max_position    - Maximum total accumulated quantity (e.g., 1.0)

-- Strategy state
local state = {
    tick_count = 0,
    last_buy_tick = 0,
    total_bought = 0,         -- Total quantity accumulated
    total_spent = 0,          -- Total cost basis
    average_price = 0,        -- Average entry price
    buy_count = 0,            -- Number of buy orders executed
    reference_price = nil,    -- Price at last buy for dip detection
    highest_price = 0,        -- Highest price seen (for drawdown calc)
}

function on_init()
    log.info("DCA Accumulator strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Base quantity: %.6f", params.base_quantity))
    log.info(string.format("  Buy interval: %d ticks", params.buy_interval))
    log.info(string.format("  Dip threshold: %.1f%%", params.dip_threshold))
    log.info(string.format("  Dip multiplier: %.1fx", params.dip_multiplier))
    log.info(string.format("  Max position: %.6f", params.max_position))

    -- Get initial price
    local result = trader.get_ticker(params.pair)
    if result.success and result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                state.reference_price = tonumber(v.c[1])
                state.highest_price = state.reference_price
                break
            end
        end
    end

    if state.reference_price then
        log.info(string.format("  Starting price: %.2f", state.reference_price))
    end

    trader.subscribe("ticker", { params.pair })
end

function on_tick()
    state.tick_count = state.tick_count + 1

    -- Check if we've reached max position
    if state.total_bought >= params.max_position then
        if state.tick_count % 1000 == 0 then
            log.info(string.format("Max position reached (%.6f/%.6f). Holding.",
                                   state.total_bought, params.max_position))
        end
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

    -- Track highest price for drawdown calculation
    if current_price > state.highest_price then
        state.highest_price = current_price
    end

    -- Initialize reference price if not set
    if not state.reference_price then
        state.reference_price = current_price
        return
    end

    -- Check for dip buy opportunity
    local dip_pct = ((state.reference_price - current_price) / state.reference_price) * 100
    local is_dip = dip_pct >= params.dip_threshold

    -- Regular interval buy
    local ticks_since_buy = state.tick_count - state.last_buy_tick
    local should_buy = ticks_since_buy >= params.buy_interval

    if should_buy or is_dip then
        -- Calculate order size
        local quantity = params.base_quantity

        if is_dip then
            quantity = params.base_quantity * params.dip_multiplier
            log.info(string.format("DIP detected: %.1f%% drop (ref: %.2f, now: %.2f) - enhanced buy",
                                   dip_pct, state.reference_price, current_price))
        end

        -- Cap at max position
        local remaining = params.max_position - state.total_bought
        if quantity > remaining then
            quantity = remaining
        end

        if quantity <= 0 then
            return
        end

        -- Place buy order
        local order = trader.market_order(params.pair, "buy", quantity)
        if order.success then
            state.total_bought = state.total_bought + quantity
            state.total_spent = state.total_spent + (quantity * current_price)
            state.average_price = state.total_spent / state.total_bought
            state.buy_count = state.buy_count + 1
            state.last_buy_tick = state.tick_count
            state.reference_price = current_price

            log.info(string.format("BUY #%d: %.6f @ %.2f | Total: %.6f | Avg price: %.2f",
                                   state.buy_count, quantity, current_price,
                                   state.total_bought, state.average_price))
        else
            log.error("Buy order failed: " .. (order.error or "unknown"))
        end

    elseif not should_buy and not is_dip then
        -- Periodic status update
        if state.tick_count % 3000 == 0 then
            local unrealized_pnl = 0
            if state.total_bought > 0 then
                unrealized_pnl = (current_price - state.average_price) * state.total_bought
            end
            local drawdown = 0
            if state.highest_price > 0 then
                drawdown = ((state.highest_price - current_price) / state.highest_price) * 100
            end

            log.info(string.format(
                "DCA Status: buys=%d | position=%.6f | avg=%.2f | price=%.2f | uPnL=%.4f | drawdown=%.1f%%",
                state.buy_count, state.total_bought, state.average_price,
                current_price, unrealized_pnl, drawdown))
        end
    end
end

function on_data(channel, pair, data)
    if channel == "ticker" and pair == params.pair then
        if data and data.c then
            local price = tonumber(data.c[1])
            if price and price > state.highest_price then
                state.highest_price = price
            end
        end
    end
end

function on_stop()
    local unrealized_pnl = 0
    local current_value = 0

    -- Get final price for reporting
    local result = trader.get_ticker(params.pair)
    if result.success and result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                local final_price = tonumber(v.c[1])
                if final_price and state.total_bought > 0 then
                    current_value = state.total_bought * final_price
                    unrealized_pnl = (final_price - state.average_price) * state.total_bought
                end
                break
            end
        end
    end

    log.info("=== DCA Accumulator Summary ===")
    log.info(string.format("  Total buys: %d", state.buy_count))
    log.info(string.format("  Total accumulated: %.6f", state.total_bought))
    log.info(string.format("  Total spent: %.2f", state.total_spent))
    log.info(string.format("  Average price: %.2f", state.average_price))
    log.info(string.format("  Current value: %.2f", current_value))
    log.info(string.format("  Unrealized P&L: %.4f", unrealized_pnl))

    trader.unsubscribe("ticker", { params.pair })
    -- Note: DCA does not auto-sell on stop - position is held
end

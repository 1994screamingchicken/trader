-- Grid Trading Strategy
-- =====================
-- Places a grid of limit buy and sell orders at fixed intervals around
-- a reference price. Profits from price oscillation within a range.
--
-- Parameters:
--   pair       - Trading pair (e.g., "XBTUSD")
--   grid_size  - Number of levels above and below (e.g., 5)
--   spacing    - Price spacing between grid levels (e.g., 100 for $100 gaps)
--   quantity   - Order size per grid level

-- Strategy state
local state = {
    reference_price = nil,
    grid_orders = {},       -- txid -> { level, side, price }
    tick_count = 0,
    setup_complete = false,
    check_interval = 50,    -- Check orders every N ticks
}

function on_init()
    log.info("Grid Trader strategy initializing")
    log.info(string.format("  Pair: %s", params.pair))
    log.info(string.format("  Grid levels: %d above + %d below", params.grid_size, params.grid_size))
    log.info(string.format("  Spacing: %.2f", params.spacing))
    log.info(string.format("  Quantity per level: %.6f", params.quantity))

    -- Get current price as reference
    local result = trader.get_ticker(params.pair)
    if result.success and result.data then
        for _, v in pairs(result.data) do
            if type(v) == "table" and v.c then
                state.reference_price = tonumber(v.c[1])
                break
            end
        end
    end

    if not state.reference_price then
        log.error("Cannot get reference price - grid setup deferred to first tick")
        return
    end

    log.info(string.format("  Reference price: %.2f", state.reference_price))
    setup_grid()
end

function setup_grid()
    if not state.reference_price then
        return
    end

    log.info("Setting up grid orders...")

    -- Place buy orders below reference price
    for i = 1, params.grid_size do
        local price = state.reference_price - (i * params.spacing)
        local result = trader.limit_order(params.pair, "buy", params.quantity, price)
        if result.success and result.data and result.data.txid then
            local txid = result.data.txid[1] or tostring(i) .. "_buy"
            state.grid_orders[txid] = {
                level = -i,
                side = "buy",
                price = price,
            }
            log.info(string.format("  Grid BUY #%d at %.2f", i, price))
        else
            log.warn(string.format("  Failed to place buy at %.2f: %s", price, result.error or "unknown"))
        end
    end

    -- Place sell orders above reference price
    for i = 1, params.grid_size do
        local price = state.reference_price + (i * params.spacing)
        local result = trader.limit_order(params.pair, "sell", params.quantity, price)
        if result.success and result.data and result.data.txid then
            local txid = result.data.txid[1] or tostring(i) .. "_sell"
            state.grid_orders[txid] = {
                level = i,
                side = "sell",
                price = price,
            }
            log.info(string.format("  Grid SELL #%d at %.2f", i, price))
        else
            log.warn(string.format("  Failed to place sell at %.2f: %s", price, result.error or "unknown"))
        end
    end

    state.setup_complete = true
    local total = 0
    for _ in pairs(state.grid_orders) do total = total + 1 end
    log.info(string.format("Grid setup complete: %d orders placed", total))
end

function on_tick()
    state.tick_count = state.tick_count + 1

    -- Try deferred setup if init failed
    if not state.setup_complete and state.tick_count == 10 then
        local result = trader.get_ticker(params.pair)
        if result.success and result.data then
            for _, v in pairs(result.data) do
                if type(v) == "table" and v.c then
                    state.reference_price = tonumber(v.c[1])
                    break
                end
            end
        end
        if state.reference_price then
            setup_grid()
        end
    end

    if not state.setup_complete then
        return
    end

    -- Periodically check if orders have been filled
    if state.tick_count % state.check_interval ~= 0 then
        return
    end

    -- Get current open orders to see what's been filled
    local result = trader.get_open_orders()
    if not result.success then
        return
    end

    -- Check which grid orders are still open
    local open_txids = {}
    if result.data and result.data.open then
        for txid, _ in pairs(result.data.open) do
            open_txids[txid] = true
        end
    end

    -- Find filled orders and replace them
    for txid, order_info in pairs(state.grid_orders) do
        if not open_txids[txid] then
            -- This order was filled - place a counter order
            log.info(string.format("Grid order filled: %s at %.2f (level %d)",
                                   order_info.side, order_info.price, order_info.level))

            -- If buy was filled, place a sell one level up
            -- If sell was filled, place a buy one level down
            local new_side, new_price
            if order_info.side == "buy" then
                new_side = "sell"
                new_price = order_info.price + params.spacing
            else
                new_side = "buy"
                new_price = order_info.price - params.spacing
            end

            local new_result = trader.limit_order(params.pair, new_side, params.quantity, new_price)
            if new_result.success and new_result.data and new_result.data.txid then
                local new_txid = new_result.data.txid[1]
                -- Remove old order, add new one
                state.grid_orders[txid] = nil
                state.grid_orders[new_txid] = {
                    level = order_info.level,
                    side = new_side,
                    price = new_price,
                }
                log.info(string.format("  Replaced with %s at %.2f", new_side, new_price))
            end
        end
    end
end

function on_stop()
    log.info("Grid Trader stopping - cancelling all grid orders")

    -- Cancel all grid orders
    local cancelled = 0
    for txid, info in pairs(state.grid_orders) do
        local result = trader.cancel_order(txid)
        if result.success then
            cancelled = cancelled + 1
        end
    end

    log.info(string.format("Cancelled %d grid orders", cancelled))

    -- Also do a cancel-all as safety measure
    trader.cancel_all()
end

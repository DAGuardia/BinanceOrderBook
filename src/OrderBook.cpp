#include "OrderBook.h"

OrderBook::OrderBook(std::string sym)
    : _symbol(std::move(sym))
{
}

void OrderBook::applyBidLevel(double px, double qty) {
    if (px <= 0.0 || qty < 0.0) return;
    std::lock_guard<std::mutex> lock(_mtx);
    if (qty == 0.0) {
        _bids.erase(px);
    }
    else {
        _bids[px] = qty;
    }
}

void OrderBook::applyAskLevel(double px, double qty) {
    if (px <= 0.0 || qty < 0.0) return;
    std::lock_guard<std::mutex> lock(_mtx);
    if (qty == 0.0) {
        _asks.erase(px);
    }
    else {
        _asks[px] = qty;
    }
}

void OrderBook::applyDepthDelta(const DepthUpdate& update) {
    std::lock_guard<std::mutex> lock(_mtx);

    // Actualizar niveles de compra (bids)
    for (const auto& [price, quantity] : update.bids) {
        if (price <= 0.0 || quantity < 0.0)
            continue; // entrada inválida, se ignora

        if (quantity == 0.0)
            _bids.erase(price); // eliminar nivel (sin oferta)
        else
            _bids[price] = quantity; // insertar o actualizar
    }

    // Actualizar niveles de venta (asks)
    for (const auto& [price, quantity] : update.asks) {
        if (price <= 0.0 || quantity < 0.0)
            continue;

        if (quantity == 0.0)
            _asks.erase(price);
        else
            _asks[price] = quantity;
    }
}

BookSnapshot OrderBook::snapshot(int topN) {
    std::lock_guard<std::mutex> lock(_mtx);

    BookSnapshot snap;
    snap.symbol = _symbol;

    if (!_bids.empty()) {
        snap.bestBidPx = _bids.begin()->first;
        snap.bestBidQty = _bids.begin()->second;
    }
    if (!_asks.empty()) {
        snap.bestAskPx = _asks.begin()->first;
        snap.bestAskQty = _asks.begin()->second;
    }

    int count = 0;
    for (auto& kv : _bids) {
        if (count++ >= topN) break;
        snap.topBids.push_back(Level{ kv.first, kv.second });
    }

    count = 0;
    for (auto& kv : _asks) {
        if (count++ >= topN) break;
        snap.topAsks.push_back(Level{ kv.first, kv.second });
    }

    return snap;
}

bool OrderBook::isSane() const {
    std::lock_guard<std::mutex> lock(_mtx);

    if (_bids.empty() || _asks.empty())
        return true; // libro vacío = sin datos, no necesariamente inválido

    double bestBidPrice = _bids.begin()->first;
    double bestAskPrice = _asks.begin()->first;

    // validación básica de integridad
    if (bestBidPrice <= 0.0 || bestAskPrice <= 0.0)
        return false;

    // bid nunca puede ser igual o mayor al ask
    if (bestBidPrice >= bestAskPrice)
        return false;

    return true;
}
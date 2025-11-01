#include "TradeStats.h"
#include <mutex>

void TradeStats::onTrade(const std::string& /*symbol*/,
    double price,
    double qty,
    bool isBuyerMaker)
{
    // Ignorar trades inválidos
    if (price <= 0.0 || qty <= 0.0)
        return;

    std::lock_guard<std::mutex> lock(_mtx);

    _last.price = price;
    _last.qty = qty;
    _last.side = isBuyerMaker ? "sell" : "buy";

    _sumPxQty += price * qty;
    _sumQty += qty;
}

TradeSnapshot TradeStats::snapshot() const {
    std::lock_guard<std::mutex> lock(_mtx);

    TradeSnapshot snapshot;
    snapshot.last = _last;
    snapshot.vwapSession = (_sumQty > 0.0) ? (_sumPxQty / _sumQty) : 0.0;

    return snapshot;
}

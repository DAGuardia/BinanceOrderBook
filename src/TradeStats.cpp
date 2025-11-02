#include "TradeStats.h"
#include <mutex>
#include "Utils.h"
#include <algorithm>
#include <deque>

void TradeStats::onTrade(double price, double qty, const std::string& sideFlag)
{
    std::lock_guard<std::mutex> lock(_mtx);

    // último trade
    _last.price = price;
    _last.qty = qty;
    _last.side = sideFlag; // "buy" o "sell"

    // vwap sesión (acumulado desde el inicio)
    _sumPxQty += price * qty;
    _sumQty += qty;

    // guardar en la ventana
    double tsNow = nowUnixSeconds();
    _recent.push_back(TimedTrade{ tsNow, price, qty });

    // recortar trades viejos (más de 300 segundos atrás)
    const double WINDOW_SEC = 300.0;
    double cutoff = tsNow - WINDOW_SEC;
    while (!_recent.empty() && _recent.front().ts < cutoff) {
        _recent.pop_front();
    }
}

TradeSnapshot TradeStats::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mtx);

    TradeSnapshot out;
    out.last = _last;

    // VWAP de sesión completa
    if (_sumQty > 0.0) {
        out.vwapSession = _sumPxQty / _sumQty;
    }
    else {
        out.vwapSession = 0.0;
    }

    // VWAP ventana móvil (últimos 5 minutos)
    const double WINDOW_SEC = 300.0;
    double now = nowUnixSeconds();
    double cutoff = now - WINDOW_SEC;

    double sumPxQtyWin = 0.0;
    double sumQtyWin = 0.0;

    for (const auto& t : _recent)
    {
        if (t.ts >= cutoff) {
            sumPxQtyWin += t.price * t.qty;
            sumQtyWin += t.qty;
        }
    }

    if (sumQtyWin > 0.0) {
        out.vwapWindow = sumPxQtyWin / sumQtyWin;
    }
    else {
        out.vwapWindow = 0.0;
    }

    return out;
}

#pragma once
#include <mutex>
#include <string>
#include <deque>

// -----------------------------------------------------------------------------
// Estructuras auxiliares
// -----------------------------------------------------------------------------

// Representa el último trade recibido para un símbolo.
struct LastTrade {
    double price = 0.0;       // Último precio ejecutado
    double qty = 0.0;         // Cantidad del último trade
    std::string side;         // "buy", "sell", o "" si aún no hubo trades
};

// Snapshot de métricas de sesión del símbolo.
struct TradeSnapshot {
    LastTrade last;           // Último trade conocido
    double vwapSession = 0.0; // VWAP acumulado (Σ p*q / Σ q)
    double vwapWindow = 0.0;
};

//para calculo de vwap
struct TimedTrade {
    double ts;    // epoch seconds
    double price;
    double qty;
};

// -----------------------------------------------------------------------------
// TradeStats
// -----------------------------------------------------------------------------
// Clase thread-safe que acumula y expone estadísticas de trading en tiempo real
// para un símbolo determinado. Se alimenta con los trades recibidos por
// BinanceTradeStream.
//
// Responsabilidad:
//   - Guardar el último trade (precio, cantidad y lado agresor).
//   - Calcular el VWAP de la sesión (ponderado por cantidad).
//   - Proveer snapshots inmutables de las métricas actuales.
//
// Ejemplo:
//   TradeStats stats;
//   stats.onTrade("btcusdt", 25000.5, 0.1, false);
//   auto snap = stats.snapshot();
// -----------------------------------------------------------------------------
class TradeStats {
public:
    void onTrade(double price, double qty, const std::string& sideFlag); // ya existe en tu código
    TradeSnapshot snapshot() const; // vamos a ampliarla

private:
    mutable std::mutex _mtx;

    LastTrade _last;

    // sesión completa
    double _sumPxQty = 0.0;
    double _sumQty = 0.0;

    // ventana móvil de 5m
    std::deque<TimedTrade> _recent;
};
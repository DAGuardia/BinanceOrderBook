#pragma once
#include <mutex>
#include <string>

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
    // Registra un nuevo trade recibido desde Binance.
    //   symbol        - símbolo del instrumento (no se usa internamente, solo para consistencia)
    //   price, qty    - precio y cantidad del trade
    //   isBuyerMaker  - true si el vendedor fue el agresor (trade "sell")
    void onTrade(const std::string& symbol,
        double price,
        double qty,
        bool isBuyerMaker);

    // Devuelve un snapshot inmutable de las métricas actuales (thread-safe).
    TradeSnapshot snapshot() const;

private:
    mutable std::mutex _mtx;  // Protege acceso a métricas internas
    LastTrade _last;          // Último trade recibido
    double _sumPxQty = 0.0;   // Suma acumulada de precio * cantidad
    double _sumQty = 0.0;   // Suma acumulada de cantidades
};
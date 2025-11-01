#pragma once
#include <string>
#include <memory>
#include <atomic>

#include <ixwebsocket/IXWebSocket.h>

class TradeStats;

// -----------------------------------------------------------------------------
// BinanceTradeStream
// -----------------------------------------------------------------------------
// Mantiene una conexión WebSocket a Binance Spot para recibir el stream
// de trades en vivo de un símbolo concreto (<symbol>@trade).
//
// Responsabilidad:
//  - Parsear cada trade ejecutado en el mercado (precio, cantidad, lado).
//  - Actualizar las métricas en TradeStats:
//      * último trade (price / qty / side)
//      * VWAP de sesión (Σ p*q / Σ q)
//  - No guarda historial local: empuja cada trade directamente a TradeStats.
//
// Ejemplo de uso:
//   auto stats = std::make_shared<TradeStats>();
//   BinanceTradeStream t("btcusdt", stats);
//   t.start();
//   ...
//   t.stop();
//
// Thread-safety:
//  - _running es atómico para evitar doble start/stop.
//  - TradeStats internamente ya se protege con su propio mutex.
// -----------------------------------------------------------------------------
class BinanceTradeStream {
public:
    // symbolLower debe venir en minúsculas (ej. "btcusdt")
    BinanceTradeStream(const std::string& symbolLower,
        std::shared_ptr<TradeStats> tradeStats);

    // Abre la conexión WebSocket y comienza a recibir eventos de trade.
    // Es idempotente: si ya estaba corriendo, no hace nada.
    void start();

    // Cierra la conexión WebSocket.
    // Es idempotente: si ya estaba detenido, no hace nada.
    void stop();

private:
    // Símbolo en minúsculas (ej "btcusdt")
    std::string _symbolLower;

    // Versión en mayúsculas (ej "BTCUSDT"), útil para logs o llamadas REST si hiciera falta
    std::string _symbolUpper;

    // Donde se acumulan las métricas del símbolo:
    //  último trade, VWAP sesión, lado agresor, etc.
    std::shared_ptr<TradeStats> _tradeStats;

    // Socket WebSocket hacia Binance
    ix::WebSocket _ws;

    // Estado de ejecución del stream (true = activo)
    std::atomic<bool> _running{ false };
};
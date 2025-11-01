#pragma once
#include <string>
#include <memory>
#include <cstdint>

class OrderBook;

// -----------------------------------------------------------------------------
// BinanceRestClient
// -----------------------------------------------------------------------------
// Cliente REST mínimo para el endpoint público de Binance Spot.
// Se utiliza principalmente para obtener snapshots iniciales del libro
// de órdenes (nivel 2) antes de comenzar la sincronización vía WebSocket.
//
// Ejemplo de uso:
//   BinanceRestClient client;
//   uint64_t lastId = 0;
//   client.loadInitialBookSnapshot("btcusdt", orderBook, 10, lastId);
//
// Dependencias:
//   - cpr (HTTP client)
//   - nlohmann::json (parser JSON)
// -----------------------------------------------------------------------------
class BinanceRestClient {
public:
    BinanceRestClient();

    // -------------------------------------------------------------------------
    // loadInitialBookSnapshot
    // -------------------------------------------------------------------------
    // Descarga un snapshot REST de /api/v3/depth para un símbolo dado.
    //
    // Parámetros:
    //  - symbolLowerCase: símbolo en minúsculas (ej: "btcusdt").
    //  - orderBook: instancia compartida del OrderBook a llenar.
    //  - limit: cantidad máxima de niveles a solicitar (10, 100, 500, etc.).
    //  - outLastUpdateId: salida del campo "lastUpdateId" del snapshot.
    //
    // Retorna:
    //  - true si se completó correctamente.
    //  - false si hubo error HTTP, JSON o datos incompletos.
    // -------------------------------------------------------------------------
    bool loadInitialBookSnapshot(const std::string& symbolLowerCase,
        std::shared_ptr<OrderBook> orderBook,
        int limit,
        uint64_t& outLastUpdateId);
};
#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <atomic>
#include <memory>

#include <ixwebsocket/IXWebSocket.h>
#include "OrderBook.h"  // Incluye definición de DepthUpdate

// -----------------------------------------------------------------------------
// BinanceDepthStream
// -----------------------------------------------------------------------------
// Componente responsable de mantener una conexión WebSocket abierta con Binance
// para recibir actualizaciones incrementales del libro de órdenes (Depth Stream).
//
// - Escucha mensajes tipo <symbol>@depth@500ms.
// - Cada mensaje contiene los cambios en los niveles de precios ("bids" y "asks").
// - Los mensajes se transforman en estructuras DepthUpdate y se encolan de forma
//   segura para ser consumidas por el BookSyncWorker.
//
// Ejemplo:
//   BinanceDepthStream stream("btcusdt");
//   stream.start();
//   auto updates = stream.drainUpdates();  // devuelve las actualizaciones acumuladas
//
// Thread-safety:
//   - La cola interna está protegida por un std::mutex.
//   - La bandera _running se maneja con std::atomic.
// -----------------------------------------------------------------------------
class BinanceDepthStream {
public:
    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    // symbolLower debe ser el símbolo en minúsculas, ej: "btcusdt".
    explicit BinanceDepthStream(const std::string& symbolLower);

    // -------------------------------------------------------------------------
    // start
    // -------------------------------------------------------------------------
    // Inicia la conexión WebSocket con Binance:
    //   wss://stream.binance.com:9443/ws/<symbol>@depth@500ms
    //
    // Si ya está ejecutándose (_running == true), no hace nada.
    // -------------------------------------------------------------------------
    void start();

    // -------------------------------------------------------------------------
    // stop
    // -------------------------------------------------------------------------
    // Cierra la conexión WebSocket y detiene el stream.
    // Si ya está detenido, no hace nada.
    // -------------------------------------------------------------------------
    void stop();

    // -------------------------------------------------------------------------
    // drainUpdates
    // -------------------------------------------------------------------------
    // Devuelve todas las actualizaciones acumuladas (DepthUpdate) y vacía la cola.
    //
    // Se utiliza desde BookSyncWorker para procesar los mensajes recibidos.
    // -------------------------------------------------------------------------
    std::deque<DepthUpdate> drainUpdates();

private:
    // Símbolo en minúsculas (ej: "btcusdt")
    std::string _symbolLower;

    // Versión en mayúsculas (ej: "BTCUSDT"), útil para logs o REST
    std::string _symbolUpper;

    // Conexión WebSocket activa hacia Binance
    ix::WebSocket _ws;

    // Estado de ejecución del stream
    std::atomic<bool> _running{ false };

    // Mutex que protege el acceso concurrente a la cola de updates
    std::mutex _mtxQueue;

    // Cola de actualizaciones pendientes de procesar
    std::deque<DepthUpdate> _queue;
};
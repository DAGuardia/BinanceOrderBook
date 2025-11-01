#pragma once
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <deque>

#include "OrderBook.h"
#include "BinanceRestClient.h"
#include "BinanceDepthStream.h"

// BookSyncWorker
//
// Responsabilidad:
// - Mantener un OrderBook sincronizado en tiempo real para un símbolo.
// - Flujo:
//   1. Cargar snapshot inicial vía REST (depth limit=10), guardando lastUpdateId.
//   2. Conectarse al WS de profundidad (<symbol>@depth@500ms).
//   3. Reproducir las actualizaciones del WS hasta engancharse con el snapshot.
//   4. Luego aplicar updates incrementales en orden asegurando continuidad.
//   5. Si se detecta un gap en el orden esperado -> resync completo.
//
// Threading:
// - start() lanza un thread interno (_workerThread).
// - stop() pide shutdown ordenado.
// - Es thread-safe respecto al ciclo de vida (start una vez, stop una vez).
//
class BookSyncWorker {
public:
    BookSyncWorker(const std::string& normalizedSymbol,
        std::shared_ptr<OrderBook> orderBook,
        BinanceRestClient* restClient);

    // Inicia el proceso de sync (REST snapshot + WS depth + loop interno)
    void start();

    // Detiene el loop y cierra el WS
    void stop();

private:
    // Hilo principal del worker que:
    // - drena updates del WebSocket
    // - intenta sincronizar / mantener continuidad
    void run();

    // Procesa un batch de DepthUpdate:
    // - Si no estamos sincronizados aún (_isSynchronized == false):
    //     * descarta updates viejos
    //     * busca el primer update que cubra snapshotLastUpdateId+1
    //     * aplica en orden verificando continuidad
    //     * marca el libro como sincronizado
    //
    // - Si ya estamos sincronizados:
    //     * exige continuidad estricta con _lastAppliedUpdateId+1
    //     * si hay gap -> resync con snapshot REST otra vez
    void processBatch(std::deque<DepthUpdate>& pendingUpdates);

private:
    // Símbolo en minúsculas (ej "btcusdt")
    std::string _symbol;

    // Libro de órdenes L2 asociado a este símbolo
    std::shared_ptr<OrderBook> _orderBook;

    // Cliente REST para snapshot/resync
    BinanceRestClient* _restClient;

    // Stream WS de profundidad (depth updates @500ms)
    BinanceDepthStream _depthStream;

    // Thread que corre run()
    std::thread _workerThread;

    // Indica si el worker está activo
    std::atomic<bool> _isRunning{ false };

    // Indica si el libro ya está alineado entre snapshot REST y updates WS
    std::atomic<bool> _isSynchronized{ false };

    // lastUpdateId del snapshot inicial que bajamos por REST
    uint64_t _snapshotLastUpdateId = 0;

    // Último lastUpdateId que aplicamos con éxito sobre el libro
    uint64_t _lastAppliedUpdateId = 0;
};
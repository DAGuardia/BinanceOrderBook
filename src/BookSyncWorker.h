#pragma once
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <deque>
#include <cstdint>

#include "OrderBook.h"
#include "BinanceRestClient.h"
#include "BinanceDepthStream.h"

// BookSyncWorker
//
// Responsabilidad:
// - Mantener un OrderBook sincronizado en tiempo real para un símbolo.
// - Flujo correcto Binance:
//   1. Abrir el WS de profundidad (<symbol>@depth@500ms) y empezar a bufferizar updates.
//   2. Bajar snapshot inicial vía REST (depth limit=10), guardar lastUpdateId.
//   3. Reproducir desde el buffer hasta enganchar con el snapshot:
//        Buscar el primer update cuyo rango [U,u] cubra snapshotLastUpdateId+1,
//        aplicar en orden verificando continuidad.
//   4. A partir de ahí aplicar incrementales asegurando continuidad estricta.
//   5. Si hay gap -> resync: volver a bajar snapshot y marcar _isSynchronized=false.
// 
// Threading:
// - start() lanza el WS, baja snapshot y después crea el thread interno (_workerThread).
// - run() drena updates en loop y mantiene el libro vivo.
// - stop() apaga todo limpio.
//
class BookSyncWorker {
public:
    BookSyncWorker(const std::string& normalizedSymbol,
        std::shared_ptr<OrderBook> orderBook,
        BinanceRestClient* restClient);

    // Inicia el proceso de sync (WS primero, luego snapshot REST, luego loop interno)
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
    //     * descartar updates viejos (u <= snapshotLastUpdateId)
    //     * buscar primer update que cubra snapshotLastUpdateId+1
    //     * aplicar todos en orden verificando continuidad estrica (prev.u+1 == curr.U)
    //     * marcar sincronizado
    //
    // - Si ya estamos sincronizados:
    //     * exigir continuidad exacta con _lastAppliedUpdateId+1
    //     * si hay gap -> resync (nuevo snapshot REST, marcar _isSynchronized=false)
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

    // lastUpdateId del snapshot inicial o del último resync
    uint64_t _snapshotLastUpdateId = 0;

    // Último lastUpdateId que aplicamos con éxito sobre el libro
    uint64_t _lastAppliedUpdateId = 0;
};

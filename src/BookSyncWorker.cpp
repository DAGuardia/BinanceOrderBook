#include "BookSyncWorker.h"
#include <iostream>
#include <chrono>
#include <thread>

BookSyncWorker::BookSyncWorker(const std::string& normalizedSymbol,
    std::shared_ptr<OrderBook> orderBook,
    BinanceRestClient* restClient)
    : _symbol(normalizedSymbol)
    , _orderBook(std::move(orderBook))
    , _restClient(restClient)
    , _depthStream(normalizedSymbol)
{
}

void BookSyncWorker::start() {
    if (_isRunning.exchange(true)) {
        // ya estaba corriendo, no lanzar de nuevo
        return;
    }

    uint64_t snapshotLastUpdateId = 0;

    // 1. snapshot inicial del libro vía REST
    bool snapshotLoaded = _restClient->loadInitialBookSnapshot(
        _symbol,
        _orderBook,
        /*limit*/ 10,
        snapshotLastUpdateId
    );

    _snapshotLastUpdateId = snapshotLastUpdateId;
    _lastAppliedUpdateId = 0;
    _isSynchronized = false;

    if (!snapshotLoaded) {
        std::cerr << "[BookSync] WARNING: no se pudo obtener snapshot inicial para "
            << _symbol << "\n";
    }

    // 2. iniciar recepción de depth updates por WebSocket
    _depthStream.start();

    // 3. lanzar el hilo de sincronización / mantenimiento
    _workerThread = std::thread(&BookSyncWorker::run, this);
}

void BookSyncWorker::stop() {
    if (!_isRunning.exchange(false)) {
        // ya estaba detenido
        return;
    }

    // cerrar el stream de depth
    _depthStream.stop();

    // esperar el hilo interno
    if (_workerThread.joinable()) {
        _workerThread.join();
    }

    std::cerr << "[BookSync] Worker detenido para " << _symbol << "\n";
}

void BookSyncWorker::processBatch(std::deque<DepthUpdate>& pendingUpdates) {
    // ========================================================
    // FASE A: todavía NO estamos sincronizados
    // ========================================================
    if (!_isSynchronized) {
        const uint64_t requiredFirstUpdate = _snapshotLastUpdateId + 1;

        // A.1 Descartar updates viejos (anteriores o iguales al snapshot)
        while (!pendingUpdates.empty() &&
            pendingUpdates.front().lastUpdateId <= _snapshotLastUpdateId)
        {
            pendingUpdates.pop_front();
        }

        // A.2 Encontrar el primer bloque de depth que "enganche" con el snapshot,
        //     es decir, cuyo rango [firstUpdateId, lastUpdateId] cubra requiredFirstUpdate
        size_t startIndex = 0;
        bool foundStartingPoint = false;

        for (size_t i = 0; i < pendingUpdates.size(); ++i) {
            const auto& update = pendingUpdates[i];
            if (update.firstUpdateId <= requiredFirstUpdate &&
                requiredFirstUpdate <= update.lastUpdateId)
            {
                startIndex = i;
                foundStartingPoint = true;
                break;
            }
        }

        if (!foundStartingPoint) {
            // Todavía no tenemos el bloque correcto para alinear con snapshot.
            // Esperamos más mensajes del WS en la próxima iteración.
            return;
        }

        // A.3 Aplicar desde startIndex hasta el final, verificando continuidad
        uint64_t lastAppliedInThisPass = 0;

        for (size_t i = startIndex; i < pendingUpdates.size(); ++i) {
            const auto& update = pendingUpdates[i];

            if (i > startIndex) {
                // Para todos menos el primero, Binance exige continuidad estricta:
                // el siguiente bloque debe empezar EXACTAMENTE en lastAppliedInThisPass + 1
                const uint64_t expectedNextFirst = lastAppliedInThisPass + 1;
                if (update.firstUpdateId != expectedNextFirst) {
                    std::cerr << "[BookSync] GAP inicial en " << _symbol
                        << " (esperado " << expectedNextFirst
                        << ", recibido [" << update.firstUpdateId
                        << "," << update.lastUpdateId << "])\n";
                    // Abortamos sync inicial. En el próximo loop probamos de nuevo.
                    return;
                }
            }

            // Aplicar el delta de profundidad al order book
            _orderBook->applyDepthDelta(update);
            lastAppliedInThisPass = update.lastUpdateId;
        }

        // A.4 Marcamos libro como sincronizado
        _lastAppliedUpdateId = lastAppliedInThisPass;
        _isSynchronized = true;

        // A.5 Limpiamos todas las actualizaciones que acabamos de reproducir
        pendingUpdates.clear();
        return;
    }

    // ========================================================
    // FASE B: ya estamos sincronizados, aplicar incremental
    // ========================================================
    while (!pendingUpdates.empty()) {
        DepthUpdate update = pendingUpdates.front();
        pendingUpdates.pop_front();

        const uint64_t expectedFirstUpdateId = _lastAppliedUpdateId + 1;
        if (update.firstUpdateId != expectedFirstUpdateId) {
            // Detectamos un gap → necesitamos resincronizar
            std::cerr << "[BookSync] GAP en runtime para " << _symbol
                << " (esperado " << expectedFirstUpdateId
                << ", recibido [" << update.firstUpdateId
                << "," << update.lastUpdateId << "]) -> resync\n";

            uint64_t newSnapshotLastUpdateId = 0;
            bool snapshotReloaded = _restClient->loadInitialBookSnapshot(
                _symbol,
                _orderBook,
                /*limit*/ 10,
                newSnapshotLastUpdateId
            );

            if (!snapshotReloaded) {
                std::cerr << "[BookSync] ERROR: no se pudo resincronizar snapshot para "
                    << _symbol << "\n";
            }

            _snapshotLastUpdateId = newSnapshotLastUpdateId;
            _lastAppliedUpdateId = 0;
            _isSynchronized = false;

            // salimos acá; en la próxima iteración intentaremos reenganchar
            return;
        }

        // Continuidad correcta → aplicar incremental
        _orderBook->applyDepthDelta(update);
        _lastAppliedUpdateId = update.lastUpdateId;
    }
}

void BookSyncWorker::run() {
    using namespace std::chrono_literals;

    while (_isRunning) {
        // Tomar todos los depth updates acumulados del stream WS
        auto updatesBatch = _depthStream.drainUpdates();

        if (!updatesBatch.empty()) {
            processBatch(updatesBatch);
        }

        // Pequeño sleep para no quemar CPU (20ms ~ 50Hz)
        std::this_thread::sleep_for(20ms);
    }
}

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

    // ----------------------------------------------------
    // 1. Primero arrancamos el WebSocket de depth.
    //    Esto empieza a bufferizar updates en _depthStream.
    //    NO tocamos el libro todavía.
    // ----------------------------------------------------
    _depthStream.start();

    // ----------------------------------------------------
    // 2. Ahora pedimos snapshot REST inicial.
    //    Guardamos snapshotLastUpdateId y aplicamos snapshot al OrderBook.
    // ----------------------------------------------------
    uint64_t snapshotLastUpdateId = 0;
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
        // Seguimos igual: el run() va a intentar nuevamente si hacemos resync más adelante.
    }

    // ----------------------------------------------------
    // 3. Lanzamos el hilo de mantenimiento/sincronización.
    //    Este hilo va a:
    //      - drenar updates del WS
    //      - intentar enganchar snapshot+buffer
    //      - luego mantener continuidad
    // ----------------------------------------------------
    _workerThread = std::thread(&BookSyncWorker::run, this);
}

void BookSyncWorker::stop() {
    if (!_isRunning.exchange(false)) {
        // ya estaba detenido
        return;
    }

    // cerramos el stream de depth primero
    _depthStream.stop();

    // esperamos el hilo interno
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
        // Necesitamos enganchar snapshot (que tiene _snapshotLastUpdateId)
        // con el buffer de updates que capturamos desde que abrimos el WS.
        //
        // Pasos Binance:
        // 1. descartar updates con u <= snapshotLastUpdateId
        // 2. buscar primer update con U <= snapshotLastUpdateId+1 <= u
        // 3. aplicar ese y todos los que siguen en orden estricto

        const uint64_t requiredFirstUpdate = _snapshotLastUpdateId + 1;

        // A.1 Descartar updates viejos que ya están cubiertos por el snapshot REST
        while (!pendingUpdates.empty() &&
            pendingUpdates.front().lastUpdateId <= _snapshotLastUpdateId)
        {
            pendingUpdates.pop_front();
        }

        // A.2 Buscar el primer bloque que "enganche" con requiredFirstUpdate
        size_t startIndex = 0;
        bool foundStartingPoint = false;

        for (size_t i = 0; i < pendingUpdates.size(); ++i) {
            const auto& update = pendingUpdates[i];
            // chequeo: U <= requiredFirstUpdate <= u
            if (update.firstUpdateId <= requiredFirstUpdate &&
                requiredFirstUpdate <= update.lastUpdateId)
            {
                startIndex = i;
                foundStartingPoint = true;
                break;
            }
        }

        if (!foundStartingPoint) {
            // Todavía no tenemos el bloque que cubre snapshotLastUpdateId+1.
            // No marcamos sync todavía. Esperamos más WS updates.
            return;
        }

        // A.3 Aplicar en orden estricto desde startIndex
        uint64_t lastAppliedInThisPass = 0;

        for (size_t i = startIndex; i < pendingUpdates.size(); ++i) {
            const auto& update = pendingUpdates[i];

            if (i > startIndex) {
                // Para los siguientes bloques:
                // Binance exige continuidad exacta:
                // next.firstUpdateId == (prev.lastUpdateId + 1)
                const uint64_t expectedNextFirst = lastAppliedInThisPass + 1;
                if (update.firstUpdateId != expectedNextFirst) {
                    std::cerr << "[BookSync] GAP inicial en " << _symbol
                        << " (esperado " << expectedNextFirst
                        << ", recibido [" << update.firstUpdateId
                        << "," << update.lastUpdateId << "])\n";
                    // Abortamos la fase de sync inicial, no tocamos más.
                    // En el próximo loop intentamos otra vez.
                    return;
                }
            }

            // Aplicar delta al order book
            _orderBook->applyDepthDelta(update);
            lastAppliedInThisPass = update.lastUpdateId;
        }

        // A.4 Marcamos el libro como sincronizado
        _lastAppliedUpdateId = lastAppliedInThisPass;
        _isSynchronized = true;

        // A.5 Limpiamos las actualizaciones que ya aplicamos
        pendingUpdates.clear();
        return;
    }

    // ========================================================
    // FASE B: ya estamos sincronizados, aplicar incremental en vivo
    // ========================================================
    while (!pendingUpdates.empty()) {
        DepthUpdate update = pendingUpdates.front();
        pendingUpdates.pop_front();

        // Binance dice:
        // después de sincronizar, cada update debe arrancar EXACTAMENTE en _lastAppliedUpdateId+1
        const uint64_t expectedFirstUpdateId = _lastAppliedUpdateId + 1;

        ////test en debug para romper sync: ok
        //if (_symbol == "btcusdt") {
        //    update.firstUpdateId += 999999;
        //}

        if (update.firstUpdateId != expectedFirstUpdateId) {
            // Detectamos gap → resync
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

            // Actualizamos estado interno para volver a fase A
            _snapshotLastUpdateId = newSnapshotLastUpdateId;
            _lastAppliedUpdateId = 0;
            _isSynchronized = false;

            // No aplicamos este update ahora. En el próximo loop, en fase A,
            // vamos a intentar reenganchar snapshot+buffer otra vez.
            return;
        }

        // Continuidad correcta → aplicar incremental inmediato
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

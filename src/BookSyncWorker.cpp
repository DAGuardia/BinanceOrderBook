#include "BookSyncWorker.h"
#include <iostream>
#include <chrono>
#include <thread>
using namespace std::chrono_literals;

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
        // 1) descartar u <= snapshotLastUpdateId
        // 2) encontrar primer bloque con U <= L+1 <= u
        // 3) aplicar desde ahí en adelante con continuidad estricta

        const uint64_t requiredFirstUpdate = _snapshotLastUpdateId + 1;

        // A.1 Descartar del frente lo que YA está cubierto por el snapshot REST
        while (!pendingUpdates.empty() &&
            pendingUpdates.front().lastUpdateId <= _snapshotLastUpdateId)
        {
            pendingUpdates.pop_front();
        }

        if (pendingUpdates.empty()) {
            // Todavía no hay nada útil para enganchar
            return;
        }

        // A.2 Si el backlog ya está ADELANTADO respecto al snapshot,
        //     significa que perdimos el "puente" -> resnapshot inmediato
        if (pendingUpdates.front().firstUpdateId > requiredFirstUpdate) {
            uint64_t newSnapshotLastUpdateId = 0;
            bool snapshotReloaded = _restClient->loadInitialBookSnapshot(
                _symbol,
                _orderBook,
                /*limit*/ 10,
                newSnapshotLastUpdateId
            );

            if (!snapshotReloaded) {
                std::cerr << "[BookSync] ERROR: resnapshot fallido en fase A para "
                    << _symbol << "\n";
                return;
            }

            _snapshotLastUpdateId = newSnapshotLastUpdateId;
            // NO vaciamos pendingUpdates: intentaremos enganchar con este backlog en la próxima vuelta
            return;
        }

        // A.3 Buscar el primer bloque que "enganche" con requiredFirstUpdate (U <= L+1 <= u)
        size_t startIndex = SIZE_MAX;
        for (size_t i = 0; i < pendingUpdates.size(); ++i) {
            const auto& update = pendingUpdates[i];
            if (update.firstUpdateId <= requiredFirstUpdate &&
                requiredFirstUpdate <= update.lastUpdateId)
            {
                startIndex = i;
                break;
            }
        }

        if (startIndex == SIZE_MAX) {
            // Aún no llegó el bloque puente; NO descartamos backlog
            return;
        }

        // A.4 Eliminar lo anterior a startIndex (ya no sirve)
        for (size_t i = 0; i < startIndex; ++i) {
            pendingUpdates.pop_front();
        }

        // A.5 Aplicar desde el nuevo frente con continuidad estricta,
        //     consumiendo del backlog (pop_front) a medida que aplicamos
        uint64_t lastAppliedInThisPass = _snapshotLastUpdateId;

        while (!pendingUpdates.empty()) {
            const auto& update = pendingUpdates.front();
            const uint64_t expectedNextFirst = lastAppliedInThisPass + 1;

            // Permitimos que el PRIMER bloque "puente" arranque dentro del rango (U <= L+1 <= u),
            // pero los siguientes DEBEN tener continuidad exacta.
            if (lastAppliedInThisPass != _snapshotLastUpdateId) {
                // Ya aplicamos al menos un bloque en esta pasada: exigir continuidad
                if (update.firstUpdateId != expectedNextFirst) {
                    std::cerr << "[BookSync] GAP inicial en " << _symbol
                        << " (esperado " << expectedNextFirst
                        << ", recibido [" << update.firstUpdateId
                        << "," << update.lastUpdateId << "])\n";
                    // No consumimos este bloque; dejamos backlog intacto para reintentar
                    return;
                }
            }
            else {
                // Es el primer bloque tras el snapshot: debe cubrir requiredFirstUpdate
                if (!(update.firstUpdateId <= requiredFirstUpdate &&
                    requiredFirstUpdate <= update.lastUpdateId))
                {
                    // Algo cambió entre que recortamos y ahora; mejor volver a intentar
                    return;
                }
            }

            _orderBook->applyDepthDelta(update);
            lastAppliedInThisPass = update.lastUpdateId;
            pendingUpdates.pop_front(); // consumido
        }

        // A.6 Sincronizado
        _lastAppliedUpdateId = lastAppliedInThisPass;
        _isSynchronized = true;
        return;
    }

    // ========================================================
    // FASE B: ya estamos sincronizados, aplicar incremental en vivo
    // ========================================================
    while (!pendingUpdates.empty()) {
        const auto& update = pendingUpdates.front();

        // Después de sincronizar, cada update debe arrancar EXACTAMENTE en _lastAppliedUpdateId+1
        const uint64_t expectedFirstUpdateId = _lastAppliedUpdateId + 1;

        if (update.firstUpdateId != expectedFirstUpdateId) {
            // Detectamos gap → resync (sin tirar backlog)
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

            // No consumimos este update; dejamos backlog para reenganchar en fase A
            return;
        }

        // Continuidad correcta → aplicar y consumir
        _orderBook->applyDepthDelta(update);
        _lastAppliedUpdateId = update.lastUpdateId;
        pendingUpdates.pop_front();
    }
}

void BookSyncWorker::run() {
    using namespace std::chrono_literals;

    while (_isRunning) {
        auto newUpdates = _depthStream.drainUpdates();

        // ⬇️ Append al backlog persistente
        if (!newUpdates.empty()) {
            _backlog.insert(_backlog.end(),
                std::make_move_iterator(newUpdates.begin()),
                std::make_move_iterator(newUpdates.end()));
        }

        if (!_backlog.empty()) {
            processBatch(_backlog); // ahora processBatch trabaja SOBRE el backlog
        }        

        // Pequeño sleep para no quemar CPU (20ms ~ 50Hz)
        std::this_thread::sleep_for(20ms);
    }
}

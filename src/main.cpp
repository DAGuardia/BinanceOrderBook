#include <iostream>
#include <unordered_map>
#include <vector>
#include <memory>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cctype>

#include "Args.h"
#include "OrderBook.h"
#include "TradeStats.h"
#include "Publisher.h"
#include "BinanceRestClient.h"
#include "BinanceTradeStream.h"
#include "BookSyncWorker.h"

static std::atomic<bool> g_running(true);

void signalHandler(int) {
    g_running = false;
}int main(int argc, char** argv) {
    try {
        // Parsear argumentos de línea de comando
        ProgramArgs programArgs = parseArgs(argc, argv);

        // Diccionarios principales: libros y estadísticas por símbolo
        std::unordered_map<std::string, std::shared_ptr<OrderBook>> orderBooks;
        std::unordered_map<std::string, std::shared_ptr<TradeStats>> tradeStatsBySymbol;

        // Hilos y streams en ejecución
        std::vector<std::unique_ptr<BookSyncWorker>> orderBookWorkers;
        std::vector<std::unique_ptr<BinanceTradeStream>> tradeStreamWorkers;

        // Cliente REST de Binance (para snapshots y resync)
        BinanceRestClient binanceRestClient;

        // Inicializar infraestructura por cada símbolo solicitado
        for (auto& symbol : programArgs.symbols) {
            // Convertir el símbolo a minúsculas (ej: BTCUSDT → btcusdt)
            std::string normalizedSymbol;
            normalizedSymbol.reserve(symbol.size());
            for (char c : symbol)
                normalizedSymbol.push_back(std::tolower(static_cast<unsigned char>(c)));

            // Crear estructuras compartidas
            auto orderBookPtr = std::make_shared<OrderBook>(normalizedSymbol);
            auto tradeStatsPtr = std::make_shared<TradeStats>();

            orderBooks[normalizedSymbol] = orderBookPtr;
            tradeStatsBySymbol[normalizedSymbol] = tradeStatsPtr;

            // Mantener el libro de órdenes sincronizado (snapshot + WS depth + resync)
            auto orderBookWorker = std::make_unique<BookSyncWorker>(
                normalizedSymbol,
                orderBookPtr,
                &binanceRestClient
            );
            orderBookWorker->start();
            orderBookWorkers.push_back(std::move(orderBookWorker));

            // Escuchar el stream de trades en tiempo real (para VWAP, último trade, etc.)
            auto tradeStreamWorker = std::make_unique<BinanceTradeStream>(
                normalizedSymbol,
                tradeStatsPtr
            );
            tradeStreamWorker->start();
            tradeStreamWorkers.push_back(std::move(tradeStreamWorker));
        }

        // Publisher: genera el CSV o salida de datos
        Publisher publisher(orderBooks, tradeStatsBySymbol, programArgs.topN, programArgs.logPath);
        publisher.start();

        // Manejar señales de cierre (Ctrl+C o kill)
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Loop principal: mantener el proceso vivo hasta señal de salida
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // Detener hilos y liberar recursos ordenadamente
        publisher.stop();

        for (auto& tradeStream : tradeStreamWorkers)
            tradeStream->stop();

        for (auto& worker : orderBookWorkers)
            worker->stop();

        std::cerr << "Apagado limpio.\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "FATAL: " << ex.what() << "\n";
        return 1;
    }
}
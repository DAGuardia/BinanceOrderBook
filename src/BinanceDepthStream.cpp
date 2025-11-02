#include "BinanceDepthStream.h"

#include <iostream>
#include <cctype>
#include <nlohmann/json.hpp>

BinanceDepthStream::BinanceDepthStream(const std::string& symbolLower)
    : _symbolLower(symbolLower)
{
    // Precalculamos el símbolo en mayúsculas para logging u otras llamadas REST.
    _symbolUpper.reserve(_symbolLower.size());
    for (char c : _symbolLower) {
        _symbolUpper.push_back(std::toupper(static_cast<unsigned char>(c)));
    }
}

void BinanceDepthStream::start() {
    if (_running.exchange(true)) {
        // Ya estaba ejecutándose, no se vuelve a iniciar
        return;
    }

    // Construimos la URL del stream de profundidad (actualizaciones cada 500ms)
    // Ejemplo: wss://stream.binance.com:9443/ws/btcusdt@depth@500ms
    std::string wsUrl = "wss://stream.binance.com:9443/ws/" +
        _symbolLower +
        "@depth@500ms";

    _ws.setUrl(wsUrl);

    {
        #ifdef _WIN32
                ix::SocketTLSOptions tlsOptions;
                _ws.setTLSOptions(tlsOptions);
        #else
                ix::SocketTLSOptions tlsOptions;
                tlsOptions.caFile = "/etc/ssl/certs/ca-certificates.crt";
                _ws.setTLSOptions(tlsOptions);
        #endif
    }



    _ws.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr& msg)
        {
            using nlohmann::json;

            switch (msg->type) {
            case ix::WebSocketMessageType::Open:
                std::cerr << "[DepthStream] Conectado a " << _symbolLower << "\n";
                return;

            case ix::WebSocketMessageType::Close:
                std::cerr << "[DepthStream] Conexion cerrada para " << _symbolLower << "\n";
                return;

            case ix::WebSocketMessageType::Error:
                std::cerr << "[DepthStream] ERROR en " << _symbolLower
                    << ": " << msg->errorInfo.reason << "\n";
                return;

            case ix::WebSocketMessageType::Message:
                break;

            default:
                return;
            }

            try {
                json jsonMsg = json::parse(msg->str);

                // Binance depth updates incluyen U (firstUpdateId), u (lastUpdateId)
                if (!jsonMsg.contains("U") || !jsonMsg.contains("u"))
                    return;

                DepthUpdate depthUpdate;
                depthUpdate.firstUpdateId = jsonMsg["U"].get<uint64_t>();
                depthUpdate.lastUpdateId = jsonMsg["u"].get<uint64_t>();

                // Procesar bids (compras)
                if (jsonMsg.contains("b")) {
                    for (auto& level : jsonMsg["b"]) {
                        if (level.size() < 2) continue;

                        double price = std::stod(level[0].get<std::string>());
                        double quantity = std::stod(level[1].get<std::string>());
                        depthUpdate.bids.emplace_back(price, quantity);
                    }
                }

                // Procesar asks (ventas)
                if (jsonMsg.contains("a")) {
                    for (auto& level : jsonMsg["a"]) {
                        if (level.size() < 2) continue;

                        double price = std::stod(level[0].get<std::string>());
                        double quantity = std::stod(level[1].get<std::string>());
                        depthUpdate.asks.emplace_back(price, quantity);
                    }
                }

                // Encolar update para que el worker lo procese
                {
                    std::lock_guard<std::mutex> lock(_mtxQueue);
                    _queue.push_back(std::move(depthUpdate));
                }
            }
            catch (const std::exception& ex) {
                std::cerr << "[DepthStream] Error al parsear update de "
                    << _symbolLower << ": " << ex.what() << "\n";
            }
        }
    );

    _ws.start();
}

void BinanceDepthStream::stop() {
    if (!_running.exchange(false)) {
        // Ya estaba detenido
        return;
    }

    _ws.stop();
    std::cerr << "[DepthStream] Detenido " << _symbolLower << "\n";
}

std::deque<DepthUpdate> BinanceDepthStream::drainUpdates() {
    std::lock_guard<std::mutex> lock(_mtxQueue);
    std::deque<DepthUpdate> drained;
    drained.swap(_queue);
    return drained;
}
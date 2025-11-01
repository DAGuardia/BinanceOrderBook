#include "BinanceTradeStream.h"
#include "TradeStats.h"

#include <iostream>
#include <cctype>
#include <nlohmann/json.hpp>

BinanceTradeStream::BinanceTradeStream(const std::string& symbolLower,
    std::shared_ptr<TradeStats> tradeStats)
    : _symbolLower(symbolLower)
    , _tradeStats(std::move(tradeStats))
{
    // Precalculamos la versión en mayúsculas (ej: "BTCUSDT")
    _symbolUpper.reserve(_symbolLower.size());
    for (char c : _symbolLower) {
        _symbolUpper.push_back(std::toupper(static_cast<unsigned char>(c)));
    }
}

void BinanceTradeStream::start() {
    // Evitar start() doble
    if (_running.exchange(true)) {
        return;
    }

    // Stream de trades en tiempo real:
    //   wss://stream.binance.com:9443/ws/<symbol>@trade
    //
    // Ejemplo: wss://stream.binance.com:9443/ws/btcusdt@trade
    std::string wsUrl =
        "wss://stream.binance.com:9443/ws/" +
        _symbolLower +
        "@trade";

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
                std::cerr << "[TradeStream] Conectado " << _symbolLower << "\n";
                return;

            case ix::WebSocketMessageType::Close:
                std::cerr << "[TradeStream] Conexión cerrada " << _symbolLower << "\n";
                return;

            case ix::WebSocketMessageType::Error:
                std::cerr << "[TradeStream] ERROR en " << _symbolLower
                    << ": " << msg->errorInfo.reason << "\n";
                return;

            case ix::WebSocketMessageType::Message:
                break;

            default:
                return;
            }

            // Mensaje normal de trade
            try {
                json jsonMsg = json::parse(msg->str);

                // Binance trade event:
                //  "p": precio (string)
                //  "q": cantidad (string)
                //  "m": isBuyerMaker (bool)
                //
                // Convención:
                //   isBuyerMaker = true  → trade lo inició el vendedor (side = "sell")
                //   isBuyerMaker = false → trade lo inició el comprador (side = "buy")
                if (!jsonMsg.contains("p") ||
                    !jsonMsg.contains("q") ||
                    !jsonMsg.contains("m"))
                {
                    return;
                }

                double price = std::stod(jsonMsg["p"].get<std::string>());
                double quantity = std::stod(jsonMsg["q"].get<std::string>());
                bool isBuyerMaker = jsonMsg["m"].get<bool>();

                // Actualizar estadísticas del símbolo (último trade, VWAP sesión, etc.)
                if (_tradeStats) {
                    _tradeStats->onTrade(_symbolLower, price, quantity, isBuyerMaker);
                }
            }
            catch (const std::exception& ex) {
                std::cerr << "[TradeStream] ERROR parseando trade de "
                    << _symbolLower << ": " << ex.what() << "\n";
            }
        }
    );

    _ws.start();
}

void BinanceTradeStream::stop() {
    if (!_running.exchange(false)) {
        // ya estaba detenido
        return;
    }

    _ws.stop();
    std::cerr << "[TradeStream] Detenido " << _symbolLower << "\n";
}
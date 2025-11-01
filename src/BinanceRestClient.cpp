#include "BinanceRestClient.h"
#include "OrderBook.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <iostream>

BinanceRestClient::BinanceRestClient() = default;

// -----------------------------------------------------------------------------
// loadInitialBookSnapshot
// -----------------------------------------------------------------------------
// Realiza una solicitud REST al endpoint /api/v3/depth de Binance para obtener
// un snapshot inicial del libro de órdenes (nivel 2) de un símbolo específico.
//
// Parámetros:
//  - symbolLowerCase: símbolo en minúsculas (ej: "btcusdt").
//  - orderBook: instancia compartida del libro donde se cargará el snapshot.
//  - limit: cantidad máxima de niveles por lado (p. ej. 10, 100, 500).
//  - outLastUpdateId: salida del valor "lastUpdateId" recibido del snapshot.
//
// Retorna:
//  - true si la operación se completó correctamente.
//  - false en caso de error HTTP, parseo o datos inválidos.
//
// Comportamiento:
//  1. Convierte el símbolo a mayúsculas (Binance usa uppercase en REST).
//  2. Solicita el snapshot REST a /api/v3/depth.
//  3. Parsea la respuesta JSON y valida estructura.
//  4. Aplica niveles "bids" y "asks" al libro.
//  5. Retorna el lastUpdateId recibido para sincronización posterior.
//
bool BinanceRestClient::loadInitialBookSnapshot(const std::string& symbolLowerCase,
    std::shared_ptr<OrderBook> orderBook,
    int limit,
    uint64_t& outLastUpdateId)
{
    std::string symbolUpperCase;
    symbolUpperCase.reserve(symbolLowerCase.size());
    for (char c : symbolLowerCase) {
        symbolUpperCase.push_back(std::toupper(static_cast<unsigned char>(c)));
    }

    std::string requestUrl =
        "https://api.binance.com/api/v3/depth?symbol=" +
        symbolUpperCase +
        "&limit=" +
        std::to_string(limit);

    cpr::Response response;

    #ifdef _WIN32
        // En Windows, Schannel ya conoce los CAs del sistema
        response = cpr::Get(
            cpr::Url{ requestUrl }
        );
    #else
        // En Linux dentro del contenedor, decile explícitamente dónde están los certificados raíz
        response = cpr::Get(
            cpr::Url{ requestUrl },
            cpr::Ssl(
                cpr::ssl::CaInfo{ "/etc/ssl/certs/ca-certificates.crt" }
            )
        );
    #endif

    if (response.status_code != 200) {
        std::cerr
            << "[BinanceRestClient] ERROR HTTP " << response.status_code
            << " al solicitar snapshot: " << requestUrl
            << " ; error msg: " << response.error.message
            << "\n";
        return false;
    }


    // Parsear respuesta JSON
    nlohmann::json jsonResponse;
    try {
        jsonResponse = nlohmann::json::parse(response.text);
    }
    catch (const std::exception& ex) {
        std::cerr << "[BinanceRestClient] ERROR al parsear JSON: " << ex.what() << "\n";
        return false;
    }

    // Validar presencia de campo lastUpdateId
    if (!jsonResponse.contains("lastUpdateId")) {
        std::cerr << "[BinanceRestClient] ERROR: respuesta sin 'lastUpdateId' para "
            << symbolUpperCase << "\n";
        return false;
    }

    outLastUpdateId = jsonResponse["lastUpdateId"].get<uint64_t>();

    // Cargar niveles iniciales al OrderBook
    try {
        // ------------------------------
        // Bids (compras)
        // ------------------------------
        for (auto& level : jsonResponse["bids"]) {
            if (level.size() < 2) continue;

            double price = std::stod(level[0].get<std::string>());
            double quantity = std::stod(level[1].get<std::string>());

            orderBook->applyBidLevel(price, quantity);
        }

        // ------------------------------
        // Asks (ventas)
        // ------------------------------
        for (auto& level : jsonResponse["asks"]) {
            if (level.size() < 2) continue;

            double price = std::stod(level[0].get<std::string>());
            double quantity = std::stod(level[1].get<std::string>());

            orderBook->applyAskLevel(price, quantity);
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "[BinanceRestClient] ERROR cargando niveles del snapshot: "
            << ex.what() << "\n";
        return false;
    }

    return true;
}
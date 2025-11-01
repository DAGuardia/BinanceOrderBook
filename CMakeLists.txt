cmake_minimum_required(VERSION 3.21)
project(BinanceOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paquetes
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)

if (WIN32)
    # en Windows usás WinHTTP / Schannel, etc.
else()
    find_package(Threads REQUIRED)
    find_package(OpenSSL REQUIRED) # <- esto es clave
endif()

# Ejecutable
add_executable(BinanceOrderBook
    src/main.cpp
    src/Args.h
    src/Args.cpp
    src/OrderBook.h
    src/OrderBook.cpp
    src/TradeStats.h
    src/TradeStats.cpp
    src/Publisher.h
    src/Publisher.cpp
    src/Utils.h
    src/BinanceRestClient.h
    src/BinanceRestClient.cpp
    src/BinanceTradeStream.h
    src/BinanceTradeStream.cpp
    src/BinanceDepthStream.h
    src/BinanceDepthStream.cpp
    src/BookSyncWorker.h
    src/BookSyncWorker.cpp
)

# Linkeo común
target_link_libraries(BinanceOrderBook
    PRIVATE
        nlohmann_json::nlohmann_json
        cpr::cpr
        ixwebsocket::ixwebsocket
)

# Dependencias específicas por plataforma
if (WIN32)
    target_link_libraries(BinanceOrderBook
        PRIVATE
            bcrypt
            ws2_32
            crypt32
    )
else()
    target_link_libraries(BinanceOrderBook
        PRIVATE
            Threads::Threads
            OpenSSL::SSL
            OpenSSL::Crypto
    )
endif()
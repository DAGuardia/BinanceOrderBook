Binance Order Book Aggregator

Proyecto en C++ que consume en tiempo real los streams de profundidad y trades de Binance, mantiene libros sincronizados por símbolo y publica snapshots periódicos en CSV con métricas agregadas.

--------------------------------------------------------------------------------

DESCRIPCION GENERAL

El sistema mantiene libros de órdenes en memoria, actualizados en tiempo real a partir de streams de Binance.
Para cada símbolo (por ejemplo "btcusdt", "ethusdt"), se ejecutan dos hilos:
- DepthStream: escucha actualizaciones "@depth@500ms"
- TradeStream: escucha trades "@trade"

Un BookSyncWorker combina ambas fuentes, aplicando las actualizaciones en el orden correcto según los campos U y u.
Si se detecta un salto (gap) en la secuencia, el sistema se resincroniza automáticamente usando el snapshot REST inicial.

Finalmente, un hilo Publisher toma snapshots consistentes y los exporta en formato CSV cada segundo.

--------------------------------------------------------------------------------

COMPILACION (Windows)

Requisitos:
- Visual Studio 2022 o superior
- CMake 3.20+
- vcpkg (para dependencias)

Dependencias (instalar con vcpkg):
vcpkg install cpr ixwebsocket nlohmann-json

Compilación:
cmake -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

--------------------------------------------------------------------------------
LLAMADAS A BINANCE

REST /api/v3/depth
Me da el snapshot del libro de órdenes con lastUpdateId, bids y asks agregados por precio.
Lo uso como estado inicial.

WS <symbol>@depth@500ms
Me da updates incrementales de libro con rango [U, u].
Los aplico en orden para mantener el order book L2.
Si hay salto en la secuencia → resync automático.

WS <symbol>@trade
Me da cada trade ejecutado (precio p, cantidad q, lado del comprador vía m == true).

Con eso calculo:
último trade
VWAP de sesión
lado (buy/sell)

Con esas 3 fuentes vos armo:
mid, spread, imbalance del book
top N niveles
últimas métricas de ejecución
stream CSV cada segundo por símbolo

--------------------------------------------------------------------------------
EJECUCION

Ejemplo de uso:
BinanceOrderBook.exe --symbols=btcusdt,ethusdt --topN=5 --log=out.csv

Parámetros:
--symbols -> lista de símbolos separados por coma
--topN -> cantidad de niveles a publicar
--log -> archivo CSV de salida (opcional, si no se indica se imprime por consola)

--------------------------------------------------------------------------------

FORMATO DEL CSV

Cada línea representa un snapshot por símbolo:
timestamp,symbol,mid,spread,bestBidPx,bestBidQty,bestAskPx,bestAskQty,topBids,topAsks,lastTradePx,lastTradeQty,lastTradeSide,vwapWin,vwapSession,imbalance

Ejemplo real:
1761963153.309593,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,109579.990000:3.380600|109579.980000:0.000100|...,109580.000000:0.932690|...,109580.860000,0.000050,buy,0.000000,109580.005646,0.789871

Campos destacados:
- mid: (bestAsk + bestBid) / 2
- spread: bestAsk - bestBid
- topBids / topAsks: lista "precio:volumen" separada por "|"
- lastTradeSide: "buy" o "sell" según isBuyerMaker
- vwapSession: VWAP acumulado desde inicio de sesión
- imbalance: profundidad_bid / (profundidad_bid + profundidad_ask)

--------------------------------------------------------------------------------

ARQUITECTURA DE HILOS

Componente              | Rol                                          | Tipo
------------------------|----------------------------------------------|----------------
BinanceDepthStream      | Escucha actualizaciones de profundidad       | Hilo por símbolo
BinanceTradeStream      | Escucha trades ejecutados                    | Hilo por símbolo
BookSyncWorker          | Aplica updates garantizando orden y resincroniza si hay gaps | Hilo por símbolo
Publisher               | Publica snapshots de todos los símbolos en CSV | 1 hilo global

--------------------------------------------------------------------------------

RESINCRONIZACION

El flujo de sincronización cumple las reglas del API de Binance:
1. Cargar snapshot REST "depth?limit=1000"
2. Guardar lastUpdateId
3. Escuchar "@depth@500ms"
4. Descarta updates con u <= lastUpdateId
5. Aplica el primer batch donde U <= lastUpdateId+1 <= u
6. Aplica updates secuenciales garantizando continuidad
7. Si falta un update -> resincroniza desde REST

En runtime, si se detecta salto (U > last_u + 1), se loguea:
[BookSync] GAP runtime btcusdt (last 2134439922, next 2134440025) -> resync

y el worker vuelve a traer snapshot REST.

--------------------------------------------------------------------------------

METRICAS EXPUESTAS

- Top-N niveles de libro
- Precio medio (mid)
- Spread
- Imbalance (profundidad relativa bid/ask)
- Último trade (precio, cantidad, lado)
- VWAP sesión
- VWAP ventana

--------------------------------------------------------------------------------

EJEMPLO DE EJECUCION REAL

[TradeStream] Conectado btcusdt
[DepthStream] Conectado btcusdt
[DepthStream] Conectado ethusdt
[TradeStream] Conectado ethusdt

1761963151.286440,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,...,109580.000000,0.000610,buy,0.000000,109580.000000,0.789871
1761963152.298617,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,...,109579.990000,0.002300,sell,0.000000,109579.995936,0.789871
1761963153.309593,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,...,109580.860000,0.000050,buy,0.000000,109580.005646,0.789871

--------------------------------------------------------------------------------

NOTAS FINALES

- Compatible con múltiples símbolos simultáneos
- Sin data races (std::mutex en OrderBook y TradeStats)
- Publicación atómica de snapshots consistentes
- Diseño modular y extensible para nuevos exchanges

Autor: Damián Guardia
Lenguaje: C++17
Dependencias: CPR, IXWebSocket, nlohmann-json
Licencia: MIT
# Binance Order Book Aggregator

Proyecto en C++17 que consume en tiempo real los streams de profundidad y trades de Binance, mantiene libros sincronizados por símbolo y publica snapshots periódicos en CSV con métricas agregadas.

---

## 🧠 Descripción general

El sistema mantiene un libro de órdenes (order book) en memoria por cada símbolo (por ejemplo `btcusdt`, `ethusdt`), actualizado en tiempo real a partir de streams WebSocket de Binance y un snapshot REST inicial.

Para cada símbolo se lanzan dos hilos independientes:

- **DepthStream**  
  Escucha las actualizaciones de profundidad (`<symbol>@depth@500ms`), que contienen los cambios incrementales del libro (bids/asks con sus secuencias `U` y `u`).

- **TradeStream**  
  Escucha el stream de trades ejecutados (`<symbol>@trade`), cada trade con precio ejecutado, cantidad y lado.

Ambos flujos alimentan un `BookSyncWorker`, que:
1. Aplica las actualizaciones de profundidad en orden garantizado.
2. Reconcilia trades recientes.
3. Se resincroniza automáticamente si detecta gaps o pérdida de continuidad en las secuencias.

Finalmente, un hilo único `Publisher` toma snapshots consistentes de todos los símbolos y los exporta en formato CSV cada segundo (stdout o archivo).

---

## 🔩 Arquitectura de hilos

| Componente             | Rol                                                                 | Tipo de hilo                |
|------------------------|---------------------------------------------------------------------|-----------------------------|
| `BinanceDepthStream`   | Escucha actualizaciones incrementales de libro (`@depth@500ms`).   | 1 hilo por símbolo          |
| `BinanceTradeStream`   | Escucha trades ejecutados (`@trade`).                              | 1 hilo por símbolo          |
| `BookSyncWorker`       | Aplica updates en orden, valida continuidad y resincroniza si hay gaps. | 1 hilo por símbolo     |
| `Publisher`            | Publica snapshots agregados de todos los símbolos en CSV cada ~1s. | 1 hilo global               |

La publicación es atómica: cada snapshot que sale por consola/archivo representa un estado consistente entre book y trades en ese instante.

---

## 🔁 Resincronización y consistencia

El flujo de sincronización sigue las reglas oficiales del order book incremental de Binance:

1. Pedir snapshot inicial:  
   `GET /api/v3/depth?symbol=SYMBOL&limit=N`

2. Guardar `lastUpdateId` del snapshot.

3. Conectarse al stream WebSocket `<symbol>@depth@500ms`.

4. Ignorar todos los mensajes cuya `u` (update final) sea `<= lastUpdateId`.

5. Encontrar el primer mensaje tal que `U <= lastUpdateId+1 <= u`.  
   A partir de ahí, las actualizaciones son válidas para continuar el libro.

6. Aplicar incrementalmente las actualizaciones siguientes en orden.

7. Si en runtime se detecta un salto de secuencia (por ejemplo, `U > last_u + 1`), se loguea algo como:
   ```text
   [BookSync] GAP runtime btcusdt (last 2134439922, next 2134440025) -> resync
   ```
   y se vuelve automáticamente al paso 1 (snapshot REST) para ese símbolo.

Esto asegura que el libro local se mantenga correcto incluso si se pierde algún paquete WS.

---

## 📊 Métricas que publica

Para cada símbolo se calcula y publica continuamente:

- `mid`: (bestAsk + bestBid) / 2
- `spread`: bestAsk - bestBid
- `bestBidPx` / `bestBidQty`
- `bestAskPx` / `bestAskQty`
- `topN` niveles de bid/ask agregados (precio:volumen)
- Último trade ejecutado:
  - `lastTradePx`
  - `lastTradeQty`
  - `lastTradeSide` = `"buy"` o `"sell"` (según `isBuyerMaker`)
- VWAP de ventana corta
- VWAP de sesión completa (desde el arranque del proceso)
- `imbalance`: profundidad_bid / (profundidad_bid + profundidad_ask)

El hilo `Publisher` emite un registro por símbolo por segundo aproximadamente.

---

## 🧾 Formato del CSV

Cada línea es:  
```text
timestamp,
symbol,
mid,
spread,
bestBidPx,bestBidQty,
bestAskPx,bestAskQty,
topBids,
topAsks,
lastTradePx,lastTradeQty,lastTradeSide,
vwapWin,vwapSession,
imbalance
```

Ejemplo real:

```text
1761963153.309593,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,109579.990000:3.380600|109579.980000:0.000100|...,109580.000000:0.932690|...,109580.860000,0.000050,buy,0.000000,109580.005646,0.789871
```

Notas:
- `topBids` y `topAsks` son listas `precio:volumen` separadas por `"|"`.
- `lastTradeSide` puede ser `"buy"` o `"sell"`.
- `vwapSession` es el VWAP acumulado desde que arrancó el proceso.
- `imbalance` mide qué tan cargado está el lado comprador vs vendedor.

---

## 📡 Llamadas a Binance

### REST: snapshot inicial
`GET /api/v3/depth?symbol=BTCUSDT&limit=1000`

- Te da el estado del libro en crudo (`bids`, `asks`) y `lastUpdateId`.
- El sistema lo usa como base inicial de cada símbolo antes de aplicar incrementales.

### WebSocket: profundidad
`<symbol>@depth@500ms`

- Flujo incremental con niveles de precio y rangos `[U, u]`.
- Es la fuente continua para mantener actualizado el order book L2/Top N.

### WebSocket: trades
`<symbol>@trade`

- Cada trade real ejecutado (precio `p`, cantidad `q`, lado comprador/vendedor).
- Se usa para:
  - Último trade visible
  - Dirección del flujo de agresión (`buy` / `sell`)
  - VWAP en ventana y sesión

---

## 🚀 Ejecución

### Ejemplo en Windows / Linux nativo
```bash
BinanceOrderBook --symbols=btcusdt,ethusdt --topN=5 --log=out.csv
```

Parámetros:
- `--symbols`  
  Lista separada por coma de símbolos spot de Binance (ej: `btcusdt,ethusdt`).

- `--topN`  
  Cantidad de niveles de libro a publicar en `topBids` / `topAsks`.

- `--log` (opcional)  
  Archivo CSV de salida.  
  Si no se indica, el snapshot se imprime en stdout.

Salida típica (recortada):
```text
[DepthStream] Conectado a btcusdt
[TradeStream] Conectado btcusdt
[DepthStream] Conectado a ethusdt
[TradeStream] Conectado ethusdt

1761963151.286440,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,...,109580.860000,0.000050,buy,0.000000,109580.000000,0.789871
1761963152.298617,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,...,109579.990000,0.002300,sell,0.000000,109579.995936,0.789871
1761963153.309593,btcusdt,109579.995000,0.010000,109579.990000,3.380600,109580.000000,0.932690,...,109580.860000,0.000050,buy,0.000000,109580.005646,0.789871
```

---

## 🐳 Ejecución en Docker

El proyecto incluye una build Docker pensada para Linux que:
- Usa `libcurl` del sistema (Ubuntu 22.04) para HTTPS.
- Compila `cpr` contra esa `libcurl`, con `CPR_USE_SYSTEM_CURL=ON` para evitar conflictos TLS.
- Instala `ixwebsocket` vía `vcpkg`, pero usando un manifiesto reducido (`vcpkg-linux.json`) que **solo** trae ixwebsocket en Linux.
- Enlaza con OpenSSL de forma explícita (`OpenSSL::SSL`, `OpenSSL::Crypto`).

Esto elimina el clásico error TLS al correr en contenedor:
`SSL: could not create a context: error:00000000:lib(0)::reason(0)`

### Archivos relevantes
- `Dockerfile`  
  Build de la app en Ubuntu 22.04.
- `vcpkg-linux.json`  
  Manifiesto alternativo para vcpkg en entorno Docker (solo `ixwebsocket`).  
  Tu `vcpkg.json` normal para Windows queda intacto.

### Build de la imagen
```bash
docker build -t binance-ob .
```

### Correr la imagen
```bash
docker run --rm binance-ob --symbols=btcusdt,ethusdt --topN=5
```

Salida típica en contenedor ya funcionando:
```text
1762039319.678570,btcusdt,109999.995000,0.010000,109999.990000,12.277870,110000.000000,0.982250,109999.990000:12.277870|...,110000.000000:0.982250|...,110017.000000,0.000060,sell,0.000000,110017.008571,0.926478
[DepthStream] Conectado a btcusdt
[TradeStream] Conectado btcusdt
[DepthStream] Conectado a ethusdt
[TradeStream] Conectado ethusdt
```

Si ves data de precios real y mensajes `Conectado`, significa:
- REST HTTPS funciona dentro de Docker.
- Los WebSockets TLS (`wss://...:9443`) también están conectando.
- Estás streameando order book y trades de Binance en vivo desde adentro del contenedor, sin depender del host.

---

## ⚠️ Notas y limitaciones actuales

- Reconexión:
  - Si Binance cierra la conexión WS (por timeout, rate limit, etc.), hoy se loguea:
    ```text
    [TradeStream] Conexión cerrada btcusdt
    [TradeStream] Detenido btcusdt
    ```
    y el hilo se apaga. Todavía no hay lógica de reconexión automática/ping keepalive.

- Seguridad TLS:
  - En Linux la app valida certificados usando la CA store del sistema (`/etc/ssl/certs/ca-certificates.crt`).
  - No se desactiva verificación SSL.

- Performance:
  - Los locks (`std::mutex`) en `OrderBook` y `TradeStats` protegen contra data races.
  - `Publisher` toma snapshots consistentes: no mezcla mitad de un libro viejo con mitad de un trade nuevo.

---

## 🛠️ Compilación (Windows)

### Requisitos
- Visual Studio 2022 o superior
- CMake 3.20+
- vcpkg instalado y configurado (`VCPKG_ROOT` definido)

### Dependencias (vcpkg)
```bash
vcpkg install cpr ixwebsocket nlohmann-json
```

### Build
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

El binario resultante (`BinanceOrderBook.exe`) acepta los mismos flags (`--symbols`, `--topN`, `--log`) que en Linux / Docker.

---

## 📚 Tecnologías y dependencias

- **Lenguaje:** C++17
- **HTTP REST:** [CPR](https://github.com/libcpr/cpr)
- **WebSocket (wss):** [ixwebsocket](https://github.com/machinezone/IXWebSocket)
- **JSON:** [nlohmann/json](https://github.com/nlohmann/json)
- **TLS:** OpenSSL (en Linux), Schannel (Windows via cpr)
- **Build system:** CMake
- **Empaquetado deps:**
  - Windows: `vcpkg.json` (full: `cpr`, `ixwebsocket`, `nlohmann-json`)
  - Docker/Linux: `vcpkg-linux.json` (solo `ixwebsocket`) + `libcurl` del sistema

---

## 📄 Licencia

MIT

Autor: Damián Guardia

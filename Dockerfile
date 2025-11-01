FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 1. Paquetes base de build y runtime
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        curl \
        ca-certificates \
        pkg-config \
        zip \
        unzip \
        tar \
        openssl \
        libssl-dev \
        libssl3 \
        libstdc++6 \
        libgcc1 \
        libcurl4-openssl-dev \
        nlohmann-json3-dev \
        python3 \
        ninja-build \
        && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 2. Copiamos tu repo al contenedor
COPY . /app

# 3. Bootstrap de vcpkg
RUN rm -rf /app/vcpkg && \
    git clone https://github.com/microsoft/vcpkg.git /app/vcpkg && \
    /app/vcpkg/bootstrap-vcpkg.sh

# 4. Inyectamos manifiesto minimal para Linux (solo ixwebsocket)
#    Este archivo lo creaste vos como vcpkg-linux.json.
#    Acá lo renombramos dentro del contenedor a vcpkg.json para este build.
COPY vcpkg-linux.json /app/vcpkg.json

ENV VCPKG_ROOT=/app/vcpkg
ENV CMAKE_TOOLCHAIN_FILE=/app/vcpkg/scripts/buildsystems/vcpkg.cmake

# 5. Instalamos solo ixwebsocket vía vcpkg (sin cpr ni curl ni openssl custom)
RUN /app/vcpkg/vcpkg install

# 6. Compilamos e instalamos cpr usando libcurl del sistema Ubuntu
#    Clave: CPR_USE_SYSTEM_CURL=ON para que NO trate de compilar su propia curl.
RUN git clone https://github.com/libcpr/cpr.git /app/cpr && \
    cmake -S /app/cpr -B /app/cpr/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DCPR_GENERATE_CURL_SYMBOLS=OFF \
        -DCPR_USE_SYSTEM_CURL=ON \
        -DCPR_ENABLE_SSL=ON \
        -DCMAKE_USE_OPENSSL=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    cmake --build /app/cpr/build --config Release && \
    cmake --install /app/cpr/build

# 7. Build de tu proyecto final
RUN cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} && \
    cmake --build build --config Release --target BinanceOrderBook

WORKDIR /app/build
ENTRYPOINT ["/app/build/BinanceOrderBook"]

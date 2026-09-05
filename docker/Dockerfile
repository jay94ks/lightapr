# Stage 1: Build LightAPR C++ Server Daemon
FROM alpine:3.19 AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    ninja \
    linux-headers

WORKDIR /src

# Copy project files
COPY . .

# Build CMake in Release mode
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release

# Stage 2: Runtime Image
FROM alpine:3.19

RUN apk add --no-cache \
    libstdc++ \
    ca-certificates

# Copy executable from builder stage
COPY --from=builder /src/build/lightapr /usr/local/bin/lightapr

# Copy entrypoint script
COPY docker/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

# Environment variables with default values
ENV CELL_ID=default_cell \
    ACCESS_KEY=lightapr_secret_key \
    MQTT_PORT=1883 \
    WS_PORT=8083 \
    HTTP_PORT=8080 \
    LOG_FILE=/var/log/lightapr.log \
    STANDALONE=true

# Expose default MQTT (1883), WebSocket MQTT (8083), HTTP Management (8080) ports
EXPOSE 1883 8083 8080

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]

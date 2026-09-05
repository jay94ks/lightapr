# APR (Access-Point Registry) Specification

🌐 **Language**: [English Version](https://github.com/jay94ks/lightapr/blob/main/README.en.md) | [Korean Version](https://github.com/jay94ks/lightapr/blob/main/README.md)  
📦 **GitHub Repository**: [https://github.com/jay94ks/lightapr](https://github.com/jay94ks/lightapr)

---

## 1. Overview
APR is a **'Self-Adaptive Discovery'** protocol designed to assemble dynamic network topologies at runtime in auto-scaling environments without hardcoded endpoints.
Rather than employing heavy distributed consensus or excessive security validation, APR focuses on fast in-memory service discovery and lightweight integration.

## 2. Ports & Planes
An APR daemon listens on independent ports in a single process:
1. **MQTT Control Plane**
   - **Port 1883/8883 (Native TCP MQTT)** & **Port 8083 (WebSocket MQTT)**
   - Role: Node registration, topology state propagation, metadata broadcasting, and asynchronous event delivery.
2. **HTTP Management/Query Plane**
   - **Port 8080/80 (HTTP REST API)**
   - Role: Registry health check, topology REST querying, and load-balancing pool API (`/resolve`).

## 3. Lifecycle & Protocol Flow
1. **Connection & Authentication**: Generate `Username` prefixed with the server's `role`. Combine `APR access key` and `Username` as the MQTT password.
2. **Node Registration**: Upon successful MQTT connection, publish metadata `{ role, workers, endpoint }` to topic `apr/node/meta`. (Worker nodes or web browser clients that do not expose an HTTP endpoint specify `"endpoint": null`.)
3. **Topology Propagation**: APR registers new node metadata and broadcasts state changes to the cell-internal topic `apr/{role}`.
4. **Direct Point-to-Point Communication**: Actual business requests/responses between nodes bypass APR, communicating directly HTTP Point-to-Point based on the locally synchronized topology.
5. **Event Delivery**: Asynchronous notifications between services utilize `app/{role}` or `app/{role}/{worker}` MQTT topics as auxiliary channels.
6. **Grace Period**: When a node's MQTT session disconnects, APR grants a 3-minute grace period (`GRACE` status) instead of immediate deletion.
   - Reconnect within 3 minutes: Restore existing session and metadata (`OK` status).
   - Exceed 3 minutes: Treated as permanent termination (`ERASED`), broadcasting expiration to the topology and removing the node from the registry.

## 4. Scale-out Architecture
- APR does not aim for a single global registry.
- Infrastructure is divided into multiple independent **Cell** units, with a single independent APR instance deployed per cell.
- State replication or consensus between APR instances is strictly excluded to maintain complete stateless isolation.
- Cross-cell communication is handled via gateway nodes at cell boundaries without APR intervention.

## 5. Client SDKs & Sample Code
Multi-language SDKs and example projects are provided for seamless APR integration:
- **SDK List (`sdk/`)**:
  - `sdk/cpp`: C++17 headers and static library (`apr_sdk_cpp`)
  - `sdk/csharp`: .NET 8.0 C# SDK (`Apr.Sdk`)
  - `sdk/nodejs`: Node.js SDK (`@lightapr/sdk`)
  - `sdk/ts`: TypeScript SDK (`@lightapr/sdk-ts`)
- **Example List (`examples/`)**:
  - `examples/http_node`: Service node example exposing an HTTP endpoint
  - `examples/worker_node`: Background worker node example without an HTTP endpoint
  - `examples/ts/`, `examples/csharp/`, `examples/cpp/`: Language-specific example variations
- **Monitor & Tester Web Apps (`monitor/`)**: dependency-free, single-file HTML/CSS/JS apps, compiled directly into the `lightapr` binary at build time (no external file needed at runtime) and served over HTTP when explicitly enabled:
  - `monitor/html/index.html`: read-only observability dashboard - server status, the 7-way memory breakdown, live connection count, the node registry table, and a live topology event feed over WebSocket MQTT. Served at `GET /monitor` (and `GET /`) when started with `--monitor`.
  - `monitor/tester/index.html`: interactive playground to exercise every daemon feature from a browser - an HTTP Playground for each REST endpoint, an MQTT Playground (connect/subscribe/publish with a decoded live message log), a node-registration simulator, and a capped burst-request tool that demonstrates the connection/rate limits below. Served at `GET /tester` when started with `--tester`.

## 6. Docker Container Image & Docker Compose

### Image Details
The LightAPR server daemon is published as a Docker Hub container image available with tag `1.0.0` and linked `latest` tag.
- **GitHub Repository**: [https://github.com/jay94ks/lightapr](https://github.com/jay94ks/lightapr)
- **Docker Hub Repository**: [https://hub.docker.com/r/jay94ks/lightapr](https://hub.docker.com/r/jay94ks/lightapr) (`jay94ks/lightapr`)
- **Image Names**: `jay94ks/lightapr:1.0.0`, `jay94ks/lightapr:latest`
- **Port Allocations**:
  - `1883`: Native TCP MQTT Control Plane
  - `8083`: WebSocket MQTT Control Plane
  - `8080`: HTTP REST Management/Discovery Plane
- **Environment Variables**:
  - `CELL_ID`: Cell identifier (Default: `default_cell`)
  - `ACCESS_KEY`: Access authentication key (Default: `lightapr_secret_key`)
  - `MQTT_PORT`: Native TCP MQTT port (Default: `1883`)
  - `WS_PORT`: WebSocket MQTT port (Default: `8083`)
  - `HTTP_PORT`: HTTP REST API port (Default: `8080`)
  - `WORKER_THREADS`: Event loop worker thread count (Default: `0`, auto-detect CPU cores)
  - `LOG_FILE`: Log file path (Default: `/var/log/lightapr.log`)
  - `LOG_LEVEL`: Minimum log level: `debug`|`info`|`warn`|`error` (Default: `info`)
  - `STANDALONE`: Standalone console logging mode (Default: `true`)
  - `CONFIG_FILE`: Path to a JSON config file (optional; see below). Its values replace the CLI defaults, and are themselves overridden by any other CLI flag/env var set explicitly.
  - `IDLE_TIMEOUT`: Seconds a connection may sit idle (no completed read) before the server closes it (Default: `90`)
  - `MAX_BUFFER_BYTES`: Max bytes a single session may buffer before an in-progress request/frame is considered complete; exceeding it closes the connection (Default: `262144`, 256 KiB)
  - `MAX_CONNECTIONS`: Process-wide cap on simultaneously open connections across MQTT (TCP+WS) and HTTP combined; `0` = unlimited (Default: `10000`)
  - `MAX_CONNECTIONS_PER_IP`: Max simultaneous connections from a single source IP, across all listeners; `0` = unlimited (Default: `100`)
  - `MAX_NEW_CONNECTIONS_PER_IP`: Max new connections a single source IP may open within `CONNECTION_RATE_WINDOW_SEC`; `0` = unlimited (Default: `20`)
  - `CONNECTION_RATE_WINDOW_SEC`: Sliding window (seconds) used by `MAX_NEW_CONNECTIONS_PER_IP` (Default: `10`)
  - `MAX_REQUESTS_PER_CONNECTION`: Max HTTP requests served over one keep-alive connection before the server closes it (forcing a reconnect, which is then rate-limited again); `0` = unlimited (Default: `10000`)
  - `MONITOR`: Serve the read-only monitor dashboard at `/monitor` (and `/`) (Default: `false` - off unless explicitly enabled)
  - `TESTER`: Serve the interactive tester app at `/tester` (Default: `false` - off unless explicitly enabled)

### High-Load & DDoS Resilience
LightAPR is designed to survive connection floods and abusive clients without exhausting server resources:
- **Connection admission control**: every accepted socket (MQTT TCP, MQTT WebSocket, and HTTP share one process-wide guard) is checked against `MAX_CONNECTIONS`, `MAX_CONNECTIONS_PER_IP`, and the `MAX_NEW_CONNECTIONS_PER_IP`/`CONNECTION_RATE_WINDOW_SEC` rate limit **before** a session object is even created — a rejected connection costs one map lookup and an immediate socket close, nothing more.
- **Idle & buffer-size caps**: every session (`IDLE_TIMEOUT`, `MAX_BUFFER_BYTES`) is force-closed if it goes quiet too long or tries to buffer an unreasonably large partial request/frame — this bounds a slow/stalled client's resource footprint regardless of intent.
- **MQTT brute-force mitigation**: a failed `CONNECT` (bad credentials or malformed packet) now closes the connection immediately after the error response, instead of allowing unlimited retry attempts on one socket — every retry has to reconnect, and reconnects are rate-limited above.
- **HTTP keep-alive abuse mitigation**: `MAX_REQUESTS_PER_CONNECTION` bounds how many requests a single connection can push through before being forced to reconnect (and pass the connection-rate check again), so keep-alive can't be used to bypass per-IP throttling.

You can watch these limits in action live: enable `--tester`, open `/tester`, and use its "Burst Request Test" tool (capped at 100 requests) to see connections get rejected once a limit is hit.

### Monitor & Tester Web Apps
Two operator-facing web apps ship compiled directly into the `lightapr` binary at build time ([`cmake/EmbedFile.cmake`](https://github.com/jay94ks/lightapr/blob/main/cmake/EmbedFile.cmake) embeds them as byte arrays - no separate file to deploy) and are off by default, since they're debug/operator tools rather than part of the discovery API:
- **`--monitor`** → `http://<host>:<http_port>/monitor` (and `/`): live dashboard - node registry, the 7-way memory breakdown (`registry`/`mqtt_rx`/`mqtt_tx`/`http_rx`/`http_tx`/`other`, plus OS RSS), live connection count, and a decoded WebSocket-MQTT topology event feed.
- **`--tester`** → `http://<host>:<http_port>/tester`: interactive playground for every HTTP endpoint and MQTT operation (connect/subscribe/publish), a node-registration simulator, and the burst-request tool mentioned above.

Both require only the flag (or `MONITOR=true` / `TESTER=true` in Docker) - no extra build step, since the HTML is embedded at compile time from `monitor/html/index.html` and `monitor/tester/index.html`.

### CLI Options & Config File
LightAPR accepts every setting as a CLI flag, or you can point it at a JSON config file with `-f/--config <path>` to avoid a long argument list:

| Flag | Description | Default |
|---|---|---|
| `-s`, `--standalone` | Run in the foreground with console logging | daemon mode |
| `-c`, `--cell-id` | Cell identifier | `default_cell` |
| `-k`, `--access-key` | MQTT access authentication key | `lightapr_secret_key` |
| `-p`, `--port` | Native TCP MQTT port | `1883` |
| `-w`, `--ws-port` | WebSocket MQTT port | `8083` |
| `-h`, `--http-port` | HTTP REST API port | `8080` |
| `-t`, `--threads` | Event loop worker thread count (`0` = auto-detect) | `0` |
| `-l`, `--log-file` | Log file path (daemon mode) | `lightapr.log` |
| `-v`, `--log-level` | Minimum log level: `debug`\|`info`\|`warn`\|`error` | `info` |
| `-f`, `--config` | Path to a JSON config file | - |
| `--idle-timeout` | Idle session timeout, seconds | `90` |
| `--max-buffer-bytes` | Max buffered bytes per session | `262144` |
| `--max-connections` | Process-wide connection cap (`0` = unlimited) | `10000` |
| `--max-connections-per-ip` | Per-source-IP connection cap (`0` = unlimited) | `100` |
| `--max-new-connections-per-ip` | Per-source-IP new-connection rate cap (`0` = unlimited) | `20` |
| `--connection-rate-window-sec` | Rate-limit window, seconds | `10` |
| `--max-requests-per-connection` | Max HTTP requests per keep-alive connection (`0` = unlimited) | `10000` |
| `--monitor` | Serve the monitor dashboard at `/monitor` (and `/`) | off |
| `--tester` | Serve the interactive tester app at `/tester` | off |

A sample config file is provided at [config.example.json](https://github.com/jay94ks/lightapr/blob/main/config.example.json):
```json
{
    "standalone": false,
    "cell_id": "default_cell",
    "access_key": "lightapr_secret_key",
    "mqtt_port": 1883,
    "ws_port": 8083,
    "http_port": 8080,
    "threads": 0,
    "log_file": "lightapr.log",
    "log_level": "info",
    "session_idle_timeout_sec": 90,
    "max_session_buffer_bytes": 262144,
    "max_connections": 10000,
    "max_connections_per_ip": 100,
    "max_new_connections_per_ip": 20,
    "connection_rate_window_sec": 10,
    "max_requests_per_connection": 10000,
    "monitor": false,
    "tester": false
}
```
Precedence: built-in defaults → `--config` file → other explicit CLI flags (which always win for the fields they set, regardless of argument order).

### Docker CLI Quickstart
```bash
# Run container using Docker CLI
docker run -d \
  --name lightapr-server \
  -p 1883:1883 \
  -p 8083:8083 \
  -p 8080:8080 \
  -e CELL_ID="cell-prod-1" \
  -e ACCESS_KEY="my_secret_key" \
  jay94ks/lightapr:latest
```

### Docker Compose Sample (`docker-compose.yml`)
Utilize the included [docker-compose.yml](https://github.com/jay94ks/lightapr/blob/main/docker-compose.yml) file to launch the service:

```yaml
version: '3.8'

services:
  lightapr:
    image: jay94ks/lightapr:latest
    container_name: lightapr-server
    build:
      context: .
      dockerfile: docker/Dockerfile
    restart: unless-stopped
    ports:
      - "1883:1883"   # Native TCP MQTT
      - "8083:8083"   # WebSocket MQTT
      - "8080:8080"   # HTTP REST Management
    environment:
      - CELL_ID=default_cell
      - ACCESS_KEY=lightapr_secret_key
      - MQTT_PORT=1883
      - WS_PORT=8083
      - HTTP_PORT=8080
      - LOG_FILE=/var/log/lightapr.log
      - STANDALONE=true
      - MAX_CONNECTIONS=10000
      - MAX_CONNECTIONS_PER_IP=100
      - MAX_NEW_CONNECTIONS_PER_IP=20
      - MONITOR=false
      - TESTER=false
```

```bash
# Run with Docker Compose
docker compose up -d
```

## 7. Specifications & Guidelines
- Detailed Protocol Specification: [PROTOCOL.md](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.md) | [English](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.en.md)
- C++ Development Guidelines: [AGENTS.md](https://github.com/jay94ks/lightapr/blob/main/AGENTS.md) | [English](https://github.com/jay94ks/lightapr/blob/main/AGENTS.en.md)

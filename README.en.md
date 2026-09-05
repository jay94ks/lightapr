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
- **Monitoring Web App (`monitor/html/index.html`)**:
  - Web dashboard for LightAPR observability, node registry viewing, load balancer testing, and WebSocket event viewing.

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
  - `LOG_FILE`: Log file path (Default: `/var/log/lightapr.log`)
  - `STANDALONE`: Standalone console logging mode (Default: `true`)

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
```

```bash
# Run with Docker Compose
docker compose up -d
```

## 7. Specifications & Guidelines
- Detailed Protocol Specification: [PROTOCOL.md](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.md) | [English](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.en.md)
- C++ Development Guidelines: [AGENTS.md](https://github.com/jay94ks/lightapr/blob/main/AGENTS.md) | [English](https://github.com/jay94ks/lightapr/blob/main/AGENTS.en.md)

# APR (Access-Point Registry) Specification

🌐 **Language**: [English Version](https://github.com/jay94ks/lightapr/blob/main/README.en.md) | [한국어 버전](https://github.com/jay94ks/lightapr/blob/main/README.md)  
📦 **GitHub**: [https://github.com/jay94ks/lightapr](https://github.com/jay94ks/lightapr)

---

## 1. 개요 (Overview)
APR은 Auto-scaling 환경에서 하드코딩된 엔드포인트 없이 런타임에 동적으로 통신망을 조립하는 **'자율 적응형 디스커버리(Self-Adaptive Discovery)'** 프로토콜이다.
무거운 분산 합의나 과도한 보안 검증 대신, 인메모리 기반의 신속한 서비스 탐색과 경량 통합을 목표로 한다.

## 2. 프로토콜 구성 (Ports & Planes)
APR 데몬은 단일 프로세스로 독립된 포트를 청취한다.
1. **MQTT 제어 평면 (Control Plane)**
   - **Port 1883/8883 (Native TCP MQTT)** & **Port 8083 (WebSocket MQTT)**
   - 역할: 노드 등록, 토폴로지 상태 전파, 메타데이터 브로드캐스팅 및 비동기 이벤트 전달.
2. **HTTP 관리/조회 평면 (Management Plane)**
   - **Port 8080/80 (HTTP REST API)**
   - 역할: 레지스트리 헬스 체크, 토폴로지 REST 조회, 로드 밸런싱 API (`/resolve`).

## 3. 프로토콜 동작 주기 (Lifecycle & Flow)
1. **연결 및 인증**: User Name은 역할(role)을 prefix로 하여 생성하고, `APR access key`와 `User Name`을 결합하여 패스워드로 사용한다.
2. **구현체 등록**: MQTT 연결 성공 시 `apr/node/meta` 토픽에 자신의 메타데이터 `{ role, workers, endpoint }`를 발행한다. (HTTP 엔드포인트를 노출하지 않는 워커 노드나 웹 브라우저 클라이언트는 `endpoint: null` 지정 가능)
3. **토폴로지 전파**: APR은 신규 노드 정보를 등록하고, 해당 셀 내부 토픽(`apr/{role}`)에 노드 상태 변화를 브로드캐스팅한다.
4. **직접 통신**: 노드 간 실제 비즈니스 요청/응답은 APR을 거치지 않고, 로컬 메모리에 동기화된 토폴로지를 기반으로 HTTP Point-to-Point 통신한다.
5. **이벤트 전파**: 서비스 간 비동기 알림이 필요할 경우 `app/{role}` 또는 `app/{role}/{worker}` MQTT 토픽을 보조 채널로 활용한다.
6. **상태 감쇠 (Grace Period)**: 노드의 MQTT 세션 단절 시 즉시 삭제하지 않고 3분의 유예 시간(`GRACE` 상태)을 부여한다.
   - 3분 이내 재접속: 기존 세션 및 메타데이터 복구 (`OK` 상태).
   - 3분 초과: 영구 퇴장(`ERASED`)으로 간주하여 토폴로지 만료 통보를 전파하고 레지스트리에서 제거한다.

## 4. 확장 모델 (Scale-out Architecture)
- APR은 글로벌(전사) 단일 레지스트리를 지향하지 않는다.
- 인프라는 복수의 독립된 셀(Cell) 단위로 분할되며, 각 셀마다 독립된 APR 단일 인스턴스가 배치된다.
- APR 간의 상태 동기화(Replication/Consensus)는 엄격히 배제하며, 완벽한 무상태 격리를 유지한다.
- 셀 간 통신(Cross-Cell)은 APR이 관여하지 않으며, 셀 경계에 배치된 게이트웨이 노드를 통해 처리한다.

## 5. 클라이언트 SDK & 샘플 코드 (SDKs & Examples)
APR 프로토콜을 손쉽게 활용할 수 있도록 다국어 공통 SDK와 예제 코드가 제공된다.
- **SDK 목록 (`sdk/`)**:
  - `sdk/cpp`: C++17 헤더 및 정적 라이브러리 (`apr_sdk_cpp`)
  - `sdk/csharp`: .NET 8.0 C# SDK (`Apr.Sdk`)
  - `sdk/nodejs`: Node.js SDK (`@lightapr/sdk`)
  - `sdk/ts`: TypeScript SDK (`@lightapr/sdk-ts`)
- **예제 목록 (`examples/`)**:
  - `examples/http_node`: 엔드포인트 서버를 갖춘 서비스 노드 예제
  - `examples/worker_node`: 엔드포인트가 없는 백그라운드 워커 노드 예제
  - `examples/ts/`, `examples/csharp/`, `examples/cpp/`: 언어별 예제 변형 구현체
- **모니터·테스터 웹 앱 (`monitor/`)**: 외부 의존성 없는 단일 HTML/CSS/JS 파일로, 빌드 타임에 `lightapr` 바이너리에 직접 컴파일되어 임베드되며(런타임에 별도 파일 불필요), 명시적으로 활성화했을 때만 HTTP로 서빙됩니다:
  - `monitor/html/index.html`: 읽기 전용 관측 대시보드 — 서버 상태, 7분류 메모리 내역, 실시간 연결 수, 노드 레지스트리 테이블, WS MQTT 기반 실시간 토폴로지 이벤트 피드. `--monitor`로 기동 시 `GET /monitor`(및 `GET /`)에서 제공.
  - `monitor/tester/index.html`: 모든 데몬 기능을 브라우저에서 직접 시험해볼 수 있는 인터랙티브 플레이그라운드 — 각 REST 엔드포인트용 HTTP Playground, 연결/구독/발행 및 디코딩된 실시간 메시지 로그를 갖춘 MQTT Playground, 노드 등록 시뮬레이터, 아래 연결/속도 제한을 직접 확인할 수 있는 상한 있는 버스트 요청 도구. `--tester`로 기동 시 `GET /tester`에서 제공.

## 6. 도커 컨테이너 이미지 & Docker Compose (Docker & Compose)

### 이미지 정보 (Docker Image)
LightAPR 서버 데몬은 Docker Hub 컨테이너 이미지로 제공되며, `1.0.0` 버전 및 `latest` 태그로 이용할 수 있습니다.
- **GitHub Repository**: [https://github.com/jay94ks/lightapr](https://github.com/jay94ks/lightapr)
- **Docker Hub Repository**: [https://hub.docker.com/r/jay94ks/lightapr](https://hub.docker.com/r/jay94ks/lightapr) (`jay94ks/lightapr`)
- **이미지명**: `jay94ks/lightapr:1.0.0`, `jay94ks/lightapr:latest`
- **포트 바인딩**:
  - `1883`: Native TCP MQTT 제어 평면
  - `8083`: WebSocket MQTT 제어 평면
  - `8080`: HTTP REST 관리/디스커버리 평면
- **주요 환경 변수**:
  - `CELL_ID`: 셀 식별자 (기본값: `default_cell`)
  - `ACCESS_KEY`: 인증 키 (기본값: `lightapr_secret_key`)
  - `MQTT_PORT`: Native TCP MQTT 포트 (기본값: `1883`)
  - `WS_PORT`: WebSocket MQTT 포트 (기본값: `8083`)
  - `HTTP_PORT`: HTTP REST API 포트 (기본값: `8080`)
  - `WORKER_THREADS`: 이벤트 루프 워커 스레드 수 (기본값: `0`, CPU 코어 수 자동 감지)
  - `LOG_FILE`: 로그 파일 경로 (기본값: `/var/log/lightapr.log`)
  - `LOG_LEVEL`: 최소 로그 레벨 `debug`|`info`|`warn`|`error` (기본값: `info`)
  - `STANDALONE`: 독립 실행형 포그라운드 콘솔 로깅 여부 (기본값: `true`)
  - `CONFIG_FILE`: JSON 설정 파일 경로 (선택 사항, 아래 참고). 파일에 명시된 값이 CLI 기본값을 대체하며, 명시적으로 지정한 다른 CLI 인자/환경 변수가 있으면 그 값이 최종적으로 우선합니다.
  - `IDLE_TIMEOUT`: 연결이 유휴 상태(완료된 읽기가 없음)로 머무를 수 있는 최대 시간(초); 초과 시 서버가 연결을 종료 (기본값: `90`)
  - `MAX_BUFFER_BYTES`: 세션 하나가 진행 중인 요청/프레임을 위해 버퍼링할 수 있는 최대 바이트 수; 초과 시 연결 종료 (기본값: `262144`, 256KiB)
  - `MAX_CONNECTIONS`: MQTT(TCP+WS)와 HTTP를 합산한 프로세스 전역 동시 연결 상한; `0`이면 무제한 (기본값: `10000`)
  - `MAX_CONNECTIONS_PER_IP`: 모든 리스너를 합산해 단일 소스 IP가 가질 수 있는 최대 동시 연결 수; `0`이면 무제한 (기본값: `100`)
  - `MAX_NEW_CONNECTIONS_PER_IP`: 단일 소스 IP가 `CONNECTION_RATE_WINDOW_SEC` 시간 내에 새로 맺을 수 있는 최대 연결 수; `0`이면 무제한 (기본값: `20`)
  - `CONNECTION_RATE_WINDOW_SEC`: `MAX_NEW_CONNECTIONS_PER_IP`가 사용하는 슬라이딩 윈도우(초) (기본값: `10`)
  - `MAX_REQUESTS_PER_CONNECTION`: Keep-Alive 연결 하나로 처리할 수 있는 최대 HTTP 요청 수; 초과 시 연결을 종료해 재연결을 강제(재연결은 다시 속도 제한 대상) (`0`이면 무제한, 기본값: `10000`)
  - `MONITOR`: `/monitor`(및 `/`)에서 읽기 전용 모니터 대시보드 제공 여부 (기본값: `false` — 명시적으로 켜야 노출)
  - `TESTER`: `/tester`에서 인터랙티브 테스터 앱 제공 여부 (기본값: `false` — 명시적으로 켜야 노출)

### 고부하 및 DDoS 내성 (High-Load & DDoS Resilience)
LightAPR은 연결 폭주나 악의적인 클라이언트에도 서버 자원이 고갈되지 않도록 설계되었습니다:
- **연결 수락 단계 제어**: 모든 accept된 소켓(MQTT TCP·MQTT WebSocket·HTTP가 하나의 프로세스 전역 가드를 공유)은 세션 객체를 생성하기 **전에** `MAX_CONNECTIONS`, `MAX_CONNECTIONS_PER_IP`, `MAX_NEW_CONNECTIONS_PER_IP`/`CONNECTION_RATE_WINDOW_SEC` 속도 제한을 검사합니다 — 거부된 연결은 맵 조회 한 번과 즉시 소켓 종료 정도의 비용만 발생시킵니다.
- **유휴·버퍼 상한**: 모든 세션은 `IDLE_TIMEOUT`·`MAX_BUFFER_BYTES`를 초과하면 강제 종료되어, 느리거나 멈춘 클라이언트가 차지하는 자원을 의도와 무관하게 제한합니다.
- **MQTT 브루트포스 방어**: 인증 실패(잘못된 자격 증명 또는 손상된 패킷) 시 오류 응답 직후 연결을 즉시 종료합니다 — 이전에는 같은 연결에서 무제한 재시도가 가능했지만, 이제 재시도마다 재연결이 필요하며 재연결은 위 속도 제한의 대상이 됩니다.
- **HTTP Keep-Alive 남용 방지**: `MAX_REQUESTS_PER_CONNECTION`으로 연결 하나가 처리할 수 있는 요청 수를 제한해, Keep-Alive를 이용해 IP별 속도 제한을 우회하지 못하도록 합니다.

위 제한들이 실제로 동작하는 모습을 직접 확인하려면: `--tester`를 켜고 `/tester`를 연 뒤 "Burst Request Test" 도구(최대 100회로 상한)를 사용해 한도 초과 시 연결이 거부되는 것을 확인할 수 있습니다.

### 모니터·테스터 웹 앱
두 운영자용 웹 앱은 `lightapr` 바이너리에 빌드 타임에 직접 컴파일되어 임베드되며([`cmake/EmbedFile.cmake`](https://github.com/jay94ks/lightapr/blob/main/cmake/EmbedFile.cmake)가 바이트 배열로 변환 — 별도 배포 파일 불필요), 디스커버리 API의 일부가 아닌 디버그/운영 도구이므로 기본값은 꺼짐입니다:
- **`--monitor`** → `http://<host>:<http_port>/monitor` (및 `/`): 실시간 대시보드 — 노드 레지스트리, 7분류 메모리 내역(`registry`/`mqtt_rx`/`mqtt_tx`/`http_rx`/`http_tx`/`other` + OS RSS), 실시간 연결 수, 디코딩된 WS MQTT 토폴로지 이벤트 피드.
- **`--tester`** → `http://<host>:<http_port>/tester`: 모든 HTTP 엔드포인트·MQTT 동작(연결/구독/발행)을 위한 인터랙티브 플레이그라운드, 노드 등록 시뮬레이터, 위에서 언급한 버스트 요청 도구.

두 앱 모두 플래그(Docker에서는 `MONITOR=true`/`TESTER=true`)만 켜면 되며, HTML이 `monitor/html/index.html`과 `monitor/tester/index.html`로부터 컴파일 타임에 임베드되므로 별도 빌드 단계가 필요 없습니다.

### CLI 옵션 및 설정 파일
LightAPR은 모든 설정을 CLI 인자로 받을 수 있으며, 인자가 길어지는 것을 피하려면 `-f/--config <경로>` 옵션으로 JSON 설정 파일을 지정할 수도 있습니다:

| 플래그 | 설명 | 기본값 |
|---|---|---|
| `-s`, `--standalone` | 포그라운드 콘솔 로깅 모드로 실행 | 데몬 모드 |
| `-c`, `--cell-id` | 셀 식별자 | `default_cell` |
| `-k`, `--access-key` | MQTT 인증 키 | `lightapr_secret_key` |
| `-p`, `--port` | Native TCP MQTT 포트 | `1883` |
| `-w`, `--ws-port` | WebSocket MQTT 포트 | `8083` |
| `-h`, `--http-port` | HTTP REST API 포트 | `8080` |
| `-t`, `--threads` | 이벤트 루프 워커 스레드 수 (`0`=자동 감지) | `0` |
| `-l`, `--log-file` | 로그 파일 경로 (데몬 모드) | `lightapr.log` |
| `-v`, `--log-level` | 최소 로그 레벨: `debug`\|`info`\|`warn`\|`error` | `info` |
| `-f`, `--config` | JSON 설정 파일 경로 | - |
| `--idle-timeout` | 세션 유휴 타임아웃(초) | `90` |
| `--max-buffer-bytes` | 세션당 최대 버퍼 바이트 수 | `262144` |
| `--max-connections` | 프로세스 전역 연결 상한 (`0`=무제한) | `10000` |
| `--max-connections-per-ip` | 소스 IP별 연결 상한 (`0`=무제한) | `100` |
| `--max-new-connections-per-ip` | 소스 IP별 신규 연결 속도 상한 (`0`=무제한) | `20` |
| `--connection-rate-window-sec` | 속도 제한 윈도우(초) | `10` |
| `--max-requests-per-connection` | Keep-Alive 연결당 최대 HTTP 요청 수 (`0`=무제한) | `10000` |
| `--monitor` | 모니터 대시보드를 `/monitor`(및 `/`)에서 제공 | 꺼짐 |
| `--tester` | 인터랙티브 테스터 앱을 `/tester`에서 제공 | 꺼짐 |

예시 설정 파일은 [config.example.json](https://github.com/jay94ks/lightapr/blob/main/config.example.json)에 있습니다:
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
우선순위: 내장 기본값 → `--config` 파일 → 명시적으로 지정한 다른 CLI 인자(argv 순서와 무관하게 해당 필드에 대해 항상 최우선).

### Docker CLI 실행 예시
```bash
# Docker 실행 예시
docker run -d \
  --name lightapr-server \
  -p 1883:1883 \
  -p 8083:8083 \
  -p 8080:8080 \
  -e CELL_ID="cell-prod-1" \
  -e ACCESS_KEY="my_secret_key" \
  jay94ks/lightapr:latest
```

### Docker Compose 샘플 (`docker-compose.yml`)
루트 디렉토리에 포함된 [docker-compose.yml](https://github.com/jay94ks/lightapr/blob/main/docker-compose.yml) 파일을 활용해 서비스를 손쉽게 오케스트레이션할 수 있습니다.

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
# Docker Compose 실행
docker compose up -d
```

## 7. 프로토콜 및 개발 규격 문서
- 세부 프로토콜 규격: [PROTOCOL.md](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.md) | [English](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.en.md)
- C++ 개발 지침: [AGENTS.md](https://github.com/jay94ks/lightapr/blob/main/AGENTS.md) | [English](https://github.com/jay94ks/lightapr/blob/main/AGENTS.en.md)
